// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L

#include "doc_internal.h"

#include <errno.h>
#include <json-c/json.h>
#include <openssl/evp.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <synapse/core.h>

static void set_error(char *error, size_t size, const char *format, ...) {
    if (!error || !size) return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

void sd_document_init(sd_document *document) {
    memset(document, 0, sizeof(*document));
}

void sd_document_free(sd_document *document) {
    if (!document) return;
    for (size_t i = 0; i < document->block_count; i++) {
        free(document->blocks[i].text);
        free(document->blocks[i].target);
        free(document->blocks[i].info);
    }
    free(document->blocks);
    sd_document_init(document);
}

const char *sd_format_name(sd_format format) {
    switch (format) {
    case SD_FORMAT_MARKDOWN: return "markdown";
    case SD_FORMAT_ASCIIDOC: return "asciidoc";
    case SD_FORMAT_RST: return "rst";
    }
    return "unknown";
}

const char *sd_block_kind_name(sd_block_kind kind) {
    switch (kind) {
    case SD_BLOCK_HEADING: return "heading";
    case SD_BLOCK_PARAGRAPH: return "paragraph";
    case SD_BLOCK_LIST_ITEM: return "list-item";
    case SD_BLOCK_CODE: return "code";
    case SD_BLOCK_QUOTE: return "quote";
    case SD_BLOCK_IMAGE: return "image";
    case SD_BLOCK_ADMONITION: return "admonition";
    case SD_BLOCK_RULE: return "rule";
    case SD_BLOCK_WARNING: return "warning";
    }
    return "unknown";
}

static int utf8_valid(const unsigned char *data, size_t size) {
    size_t i = 0;
    while (i < size) {
        unsigned char ch = data[i++];
        if (ch < 0x80) { if (ch == 0) return 0; continue; }
        unsigned length;
        uint32_t code;
        if ((ch & 0xe0U) == 0xc0U) { length = 2; code = ch & 0x1fU; if (code < 2) return 0; }
        else if ((ch & 0xf0U) == 0xe0U) { length = 3; code = ch & 0x0fU; }
        else if ((ch & 0xf8U) == 0xf0U) { length = 4; code = ch & 0x07U; }
        else return 0;
        if (i + length - 1 > size) return 0;
        for (unsigned j = 1; j < length; j++) {
            unsigned char next = data[i++];
            if ((next & 0xc0U) != 0x80U) return 0;
            code = (code << 6) | (next & 0x3fU);
        }
        if ((length == 3 && code < 0x800U) || (length == 4 && code < 0x10000U)
            || code > 0x10ffffU || (code >= 0xd800U && code <= 0xdfffU)) return 0;
    }
    return 1;
}

int sd_sha256_hex(const void *data, size_t size, char out[65]) {
    EVP_MD_CTX *context = EVP_MD_CTX_new();
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    if (!context) return -1;
    int ok = EVP_DigestInit_ex(context, EVP_sha256(), NULL) == 1
             && EVP_DigestUpdate(context, data, size) == 1
             && EVP_DigestFinal_ex(context, digest, &digest_size) == 1;
    EVP_MD_CTX_free(context);
    if (!ok || digest_size != 32) return -1;
    for (unsigned int i = 0; i < digest_size; i++) (void)snprintf(out + i * 2, 3, "%02x", digest[i]);
    out[64] = '\0';
    return 0;
}

static char *bounded_dup(const char *text, size_t limit) {
    if (!text) text = "";
    size_t size = strlen(text);
    if (size > limit) { errno = E2BIG; return NULL; }
    char *copy = malloc(size + 1);
    if (copy) memcpy(copy, text, size + 1);
    return copy;
}

static int add_block(sd_document *document, sd_block_kind kind, int level, const char *text,
                     const char *target, const char *info, char *error, size_t error_size) {
    if (document->block_count >= SD_MAX_BLOCKS) {
        set_error(error, error_size, "document exceeds block limit");
        return -1;
    }
    if (document->block_count == document->block_capacity) {
        size_t next = document->block_capacity ? document->block_capacity * 2U : 64U;
        if (next > SD_MAX_BLOCKS) next = SD_MAX_BLOCKS;
        sd_block *grown = realloc(document->blocks, next * sizeof(*grown));
        if (!grown) return -1;
        memset(grown + document->block_capacity, 0,
               (next - document->block_capacity) * sizeof(*grown));
        document->blocks = grown;
        document->block_capacity = next;
    }
    sd_block block = {.kind = kind, .level = level};
    block.text = bounded_dup(text, SD_BLOCK_TEXT_LIMIT);
    block.target = bounded_dup(target, SD_TARGET_LIMIT);
    block.info = bounded_dup(info, SD_TITLE_LIMIT);
    if (!block.text || !block.target || !block.info) {
        free(block.text); free(block.target); free(block.info);
        set_error(error, error_size, "document block exceeds bound or allocation failed");
        return -1;
    }
    document->blocks[document->block_count++] = block;
    if (kind == SD_BLOCK_WARNING) document->warning_count++;
    if (kind == SD_BLOCK_HEADING && !document->title[0]) {
        size_t length = strlen(text);
        if (length > SD_TITLE_LIMIT) length = SD_TITLE_LIMIT;
        memcpy(document->title, text, length);
        document->title[length] = '\0';
    }
    return 0;
}

static int append_text(char **buffer, size_t *used, size_t *capacity, const char *text,
                       const char *separator) {
    size_t text_size = strlen(text), separator_size = *used ? strlen(separator) : 0;
    if (*used + separator_size + text_size > SD_BLOCK_TEXT_LIMIT) return -1;
    size_t needed = *used + separator_size + text_size + 1;
    if (needed > *capacity) {
        size_t next = *capacity ? *capacity : 256U;
        while (next < needed) {
            if (next > SD_BLOCK_TEXT_LIMIT / 2U) { next = SD_BLOCK_TEXT_LIMIT + 1U; break; }
            next *= 2U;
        }
        char *grown = realloc(*buffer, next);
        if (!grown) return -1;
        *buffer = grown; *capacity = next;
    }
    if (separator_size) { memcpy(*buffer + *used, separator, separator_size); *used += separator_size; }
    memcpy(*buffer + *used, text, text_size); *used += text_size;
    (*buffer)[*used] = '\0';
    return 0;
}

static int flush_paragraph(sd_document *document, char **paragraph, size_t *used, size_t *capacity,
                           char *error, size_t error_size) {
    int result = 0;
    if (*used) result = add_block(document, SD_BLOCK_PARAGRAPH, 0, *paragraph, "", "",
                                  error, error_size);
    free(*paragraph); *paragraph = NULL; *used = 0; *capacity = 0;
    return result;
}

static int split_lines(char *data, size_t size, char ***lines_out, size_t *count_out) {
    size_t count = 1, line_size = 0;
    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\n') {
            if (line_size > SD_LINE_LIMIT || ++count > SD_MAX_LINES) { errno = E2BIG; return -1; }
            line_size = 0;
        } else if (data[i] != '\r') line_size++;
    }
    if (line_size > SD_LINE_LIMIT) { errno = E2BIG; return -1; }
    char **lines = calloc(count + 1, sizeof(*lines));
    if (!lines) return -1;
    size_t index = 0;
    lines[index++] = data;
    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\r' && (i + 1 == size || data[i + 1] == '\n')) data[i] = '\0';
        if (data[i] == '\n') {
            data[i] = '\0';
            lines[index++] = data + i + 1;
        }
    }
    *lines_out = lines;
    *count_out = index;
    return 0;
}

static int collect_code(char **lines, size_t count, size_t *index, const char *closing,
                        const char *indent, char **result, int *closed) {
    char *buffer = NULL; size_t used = 0, capacity = 0;
    *closed = 0;
    for ((*index)++; *index < count; (*index)++) {
        char *line = lines[*index];
        if (closing && strcmp(line, closing) == 0) { *closed = 1; break; }
        if (!closing && (!line[0] || (indent && strncmp(line, indent, strlen(indent)) != 0))) {
            (*index)--;
            *closed = 1;
            break;
        }
        const char *content = !closing && indent ? line + strlen(indent) : line;
        if (append_text(&buffer, &used, &capacity, content, "\n") != 0) { free(buffer); return -1; }
    }
    if (!buffer) buffer = bounded_dup("", 0);
    *result = buffer;
    return buffer ? 0 : -1;
}

static int is_remote(const char *target) {
    return strstr(target, "://") != NULL || strncmp(target, "data:", 5) == 0;
}

static int parse_markdown(char **lines, size_t count, sd_document *document,
                          char *error, size_t error_size) {
    char *paragraph = NULL; size_t used = 0, capacity = 0;
    for (size_t i = 0; i < count; i++) {
        char *line = lines[i];
        char *trimmed = synapse_trim(line);
        if (!trimmed[0]) { if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)) return -1; continue; }
        if (strncmp(trimmed, "```", 3) == 0 || strncmp(trimmed, "~~~", 3) == 0) {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)) return -1;
            char closing[4] = {trimmed[0], trimmed[0], trimmed[0], '\0'};
            const char *info = synapse_trim(trimmed + 3);
            char *code = NULL; int closed = 0;
            if (collect_code(lines, count, &i, closing, NULL, &code, &closed) != 0) return -1;
            if (add_block(document, SD_BLOCK_CODE, 0, code, "", info, error, error_size)) { free(code); return -1; }
            free(code);
            if (!closed && add_block(document, SD_BLOCK_WARNING, 0, "Unclosed Markdown code fence", "", "parser", error, error_size)) return -1;
            continue;
        }
        size_t hashes = 0;
        while (hashes < 6 && trimmed[hashes] == '#') hashes++;
        if (hashes && trimmed[hashes] == ' ') {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_HEADING, (int)hashes,
                             synapse_trim(trimmed + hashes + 1), "", "", error, error_size)) return -1;
            continue;
        }
        if (strncmp(trimmed, "![", 2) == 0) {
            char *middle = strstr(trimmed + 2, "](");
            char *end = middle ? strrchr(middle + 2, ')') : NULL;
            if (middle && end && end[1] == '\0') {
                if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)) return -1;
                *middle = '\0'; *end = '\0';
                const char *target = middle + 2;
                if (add_block(document, SD_BLOCK_IMAGE, 0, trimmed + 2, target, "", error, error_size)) return -1;
                if (is_remote(target) && add_block(document, SD_BLOCK_WARNING, 0, "Remote image loading is disabled", target, "security", error, error_size)) return -1;
                continue;
            }
        }
        if ((trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed[1] == ' ') {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_LIST_ITEM, 1, synapse_trim(trimmed + 2), "", "unordered", error, error_size)) return -1;
            continue;
        }
        char *number_end = trimmed;
        while (*number_end >= '0' && *number_end <= '9') number_end++;
        if (number_end > trimmed && number_end[0] == '.' && number_end[1] == ' ') {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_LIST_ITEM, 1, synapse_trim(number_end + 2), "", "ordered", error, error_size)) return -1;
            continue;
        }
        if (trimmed[0] == '>' && (trimmed[1] == ' ' || trimmed[1] == '\0')) {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_QUOTE, 0, synapse_trim(trimmed + 1), "", "", error, error_size)) return -1;
            continue;
        }
        if (strcmp(trimmed, "---") == 0 || strcmp(trimmed, "***") == 0 || strcmp(trimmed, "___") == 0) {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_RULE, 0, "", "", "", error, error_size)) return -1;
            continue;
        }
        if (trimmed[0] == '<' && add_block(document, SD_BLOCK_WARNING, 0,
                                           "Raw HTML is rendered as text", "", "security",
                                           error, error_size) != 0) {
            free(paragraph); return -1;
        }
        if (append_text(&paragraph, &used, &capacity, trimmed, " ") != 0) {
            free(paragraph); set_error(error, error_size, "Markdown paragraph exceeds limit"); return -1;
        }
    }
    return flush_paragraph(document, &paragraph, &used, &capacity, error, error_size);
}

static int parse_image_macro(char *text, const char *prefix, char **target, char **alt) {
    size_t prefix_size = strlen(prefix);
    if (strncmp(text, prefix, prefix_size) != 0) return 0;
    char *open = strchr(text + prefix_size, '[');
    char *close = open ? strrchr(open + 1, ']') : NULL;
    if (!open || !close || close[1] != '\0') return 0;
    *open = '\0'; *close = '\0'; *target = text + prefix_size; *alt = open + 1;
    return 1;
}

static int parse_asciidoc(char **lines, size_t count, sd_document *document,
                          char *error, size_t error_size) {
    char *paragraph = NULL; size_t used = 0, capacity = 0;
    char pending_info[SD_TITLE_LIMIT + 1] = {0};
    for (size_t i = 0; i < count; i++) {
        char *trimmed = synapse_trim(lines[i]);
        if (!trimmed[0]) { if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)) return -1; continue; }
        if (strncmp(trimmed, "[source", 7) == 0 && trimmed[strlen(trimmed) - 1] == ']') {
            const char *comma = strchr(trimmed, ',');
            if (comma) {
                size_t length = strlen(comma + 1); if (length && comma[1 + length - 1] == ']') length--;
                if (length > SD_TITLE_LIMIT) length = SD_TITLE_LIMIT;
                memcpy(pending_info, comma + 1, length); pending_info[length] = '\0';
            } else strcpy(pending_info, "text");
            continue;
        }
        if (strcmp(trimmed, "----") == 0) {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)) return -1;
            char *code = NULL; int closed = 0;
            if (collect_code(lines, count, &i, "----", NULL, &code, &closed) != 0) return -1;
            if (add_block(document, SD_BLOCK_CODE, 0, code, "", pending_info, error, error_size)) { free(code); return -1; }
            pending_info[0] = '\0'; free(code);
            if (!closed && add_block(document, SD_BLOCK_WARNING, 0, "Unclosed AsciiDoc block", "", "parser", error, error_size)) return -1;
            continue;
        }
        size_t equals = 0;
        while (equals < 6 && trimmed[equals] == '=') equals++;
        if (equals && trimmed[equals] == ' ') {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_HEADING, (int)equals,
                             synapse_trim(trimmed + equals + 1), "", "", error, error_size)) return -1;
            continue;
        }
        char *target = NULL, *alt = NULL;
        if (parse_image_macro(trimmed, "image::", &target, &alt)) {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_IMAGE, 0, *alt ? alt : target, target, "", error, error_size)) return -1;
            if (is_remote(target) && add_block(document, SD_BLOCK_WARNING, 0, "Remote image loading is disabled", target, "security", error, error_size)) return -1;
            continue;
        }
        if (strncmp(trimmed, "include::", 9) == 0 || strcmp(trimmed, "++++") == 0) {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_WARNING, 0, "AsciiDoc include/passthrough is disabled", "", "security", error, error_size)) return -1;
            continue;
        }
        static const char *admonitions[] = {"NOTE:", "TIP:", "IMPORTANT:", "WARNING:", "CAUTION:"};
        int matched = 0;
        for (size_t a = 0; a < sizeof(admonitions) / sizeof(admonitions[0]); a++) {
            size_t length = strlen(admonitions[a]);
            if (strncmp(trimmed, admonitions[a], length) == 0) {
                if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                    || add_block(document, SD_BLOCK_ADMONITION, 0, synapse_trim(trimmed + length), "", admonitions[a], error, error_size)) return -1;
                matched = 1; break;
            }
        }
        if (matched) continue;
        if ((trimmed[0] == '*' || trimmed[0] == '-') && trimmed[1] == ' ') {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_LIST_ITEM, 1, synapse_trim(trimmed + 2), "", "unordered", error, error_size)) return -1;
            continue;
        }
        if (trimmed[0] == '.' && trimmed[1] == ' ') {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_LIST_ITEM, 1, synapse_trim(trimmed + 2), "", "ordered", error, error_size)) return -1;
            continue;
        }
        if (append_text(&paragraph, &used, &capacity, trimmed, " ") != 0) {
            free(paragraph); set_error(error, error_size, "AsciiDoc paragraph exceeds limit"); return -1;
        }
    }
    return flush_paragraph(document, &paragraph, &used, &capacity, error, error_size);
}

static int underline_heading(const char *line, size_t title_length, int *level) {
    if (!line[0]) return 0;
    char marker = line[0];
    if (!strchr("=-~^\"`", marker)) return 0;
    size_t length = 0;
    while (line[length] == marker) length++;
    if (line[length] || length < 2 || length + 2 < title_length) return 0;
    const char *order = "=-~^\"`";
    *level = (int)(strchr(order, marker) - order) + 1;
    return 1;
}

static int parse_rst(char **lines, size_t count, sd_document *document,
                     char *error, size_t error_size) {
    char *paragraph = NULL; size_t used = 0, capacity = 0;
    for (size_t i = 0; i < count; i++) {
        char *trimmed = synapse_trim(lines[i]);
        if (!trimmed[0]) { if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)) return -1; continue; }
        int heading_level = 0;
        if (i + 1 < count && underline_heading(synapse_trim(lines[i + 1]), strlen(trimmed), &heading_level)) {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_HEADING, heading_level, trimmed, "", "", error, error_size)) return -1;
            i++; continue;
        }
        if (strncmp(trimmed, ".. image::", 10) == 0) {
            char *target = synapse_trim(trimmed + 10);
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_IMAGE, 0, target, target, "", error, error_size)) return -1;
            if (is_remote(target) && add_block(document, SD_BLOCK_WARNING, 0, "Remote image loading is disabled", target, "security", error, error_size)) return -1;
            continue;
        }
        if (strncmp(trimmed, ".. include::", 12) == 0 || strncmp(trimmed, ".. raw::", 8) == 0) {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_WARNING, 0, "reStructuredText include/raw directive is disabled", "", "security", error, error_size)) return -1;
            continue;
        }
        if (strncmp(trimmed, ".. code-block::", 15) == 0) {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)) return -1;
            char info[SD_TITLE_LIMIT + 1];
            const char *source = synapse_trim(trimmed + 15); size_t length = strlen(source);
            if (length > SD_TITLE_LIMIT) length = SD_TITLE_LIMIT;
            memcpy(info, source, length); info[length] = '\0';
            while (i + 1 < count && !synapse_trim(lines[i + 1])[0]) i++;
            char *code = NULL; int closed = 0;
            if (collect_code(lines, count, &i, NULL, "   ", &code, &closed) != 0) return -1;
            if (add_block(document, SD_BLOCK_CODE, 0, code, "", info, error, error_size)) { free(code); return -1; }
            free(code); continue;
        }
        if (strncmp(trimmed, ".. note::", 9) == 0 || strncmp(trimmed, ".. warning::", 12) == 0
            || strncmp(trimmed, ".. tip::", 8) == 0) {
            char *body = strstr(trimmed, "::") + 2;
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_ADMONITION, 0, synapse_trim(body), "", "directive", error, error_size)) return -1;
            continue;
        }
        if ((trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed[1] == ' ') {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_LIST_ITEM, 1, synapse_trim(trimmed + 2), "", "unordered", error, error_size)) return -1;
            continue;
        }
        if (lines[i][0] == ' ' && lines[i][1] == ' ' && lines[i][2] == ' ') {
            if (flush_paragraph(document, &paragraph, &used, &capacity, error, error_size)
                || add_block(document, SD_BLOCK_QUOTE, 0, synapse_trim(lines[i]), "", "", error, error_size)) return -1;
            continue;
        }
        if (append_text(&paragraph, &used, &capacity, trimmed, " ") != 0) {
            free(paragraph); set_error(error, error_size, "reStructuredText paragraph exceeds limit"); return -1;
        }
    }
    return flush_paragraph(document, &paragraph, &used, &capacity, error, error_size);
}

static int detect_format(const char *path, const char *option, sd_format *format) {
    if (option && strcmp(option, "auto") != 0) {
        if (strcmp(option, "markdown") == 0) *format = SD_FORMAT_MARKDOWN;
        else if (strcmp(option, "asciidoc") == 0) *format = SD_FORMAT_ASCIIDOC;
        else if (strcmp(option, "rst") == 0) *format = SD_FORMAT_RST;
        else return -1;
        return 0;
    }
    const char *dot = strrchr(path, '.');
    if (!dot) return -1;
    if (synapse_ascii_casecmp(dot, ".md") == 0 || synapse_ascii_casecmp(dot, ".markdown") == 0
        || synapse_ascii_casecmp(dot, ".mdown") == 0) *format = SD_FORMAT_MARKDOWN;
    else if (synapse_ascii_casecmp(dot, ".adoc") == 0
             || synapse_ascii_casecmp(dot, ".asciidoc") == 0) *format = SD_FORMAT_ASCIIDOC;
    else if (synapse_ascii_casecmp(dot, ".rst") == 0 || synapse_ascii_casecmp(dot, ".rest") == 0)
        *format = SD_FORMAT_RST;
    else return -1;
    return 0;
}

int sd_document_load(const char *path, const char *format_option, sd_document *document,
                     char *error, size_t error_size) {
    if (detect_format(path, format_option ? format_option : "auto", &document->format) != 0) {
        set_error(error, error_size, "unknown document format; use --input markdown|asciidoc|rst");
        return -1;
    }
    struct stat status;
    if (lstat(path, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size < 0
        || (uint64_t)status.st_size > SD_INPUT_LIMIT) {
        set_error(error, error_size, "input must be a regular non-symlink file up to 8 MiB");
        return -1;
    }
    size_t size = 0;
    char *data = synapse_read_file(path, SD_INPUT_LIMIT, &size);
    if (!data || size != (size_t)status.st_size || !utf8_valid((const unsigned char *)data, size)) {
        free(data);
        set_error(error, error_size, "input is incomplete, non-UTF-8 or contains NUL");
        return -1;
    }
    document->source_bytes = size;
    if (sd_sha256_hex(data, size, document->source_sha256) != 0) {
        free(data); set_error(error, error_size, "cannot hash document"); return -1;
    }
    const char *base = strrchr(path, '/'); base = base ? base + 1 : path;
    char fallback_title[SD_TITLE_LIMIT + 1];
    size_t base_size = strlen(base); if (base_size > SD_TITLE_LIMIT) base_size = SD_TITLE_LIMIT;
    memcpy(fallback_title, base, base_size); fallback_title[base_size] = '\0';
    char **lines = NULL; size_t line_count = 0;
    if (split_lines(data, size, &lines, &line_count) != 0) {
        free(data); set_error(error, error_size, "document exceeds line count or line length limit"); return -1;
    }
    int result = document->format == SD_FORMAT_MARKDOWN
                 ? parse_markdown(lines, line_count, document, error, error_size)
                 : document->format == SD_FORMAT_ASCIIDOC
                   ? parse_asciidoc(lines, line_count, document, error, error_size)
                   : parse_rst(lines, line_count, document, error, error_size);
    free(lines); free(data);
    if (!document->title[0]) memcpy(document->title, fallback_title, base_size + 1);
    if (result != 0 || document->block_count == 0) {
        if (!error[0]) set_error(error, error_size, "document has no renderable blocks");
        return -1;
    }
    return 0;
}

char *sd_document_to_json(const sd_document *document, size_t *size_out) {
    json_object *root = json_object_new_object();
    json_object *blocks = json_object_new_array();
    if (!root || !blocks) { if (root) json_object_put(root); if (blocks) json_object_put(blocks); return NULL; }
    json_object_object_add(root, "schema", json_object_new_string("synapse.doc.ast/v1"));
    json_object_object_add(root, "format", json_object_new_string(sd_format_name(document->format)));
    json_object_object_add(root, "title", json_object_new_string(document->title));
    json_object_object_add(root, "sourceSha256", json_object_new_string(document->source_sha256));
    json_object_object_add(root, "sourceBytes", json_object_new_int64((int64_t)document->source_bytes));
    json_object_object_add(root, "warnings", json_object_new_int64((int64_t)document->warning_count));
    for (size_t i = 0; i < document->block_count; i++) {
        const sd_block *block = &document->blocks[i];
        json_object *item = json_object_new_object();
        json_object_object_add(item, "kind", json_object_new_string(sd_block_kind_name(block->kind)));
        json_object_object_add(item, "level", json_object_new_int(block->level));
        json_object_object_add(item, "text", json_object_new_string(block->text));
        json_object_object_add(item, "target", json_object_new_string(block->target));
        json_object_object_add(item, "info", json_object_new_string(block->info));
        json_object_array_add(blocks, item);
    }
    json_object_object_add(root, "blocks", blocks);
    const char *serialized = json_object_to_json_string_ext(
        root, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
    size_t size = strlen(serialized);
    char *copy = malloc(size + 1);
    if (copy) { memcpy(copy, serialized, size + 1); if (size_out) *size_out = size; }
    json_object_put(root);
    return copy;
}

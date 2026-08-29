// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L

#include "doc_internal.h"

#include <errno.h>
#include <json-c/json.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <synapse/core.h>

static void set_error(char *error, size_t size, const char *format, ...) {
    if (!error || size == 0) return;
    va_list arguments;
    va_start(arguments, format);
    (void)vsnprintf(error, size, format, arguments);
    va_end(arguments);
}

static int ascii_space(char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

static int horizontal_space(char ch) {
    return ch == ' ' || ch == '\t';
}

static int ascii_alnum(unsigned char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
           || (ch >= '0' && ch <= '9');
}

static int escaped_at(const char *data, size_t position, size_t floor) {
    size_t slashes = 0;
    while (position > floor && data[position - 1U] == '\\') {
        position--;
        slashes++;
    }
    return (slashes & 1U) != 0;
}

static size_t line_end_for(const char *data, size_t size, size_t start) {
    size_t end = start;
    while (end < size && data[end] != '\n') end++;
    if (end > start && data[end - 1U] == '\r') end--;
    return end;
}

static size_t next_line_for(const char *data, size_t size, size_t start) {
    size_t next = start;
    while (next < size && data[next] != '\n') next++;
    if (next < size) next++;
    return next;
}

static void trim_horizontal(const char *data, size_t *start, size_t *end) {
    while (*start < *end && horizontal_space(data[*start])) (*start)++;
    while (*end > *start && horizontal_space(data[*end - 1U])) (*end)--;
}

static void trim_space(const char *data, size_t *start, size_t *end) {
    while (*start < *end && ascii_space(data[*start])) (*start)++;
    while (*end > *start && ascii_space(data[*end - 1U])) (*end)--;
}

static int range_equals(const char *data, size_t start, size_t end,
                        const char *text) {
    trim_space(data, &start, &end);
    size_t length = strlen(text);
    return end - start == length && memcmp(data + start, text, length) == 0;
}

static char *copy_range(const char *data, size_t start, size_t end, size_t limit) {
    if (!data || end < start || end - start > limit) {
        errno = E2BIG;
        return NULL;
    }
    size_t size = end - start;
    char *copy = malloc(size + 1U);
    if (!copy) return NULL;
    if (size) memcpy(copy, data + start, size);
    copy[size] = '\0';
    return copy;
}

static char *copy_text(const char *text, size_t limit) {
    if (!text) text = "";
    return copy_range(text, 0, strlen(text), limit);
}

static int grow_array(void **items, size_t *capacity, size_t count,
                      size_t item_size, size_t maximum) {
    if (count < *capacity) return 0;
    if (count >= maximum) {
        errno = E2BIG;
        return -1;
    }
    size_t next = *capacity ? *capacity * 2U : 32U;
    if (next > maximum) next = maximum;
    if (next <= count || next > SIZE_MAX / item_size) {
        errno = EOVERFLOW;
        return -1;
    }
    void *grown = realloc(*items, next * item_size);
    if (!grown) return -1;
    memset((char *)grown + (*capacity * item_size), 0,
           (next - *capacity) * item_size);
    *items = grown;
    *capacity = next;
    return 0;
}

void sd_presentation_init(sd_presentation *presentation) {
    if (presentation) memset(presentation, 0, sizeof(*presentation));
}

void sd_presentation_free(sd_presentation *presentation) {
    if (!presentation) return;
    for (size_t i = 0; i < presentation->block_count; i++) {
        sd_presentation_block *item = &presentation->blocks[i];
        free(item->text);
        free(item->target);
        free(item->info);
        for (size_t j = 0; j < item->run_count; j++) {
            free(item->runs[j].text);
            free(item->runs[j].target);
            free(item->runs[j].heading);
            free(item->runs[j].block);
        }
        free(item->runs);
    }
    free(presentation->blocks);
    sd_presentation_init(presentation);
}

const char *sd_inline_kind_name(sd_inline_kind kind) {
    switch (kind) {
    case SD_INLINE_TEXT: return "text";
    case SD_INLINE_EMPHASIS: return "emphasis";
    case SD_INLINE_STRONG: return "strong";
    case SD_INLINE_CODE: return "code";
    case SD_INLINE_LINK: return "link";
    case SD_INLINE_WIKILINK: return "wikilink";
    case SD_INLINE_EMBED: return "embed";
    case SD_INLINE_IMAGE: return "image";
    case SD_INLINE_TAG: return "tag";
    }
    return "unknown";
}

static int external_target(const char *target) {
    return strstr(target, "://") != NULL || strncmp(target, "mailto:", 7) == 0
           || strncmp(target, "data:", 5) == 0;
}

static int push_run(sd_presentation *presentation, sd_presentation_block *block,
                    sd_inline_kind kind, char *text, char *target, char *heading,
                    char *fragment_block, size_t start, size_t end,
                    size_t text_start, size_t text_end, char *error,
                    size_t error_size) {
    if (!text || !target || !heading || !fragment_block || end < start
        || text_start < start || text_end < text_start || text_end > end
        || presentation->run_count >= SD_MAX_PRESENTATION_RUNS
        || grow_array((void **)&block->runs, &block->run_capacity,
                      block->run_count, sizeof(*block->runs),
                      SD_MAX_PRESENTATION_RUNS) != 0) {
        free(text);
        free(target);
        free(heading);
        free(fragment_block);
        set_error(error, error_size,
                  "presentation inline run exceeds bound or allocation failed");
        return -1;
    }
    block->runs[block->run_count++] = (sd_inline_run){
        .kind = kind,
        .text = text,
        .target = target,
        .heading = heading,
        .block = fragment_block,
        .external = external_target(target),
        .start_byte = start,
        .end_byte = end,
        .text_start_byte = text_start,
        .text_end_byte = text_end
    };
    presentation->run_count++;
    return 0;
}

static int add_simple_run(sd_presentation *presentation,
                          sd_presentation_block *block, sd_inline_kind kind,
                          const char *data, size_t start, size_t end,
                          size_t text_start, size_t text_end, char *error,
                          size_t error_size) {
    char *text = copy_range(data, text_start, text_end, SD_BLOCK_TEXT_LIMIT);
    char *empty_target = copy_text("", 0);
    char *empty_heading = copy_text("", 0);
    char *empty_block = copy_text("", 0);
    return push_run(presentation, block, kind, text, empty_target,
                    empty_heading, empty_block, start, end, text_start,
                    text_end, error, error_size);
}

static int split_destination(const char *data, size_t start, size_t end,
                             char **target_out, char **heading_out,
                             char **block_out, char *error,
                             size_t error_size) {
    trim_space(data, &start, &end);
    size_t hash = end;
    for (size_t i = start; i < end; i++) {
        if (data[i] == '#' && !escaped_at(data, i, start)) {
            hash = i;
            break;
        }
    }
    size_t target_end = hash;
    trim_space(data, &start, &target_end);
    size_t fragment_start = hash < end ? hash + 1U : end;
    size_t fragment_end = end;
    trim_space(data, &fragment_start, &fragment_end);

    char *target = copy_range(data, start, target_end, SD_TARGET_LIMIT);
    char *heading = NULL;
    char *fragment_block = NULL;
    if (!target) goto fail;
    if (fragment_start < fragment_end && data[fragment_start] == '^') {
        fragment_start++;
        trim_space(data, &fragment_start, &fragment_end);
        heading = copy_text("", 0);
        fragment_block = copy_range(data, fragment_start, fragment_end,
                                    SD_TARGET_LIMIT);
    } else {
        heading = copy_range(data, fragment_start, fragment_end,
                             SD_TARGET_LIMIT);
        fragment_block = copy_text("", 0);
    }
    if (!heading || !fragment_block) goto fail;
    *target_out = target;
    *heading_out = heading;
    *block_out = fragment_block;
    return 0;

fail:
    free(target);
    free(heading);
    free(fragment_block);
    set_error(error, error_size,
              "presentation link destination exceeds bound or allocation failed");
    return -1;
}

static int add_link_run(sd_presentation *presentation,
                        sd_presentation_block *block, sd_inline_kind kind,
                        const char *data, size_t start, size_t end,
                        size_t text_start, size_t text_end,
                        size_t destination_start, size_t destination_end,
                        char *error, size_t error_size) {
    char *text = copy_range(data, text_start, text_end, SD_LINK_LABEL_LIMIT);
    char *target = NULL;
    char *heading = NULL;
    char *fragment_block = NULL;
    if (split_destination(data, destination_start, destination_end, &target,
                          &heading, &fragment_block, error, error_size) != 0) {
        free(text);
        return -1;
    }
    if (!text) {
        free(target);
        free(heading);
        free(fragment_block);
        set_error(error, error_size, "presentation link label exceeds bound");
        return -1;
    }
    return push_run(presentation, block, kind, text, target, heading,
                    fragment_block, start, end, text_start, text_end,
                    error, error_size);
}

static size_t run_length(const char *data, size_t end, size_t position,
                         char marker) {
    size_t length = 0;
    while (position + length < end && data[position + length] == marker)
        length++;
    return length;
}

static size_t inline_line_end(const char *data, size_t end, size_t position) {
    size_t line_end = position;
    while (line_end < end && data[line_end] != '\n' && data[line_end] != '\r')
        line_end++;
    return line_end;
}

static int scan_code(const char *data, size_t range_start, size_t range_end,
                     size_t position, size_t *token_end,
                     size_t *text_start, size_t *text_end) {
    if (data[position] != '`' || escaped_at(data, position, range_start)) return 0;
    size_t line_end = inline_line_end(data, range_end, position);
    size_t marker_size = run_length(data, line_end, position, '`');
    size_t cursor = position + marker_size;
    while (cursor < line_end) {
        if (data[cursor] == '`'
            && run_length(data, line_end, cursor, '`') == marker_size) {
            *token_end = cursor + marker_size;
            *text_start = position + marker_size;
            *text_end = cursor;
            return 1;
        }
        cursor++;
    }
    return 0;
}

static int scan_wikilink(const char *data, size_t range_start, size_t range_end,
                         size_t position, int *embed, size_t *token_end,
                         size_t *text_start, size_t *text_end,
                         size_t *destination_start,
                         size_t *destination_end) {
    *embed = data[position] == '!';
    size_t open = position + (*embed ? 1U : 0U);
    size_t line_end = inline_line_end(data, range_end, position);
    if (open + 1U >= line_end || data[open] != '[' || data[open + 1U] != '['
        || escaped_at(data, open, range_start)) return 0;
    size_t content_start = open + 2U;
    size_t close = content_start;
    while (close + 1U < line_end) {
        if (data[close] == ']' && data[close + 1U] == ']'
            && !escaped_at(data, close, content_start)) break;
        close++;
    }
    if (close + 1U >= line_end) return 0;
    size_t pipe = close;
    for (size_t i = content_start; i < close; i++) {
        if (data[i] == '|' && !escaped_at(data, i, content_start)) {
            pipe = i;
            break;
        }
    }
    *destination_start = content_start;
    *destination_end = pipe;
    *text_start = pipe < close ? pipe + 1U : content_start;
    *text_end = pipe < close ? close : pipe;
    *token_end = close + 2U;
    return 1;
}

static int scan_markdown_link(const char *data, size_t range_start,
                              size_t range_end, size_t position, int *image,
                              size_t *token_end, size_t *text_start,
                              size_t *text_end, size_t *destination_start,
                              size_t *destination_end) {
    *image = data[position] == '!';
    size_t open = position + (*image ? 1U : 0U);
    size_t line_end = inline_line_end(data, range_end, position);
    if (open >= line_end || data[open] != '['
        || escaped_at(data, open, range_start)
        || (open + 1U < line_end && data[open + 1U] == '[')) return 0;
    size_t close = open + 1U;
    while (close < line_end
           && (data[close] != ']' || escaped_at(data, close, open + 1U)))
        close++;
    if (close + 1U >= line_end || data[close + 1U] != '(') return 0;
    size_t destination = close + 2U;
    size_t cursor = destination;
    int depth = 1;
    while (cursor < line_end) {
        if (!escaped_at(data, cursor, destination)) {
            if (data[cursor] == '(') depth++;
            else if (data[cursor] == ')' && --depth == 0) break;
        }
        cursor++;
    }
    if (cursor >= line_end) return 0;
    size_t destination_finish = cursor;
    trim_space(data, &destination, &destination_finish);
    if (destination < destination_finish && data[destination] == '<'
        && data[destination_finish - 1U] == '>') {
        destination++;
        destination_finish--;
    } else {
        size_t first_space = destination;
        while (first_space < destination_finish
               && !ascii_space(data[first_space])) first_space++;
        destination_finish = first_space;
    }
    *token_end = cursor + 1U;
    *text_start = open + 1U;
    *text_end = close;
    *destination_start = destination;
    *destination_end = destination_finish;
    return 1;
}

static int scan_emphasis(const char *data, size_t range_start,
                         size_t range_end, size_t position, size_t width,
                         size_t *token_end, size_t *text_start,
                         size_t *text_end) {
    char marker = data[position];
    if ((marker != '*' && marker != '_')
        || escaped_at(data, position, range_start)) return 0;
    size_t line_end = inline_line_end(data, range_end, position);
    if (run_length(data, line_end, position, marker) < width
        || position + width >= line_end
        || ascii_space(data[position + width])) return 0;
    if (marker == '_' && position > range_start
        && ascii_alnum((unsigned char)data[position - 1U])) return 0;
    size_t cursor = position + width;
    while (cursor + width <= line_end) {
        if (data[cursor] == marker
            && run_length(data, line_end, cursor, marker) >= width
            && !escaped_at(data, cursor, position + width)
            && cursor > position + width
            && !ascii_space(data[cursor - 1U])) {
            if (marker == '_' && cursor + width < line_end
                && ascii_alnum((unsigned char)data[cursor + width])) {
                cursor++;
                continue;
            }
            *token_end = cursor + width;
            *text_start = position + width;
            *text_end = cursor;
            return 1;
        }
        cursor++;
    }
    return 0;
}

static int tag_character(unsigned char ch) {
    return ch >= 0x80U || ascii_alnum(ch) || ch == '_' || ch == '-'
           || ch == '/';
}

static int tag_boundary(const char *data, size_t position, size_t floor) {
    if (position == floor) return 1;
    unsigned char before = (unsigned char)data[position - 1U];
    return ascii_space((char)before) || before == '(' || before == '['
           || before == '{' || before == '>' || before == ':' || before == ';'
           || before == ',';
}

static int parse_inline(sd_presentation *presentation,
                        sd_presentation_block *block, const char *data,
                        size_t start, size_t end, char *error,
                        size_t error_size) {
    size_t plain_start = start;
    size_t position = start;
    while (position < end) {
        size_t token_end = 0;
        size_t text_start = 0;
        size_t text_end = 0;
        size_t destination_start = 0;
        size_t destination_end = 0;
        sd_inline_kind kind = SD_INLINE_TEXT;
        int matched = 0;
        int link_flag = 0;

        if (scan_code(data, start, end, position, &token_end,
                      &text_start, &text_end)) {
            kind = SD_INLINE_CODE;
            matched = 1;
        } else if ((data[position] == '[' || data[position] == '!')
                   && scan_wikilink(data, start, end, position, &link_flag,
                                    &token_end, &text_start, &text_end,
                                    &destination_start, &destination_end)) {
            kind = link_flag ? SD_INLINE_EMBED : SD_INLINE_WIKILINK;
            matched = 2;
        } else if ((data[position] == '[' || data[position] == '!')
                   && scan_markdown_link(data, start, end, position, &link_flag,
                                         &token_end, &text_start, &text_end,
                                         &destination_start,
                                         &destination_end)) {
            kind = link_flag ? SD_INLINE_IMAGE : SD_INLINE_LINK;
            matched = 2;
        } else if ((data[position] == '*' || data[position] == '_')
                   && scan_emphasis(data, start, end, position, 2U,
                                    &token_end, &text_start, &text_end)) {
            kind = SD_INLINE_STRONG;
            matched = 1;
        } else if ((data[position] == '*' || data[position] == '_')
                   && scan_emphasis(data, start, end, position, 1U,
                                    &token_end, &text_start, &text_end)) {
            kind = SD_INLINE_EMPHASIS;
            matched = 1;
        } else if (data[position] == '#'
                   && !escaped_at(data, position, start)
                   && tag_boundary(data, position, start)
                   && position + 1U < end
                   && tag_character((unsigned char)data[position + 1U])) {
            token_end = position + 1U;
            int non_digit = 0;
            while (token_end < end
                   && tag_character((unsigned char)data[token_end])) {
                if (!ascii_alnum((unsigned char)data[token_end])
                    || data[token_end] < '0' || data[token_end] > '9')
                    non_digit = 1;
                token_end++;
            }
            if (non_digit) {
                text_start = position;
                text_end = token_end;
                destination_start = position + 1U;
                destination_end = token_end;
                kind = SD_INLINE_TAG;
                matched = 3;
            }
        }

        if (!matched) {
            position++;
            continue;
        }
        if (plain_start < position
            && add_simple_run(presentation, block, SD_INLINE_TEXT, data,
                              plain_start, position, plain_start, position,
                              error, error_size) != 0) return -1;
        if (matched == 1) {
            if (add_simple_run(presentation, block, kind, data, position,
                               token_end, text_start, text_end, error,
                               error_size) != 0) return -1;
        } else if (matched == 2) {
            if (add_link_run(presentation, block, kind, data, position,
                             token_end, text_start, text_end,
                             destination_start, destination_end, error,
                             error_size) != 0) return -1;
        } else {
            char *text = copy_range(data, text_start, text_end,
                                    SD_LINK_LABEL_LIMIT);
            char *target = copy_range(data, destination_start,
                                      destination_end, SD_TARGET_LIMIT);
            char *heading = copy_text("", 0);
            char *fragment_block = copy_text("", 0);
            if (push_run(presentation, block, kind, text, target, heading,
                         fragment_block, position, token_end, text_start,
                         text_end, error, error_size) != 0) return -1;
        }
        position = token_end;
        plain_start = position;
    }
    if (plain_start < end
        && add_simple_run(presentation, block, SD_INLINE_TEXT, data,
                          plain_start, end, plain_start, end, error,
                          error_size) != 0) return -1;
    return 0;
}

static int build_block_text(sd_presentation_block *block, char *error,
                            size_t error_size) {
    size_t total = 0;
    for (size_t i = 0; i < block->run_count; i++) {
        size_t length = strlen(block->runs[i].text);
        if (length > SD_BLOCK_TEXT_LIMIT - total) {
            set_error(error, error_size,
                      "presentation block text exceeds bound");
            return -1;
        }
        total += length;
    }
    char *text = malloc(total + 1U);
    if (!text) return -1;
    size_t used = 0;
    for (size_t i = 0; i < block->run_count; i++) {
        size_t length = strlen(block->runs[i].text);
        if (length) memcpy(text + used, block->runs[i].text, length);
        used += length;
    }
    text[used] = '\0';
    free(block->text);
    block->text = text;
    return 0;
}

static sd_presentation_block *push_block(sd_presentation *presentation,
                                         sd_block_kind kind, int level,
                                         int generated, size_t start,
                                         size_t end, size_t text_start,
                                         size_t text_end, const char *target,
                                         const char *info, char *error,
                                         size_t error_size) {
    if (presentation->block_count >= SD_MAX_BLOCKS || end < start
        || text_start < start || text_end < text_start || text_end > end
        || grow_array((void **)&presentation->blocks,
                      &presentation->block_capacity,
                      presentation->block_count,
                      sizeof(*presentation->blocks), SD_MAX_BLOCKS) != 0) {
        set_error(error, error_size,
                  "presentation block exceeds bound or allocation failed");
        return NULL;
    }
    sd_presentation_block *block =
        &presentation->blocks[presentation->block_count++];
    *block = (sd_presentation_block){
        .kind = kind,
        .level = level,
        .generated = generated,
        .start_byte = start,
        .end_byte = end,
        .text_start_byte = text_start,
        .text_end_byte = text_end
    };
    block->text = copy_text("", 0);
    block->target = copy_text(target ? target : "", SD_TARGET_LIMIT);
    block->info = copy_text(info ? info : "", SD_TITLE_LIMIT);
    if (!block->text || !block->target || !block->info) {
        set_error(error, error_size,
                  "presentation block value exceeds bound or allocation failed");
        return NULL;
    }
    return block;
}

static int add_inline_block(sd_presentation *presentation, sd_block_kind kind,
                            int level, const char *data, size_t start,
                            size_t end, size_t text_start, size_t text_end,
                            const char *target, const char *info,
                            char *error, size_t error_size) {
    sd_presentation_block *block = push_block(
        presentation, kind, level, 0, start, end, text_start, text_end,
        target, info, error, error_size);
    if (!block || parse_inline(presentation, block, data, text_start, text_end,
                               error, error_size) != 0
        || build_block_text(block, error, error_size) != 0) return -1;
    return 0;
}

static int add_code_block(sd_presentation *presentation, const char *data,
                          size_t start, size_t end, size_t text_start,
                          size_t text_end, const char *info, char *error,
                          size_t error_size) {
    sd_presentation_block *block = push_block(
        presentation, SD_BLOCK_CODE, 0, 0, start, end, text_start, text_end,
        "", info, error, error_size);
    if (!block) return -1;
    free(block->text);
    block->text = copy_range(data, text_start, text_end, SD_BLOCK_TEXT_LIMIT);
    if (!block->text) {
        set_error(error, error_size, "presentation code block exceeds bound");
        return -1;
    }
    return 0;
}

static int add_rule_block(sd_presentation *presentation, size_t start,
                          size_t end, char *error, size_t error_size) {
    return push_block(presentation, SD_BLOCK_RULE, 0, 0, start, end,
                      start, start, "", "", error, error_size) ? 0 : -1;
}

static int add_warning(sd_presentation *presentation, size_t anchor,
                       const char *message, const char *info, char *error,
                       size_t error_size) {
    sd_presentation_block *block = push_block(
        presentation, SD_BLOCK_WARNING, 0, 1, anchor, anchor, anchor, anchor,
        "", info, error, error_size);
    if (!block) return -1;
    free(block->text);
    block->text = copy_text(message, SD_BLOCK_TEXT_LIMIT);
    if (!block->text) return -1;
    presentation->warning_count++;
    return 0;
}

static int validate_lines(const char *data, size_t size, char *error,
                          size_t error_size) {
    size_t count = 1;
    size_t length = 0;
    for (size_t i = 0; i < size; i++) {
        if (data[i] == '\n') {
            if (length > SD_LINE_LIMIT || ++count > SD_MAX_LINES) {
                set_error(error, error_size,
                          "presentation exceeds line count or line length limit");
                return -1;
            }
            length = 0;
        } else if (data[i] != '\r') {
            length++;
        }
    }
    if (length > SD_LINE_LIMIT) {
        set_error(error, error_size,
                  "presentation exceeds line count or line length limit");
        return -1;
    }
    return 0;
}

static int simple_frontmatter_title(const char *data, size_t start, size_t end,
                                    char output[SD_TITLE_LIMIT + 1]) {
    trim_space(data, &start, &end);
    if (start == end || data[start] == '|' || data[start] == '>'
        || data[start] == '[' || data[start] == '{' || data[start] == '&'
        || data[start] == '*' || data[start] == '!') return 0;
    if (data[start] == '\'' || data[start] == '"') {
        char quote = data[start];
        if (end - start < 2U || data[end - 1U] != quote) return 0;
        start++;
        end--;
        for (size_t i = start; i < end; i++)
            if (data[i] == '\\' || data[i] == quote) return 0;
    } else {
        for (size_t i = start; i < end; i++) {
            if (data[i] == '#' && (i == start || ascii_space(data[i - 1U]))) {
                end = i;
                break;
            }
        }
        trim_space(data, &start, &end);
    }
    if (end - start > SD_TITLE_LIMIT) return 0;
    memcpy(output, data + start, end - start);
    output[end - start] = '\0';
    return 1;
}

static int parse_frontmatter(const char *data, size_t size,
                             sd_presentation *presentation,
                             size_t *body_start, char *error,
                             size_t error_size) {
    size_t first = size >= 3U && (unsigned char)data[0] == 0xefU
                   && (unsigned char)data[1] == 0xbbU
                   && (unsigned char)data[2] == 0xbfU ? 3U : 0U;
    *body_start = first;
    size_t first_end = line_end_for(data, size, first);
    if (!range_equals(data, first, first_end, "---")) return 0;
    presentation->frontmatter_present = 1;
    presentation->frontmatter_start_byte = first;
    size_t position = next_line_for(data, size, first);
    while (position < size && position - first <= SD_FRONTMATTER_LIMIT) {
        size_t end = line_end_for(data, size, position);
        if (range_equals(data, position, end, "---")
            || range_equals(data, position, end, "...")) {
            *body_start = next_line_for(data, size, position);
            presentation->frontmatter_end_byte = *body_start;
            return 0;
        }
        size_t start = position;
        size_t trimmed_end = end;
        trim_horizontal(data, &start, &trimmed_end);
        if (start == position && trimmed_end > start) {
            size_t colon = start;
            while (colon < trimmed_end && data[colon] != ':') colon++;
            if (colon < trimmed_end && colon - start == strlen("title")
                && memcmp(data + start, "title", strlen("title")) == 0) {
                (void)simple_frontmatter_title(
                    data, colon + 1U, trimmed_end,
                    presentation->frontmatter_title);
            }
        }
        position = next_line_for(data, size, position);
    }
    set_error(error, error_size,
              "frontmatter is unclosed or exceeds 64 KiB");
    return -1;
}

static int rule_line(const char *data, size_t start, size_t end) {
    trim_horizontal(data, &start, &end);
    if (end - start < 3U) return 0;
    char marker = data[start];
    if (marker != '-' && marker != '*' && marker != '_') return 0;
    size_t count = 0;
    for (size_t i = start; i < end; i++) {
        if (data[i] == marker) count++;
        else if (!horizontal_space(data[i])) return 0;
    }
    return count >= 3U;
}

static int fence_open(const char *data, size_t start, size_t end,
                      char *marker, size_t *length, size_t *info_start,
                      size_t *info_end) {
    trim_horizontal(data, &start, &end);
    if (start >= end || (data[start] != '`' && data[start] != '~')) return 0;
    size_t count = run_length(data, end, start, data[start]);
    if (count < 3U) return 0;
    *marker = data[start];
    *length = count;
    *info_start = start + count;
    *info_end = end;
    trim_horizontal(data, info_start, info_end);
    return 1;
}

static int fence_close(const char *data, size_t start, size_t end,
                       char marker, size_t minimum) {
    trim_horizontal(data, &start, &end);
    size_t count = run_length(data, end, start, marker);
    if (count < minimum) return 0;
    start += count;
    while (start < end && horizontal_space(data[start])) start++;
    return start == end;
}

static int list_prefix(const char *data, size_t start, size_t end,
                       size_t *text_start, const char **info) {
    size_t cursor = start;
    while (cursor < end && horizontal_space(data[cursor])) cursor++;
    if (cursor + 1U < end && (data[cursor] == '-' || data[cursor] == '*'
                             || data[cursor] == '+')
        && horizontal_space(data[cursor + 1U])) {
        *text_start = cursor + 2U;
        while (*text_start < end && horizontal_space(data[*text_start]))
            (*text_start)++;
        *info = "unordered";
        return 1;
    }
    size_t number = cursor;
    while (number < end && data[number] >= '0' && data[number] <= '9') number++;
    if (number > cursor && number + 1U < end && data[number] == '.'
        && horizontal_space(data[number + 1U])) {
        *text_start = number + 2U;
        while (*text_start < end && horizontal_space(data[*text_start]))
            (*text_start)++;
        *info = "ordered";
        return 1;
    }
    return 0;
}

static int heading_prefix(const char *data, size_t start, size_t end,
                          int *level, size_t *text_start) {
    while (start < end && horizontal_space(data[start])) start++;
    size_t hashes = 0;
    while (start + hashes < end && hashes < 6U
           && data[start + hashes] == '#') hashes++;
    if (hashes == 0 || start + hashes >= end
        || !horizontal_space(data[start + hashes])) return 0;
    *level = (int)hashes;
    *text_start = start + hashes + 1U;
    while (*text_start < end && horizontal_space(data[*text_start]))
        (*text_start)++;
    return 1;
}

static int line_structural(const char *data, size_t start, size_t end) {
    size_t trimmed = start;
    while (trimmed < end && horizontal_space(data[trimmed])) trimmed++;
    if (trimmed == end) return 1;
    char marker = '\0';
    size_t length = 0;
    size_t info_start = 0;
    size_t info_end = 0;
    int level = 0;
    size_t text_start = 0;
    const char *info = NULL;
    return fence_open(data, start, end, &marker, &length, &info_start,
                      &info_end)
           || heading_prefix(data, start, end, &level, &text_start)
           || list_prefix(data, start, end, &text_start, &info)
           || data[trimmed] == '>' || rule_line(data, start, end);
}

static int block_has_external_media(const sd_presentation_block *block) {
    for (size_t i = 0; i < block->run_count; i++) {
        const sd_inline_run *run = &block->runs[i];
        if ((run->kind == SD_INLINE_IMAGE || run->kind == SD_INLINE_EMBED)
            && run->external) return 1;
    }
    return 0;
}

static int parse_markdown_presentation(const char *data, size_t size,
                                       size_t body_start,
                                       sd_presentation *presentation,
                                       char *error, size_t error_size) {
    size_t position = body_start;
    while (position < size) {
        size_t line_end = line_end_for(data, size, position);
        size_t next = next_line_for(data, size, position);
        size_t trimmed_start = position;
        size_t trimmed_end = line_end;
        trim_horizontal(data, &trimmed_start, &trimmed_end);
        if (trimmed_start == trimmed_end) {
            position = next;
            continue;
        }

        char fence_marker = '\0';
        size_t fence_length = 0;
        size_t info_start = 0;
        size_t info_end = 0;
        if (fence_open(data, position, line_end, &fence_marker, &fence_length,
                       &info_start, &info_end)) {
            char *info = copy_range(data, info_start, info_end, SD_TITLE_LIMIT);
            if (!info) return -1;
            size_t content_start = next;
            size_t cursor = next;
            size_t close_start = size;
            size_t block_end = size;
            while (cursor < size) {
                size_t cursor_end = line_end_for(data, size, cursor);
                if (fence_close(data, cursor, cursor_end, fence_marker,
                                fence_length)) {
                    close_start = cursor;
                    block_end = next_line_for(data, size, cursor);
                    break;
                }
                cursor = next_line_for(data, size, cursor);
            }
            size_t content_end = close_start;
            if (content_end > content_start && data[content_end - 1U] == '\n') {
                content_end--;
                if (content_end > content_start
                    && data[content_end - 1U] == '\r') content_end--;
            }
            int result = add_code_block(presentation, data, position, block_end,
                                        content_start, content_end, info,
                                        error, error_size);
            free(info);
            if (result != 0) return -1;
            if (close_start == size
                && add_warning(presentation, size,
                               "Unclosed Markdown code fence", "parser",
                               error, error_size) != 0) return -1;
            position = block_end;
            continue;
        }

        int heading_level = 0;
        size_t text_start = 0;
        if (heading_prefix(data, position, line_end, &heading_level,
                           &text_start)) {
            size_t text_end = line_end;
            trim_horizontal(data, &text_start, &text_end);
            if (add_inline_block(presentation, SD_BLOCK_HEADING,
                                 heading_level, data, position, next,
                                 text_start, text_end, "", "", error,
                                 error_size) != 0) return -1;
            if (!presentation->title[0]) {
                const char *title = presentation->blocks[
                    presentation->block_count - 1U].text;
                size_t length = strlen(title);
                if (length > SD_TITLE_LIMIT) length = SD_TITLE_LIMIT;
                memcpy(presentation->title, title, length);
                presentation->title[length] = '\0';
            }
            position = next;
            continue;
        }

        const char *list_info = NULL;
        if (list_prefix(data, position, line_end, &text_start, &list_info)) {
            size_t text_end = line_end;
            trim_horizontal(data, &text_start, &text_end);
            size_t indentation = trimmed_start - position;
            int level = (int)(indentation / 2U) + 1;
            if (level > 16) level = 16;
            if (add_inline_block(presentation, SD_BLOCK_LIST_ITEM, level,
                                 data, position, next, text_start, text_end,
                                 "", list_info, error, error_size) != 0)
                return -1;
            if (block_has_external_media(
                    &presentation->blocks[presentation->block_count - 1U])
                && add_warning(presentation, next,
                               "Remote media loading is disabled", "security",
                               error, error_size) != 0) return -1;
            position = next;
            continue;
        }

        if (data[trimmed_start] == '>') {
            text_start = trimmed_start + 1U;
            while (text_start < trimmed_end
                   && horizontal_space(data[text_start])) text_start++;
            if (add_inline_block(presentation, SD_BLOCK_QUOTE, 0, data,
                                 position, next, text_start, trimmed_end,
                                 "", "", error, error_size) != 0) return -1;
            if (block_has_external_media(
                    &presentation->blocks[presentation->block_count - 1U])
                && add_warning(presentation, next,
                               "Remote media loading is disabled", "security",
                               error, error_size) != 0) return -1;
            position = next;
            continue;
        }

        if (rule_line(data, position, line_end)) {
            if (add_rule_block(presentation, position, next, error,
                               error_size) != 0) return -1;
            position = next;
            continue;
        }

        size_t paragraph_start = position;
        size_t paragraph_text_start = trimmed_start;
        size_t paragraph_text_end = trimmed_end;
        size_t paragraph_end = next;
        size_t cursor = next;
        while (cursor < size) {
            size_t cursor_end = line_end_for(data, size, cursor);
            size_t cursor_start = cursor;
            size_t cursor_trimmed_end = cursor_end;
            trim_horizontal(data, &cursor_start, &cursor_trimmed_end);
            if (cursor_start == cursor_trimmed_end
                || line_structural(data, cursor, cursor_end)) break;
            paragraph_text_end = cursor_trimmed_end;
            paragraph_end = next_line_for(data, size, cursor);
            cursor = paragraph_end;
            if (paragraph_text_end - paragraph_text_start
                > SD_BLOCK_TEXT_LIMIT) {
                set_error(error, error_size,
                          "presentation paragraph exceeds limit");
                return -1;
            }
        }
        if (add_inline_block(presentation, SD_BLOCK_PARAGRAPH, 0, data,
                             paragraph_start, paragraph_end,
                             paragraph_text_start, paragraph_text_end,
                             "", "", error, error_size) != 0) return -1;
        sd_presentation_block *paragraph =
            &presentation->blocks[presentation->block_count - 1U];
        if (paragraph->run_count == 1U
            && paragraph->runs[0].kind == SD_INLINE_IMAGE
            && paragraph->runs[0].start_byte == paragraph_text_start
            && paragraph->runs[0].end_byte == paragraph_text_end) {
            paragraph->kind = SD_BLOCK_IMAGE;
            free(paragraph->target);
            paragraph->target = copy_text(paragraph->runs[0].target,
                                          SD_TARGET_LIMIT);
            if (!paragraph->target) return -1;
        }
        int raw_html = data[paragraph_text_start] == '<';
        int remote_media = block_has_external_media(paragraph);
        if (raw_html
            && add_warning(presentation, paragraph_end,
                           "Raw HTML is rendered as text", "security",
                           error, error_size) != 0) return -1;
        if (remote_media
            && add_warning(presentation, paragraph_end,
                           "Remote media loading is disabled", "security",
                           error, error_size) != 0) return -1;
        position = paragraph_end;
    }
    return 0;
}

int sd_presentation_load(const char *path, const char *format_option,
                         sd_presentation *presentation, char *error,
                         size_t error_size) {
    if (!presentation || !path || !*path) {
        set_error(error, error_size, "presentation and input path are required");
        return -1;
    }
    const char *option = format_option ? format_option : "auto";
    if (strcmp(option, "markdown") != 0) {
        const char *dot = strrchr(path ? path : "", '.');
        if (strcmp(option, "auto") != 0 || !dot
            || (synapse_ascii_casecmp(dot, ".md") != 0
                && synapse_ascii_casecmp(dot, ".markdown") != 0
                && synapse_ascii_casecmp(dot, ".mdown") != 0)) {
            set_error(error, error_size,
                      "semantic presentation supports Markdown input only");
            return -1;
        }
    }
    char *data = NULL;
    size_t size = 0;
    if (sd_read_source(path, &data, &size, presentation->source_sha256,
                       error, error_size) != 0) return -1;
    presentation->format = SD_FORMAT_MARKDOWN;
    presentation->source_bytes = size;
    if (validate_lines(data, size, error, error_size) != 0) {
        free(data);
        return -1;
    }
    size_t body_start = 0;
    int result = parse_frontmatter(data, size, presentation, &body_start,
                                   error, error_size);
    if (result == 0)
        result = parse_markdown_presentation(data, size, body_start,
                                             presentation, error, error_size);
    if (result == 0 && presentation->frontmatter_title[0]) {
        size_t length = strlen(presentation->frontmatter_title);
        memcpy(presentation->title, presentation->frontmatter_title,
               length + 1U);
    }
    if (result == 0 && !presentation->title[0]) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        size_t length = strlen(base);
        if (length > SD_TITLE_LIMIT) length = SD_TITLE_LIMIT;
        memcpy(presentation->title, base, length);
        presentation->title[length] = '\0';
    }
    free(data);
    if (result != 0) {
        sd_presentation_free(presentation);
        return -1;
    }
    return 0;
}

static json_object *run_to_json(const sd_inline_run *run) {
    json_object *item = json_object_new_object();
    if (!item) return NULL;
    json_object_object_add(item, "kind",
                           json_object_new_string(sd_inline_kind_name(run->kind)));
    json_object_object_add(item, "text", json_object_new_string(run->text));
    json_object_object_add(item, "target", json_object_new_string(run->target));
    json_object_object_add(item, "heading", json_object_new_string(run->heading));
    json_object_object_add(item, "block", json_object_new_string(run->block));
    json_object_object_add(item, "external",
                           json_object_new_boolean(run->external));
    json_object_object_add(item, "startByte",
                           json_object_new_int64((int64_t)run->start_byte));
    json_object_object_add(item, "endByte",
                           json_object_new_int64((int64_t)run->end_byte));
    json_object_object_add(item, "textStartByte",
                           json_object_new_int64((int64_t)run->text_start_byte));
    json_object_object_add(item, "textEndByte",
                           json_object_new_int64((int64_t)run->text_end_byte));
    return item;
}

char *sd_presentation_to_json(const sd_presentation *presentation,
                              size_t *size_out) {
    json_object *root = json_object_new_object();
    json_object *frontmatter = json_object_new_object();
    json_object *blocks = json_object_new_array();
    if (!root || !frontmatter || !blocks) goto fail;

    json_object_object_add(root, "schema",
                           json_object_new_string("synapse.doc.presentation/v1"));
    json_object_object_add(root, "format", json_object_new_string("markdown"));
    json_object_object_add(root, "title",
                           json_object_new_string(presentation->title));
    json_object_object_add(root, "sourceSha256",
                           json_object_new_string(presentation->source_sha256));
    json_object_object_add(root, "sourceBytes",
                           json_object_new_int64((int64_t)presentation->source_bytes));
    json_object_object_add(frontmatter, "present",
                           json_object_new_boolean(presentation->frontmatter_present));
    json_object_object_add(frontmatter, "startByte",
                           json_object_new_int64(
                               (int64_t)presentation->frontmatter_start_byte));
    json_object_object_add(frontmatter, "endByte",
                           json_object_new_int64(
                               (int64_t)presentation->frontmatter_end_byte));
    json_object_object_add(frontmatter, "title",
                           json_object_new_string(
                               presentation->frontmatter_title));
    json_object_object_add(root, "frontmatter", frontmatter);
    frontmatter = NULL;
    json_object_object_add(root, "warnings",
                           json_object_new_int64(
                               (int64_t)presentation->warning_count));

    for (size_t i = 0; i < presentation->block_count; i++) {
        const sd_presentation_block *block = &presentation->blocks[i];
        json_object *item = json_object_new_object();
        json_object *runs = json_object_new_array();
        if (!item || !runs) {
            if (item) json_object_put(item);
            if (runs) json_object_put(runs);
            goto fail;
        }
        json_object_object_add(item, "kind",
                               json_object_new_string(
                                   sd_block_kind_name(block->kind)));
        json_object_object_add(item, "level",
                               json_object_new_int(block->level));
        json_object_object_add(item, "generated",
                               json_object_new_boolean(block->generated));
        json_object_object_add(item, "text",
                               json_object_new_string(block->text));
        json_object_object_add(item, "target",
                               json_object_new_string(block->target));
        json_object_object_add(item, "info",
                               json_object_new_string(block->info));
        json_object_object_add(item, "startByte",
                               json_object_new_int64(
                                   (int64_t)block->start_byte));
        json_object_object_add(item, "endByte",
                               json_object_new_int64(
                                   (int64_t)block->end_byte));
        json_object_object_add(item, "textStartByte",
                               json_object_new_int64(
                                   (int64_t)block->text_start_byte));
        json_object_object_add(item, "textEndByte",
                               json_object_new_int64(
                                   (int64_t)block->text_end_byte));
        for (size_t j = 0; j < block->run_count; j++) {
            json_object *run = run_to_json(&block->runs[j]);
            if (!run) {
                json_object_put(runs);
                json_object_put(item);
                goto fail;
            }
            json_object_array_add(runs, run);
        }
        json_object_object_add(item, "runs", runs);
        json_object_array_add(blocks, item);
    }
    json_object_object_add(root, "blocks", blocks);
    blocks = NULL;

    const char *serialized = json_object_to_json_string_ext(
        root, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
    size_t size = strlen(serialized);
    if (size > SD_OUTPUT_LIMIT) {
        errno = E2BIG;
        json_object_put(root);
        return NULL;
    }
    char *copy = malloc(size + 1U);
    if (copy) {
        memcpy(copy, serialized, size + 1U);
        if (size_out) *size_out = size;
    }
    json_object_put(root);
    return copy;

fail:
    if (blocks) json_object_put(blocks);
    if (frontmatter) json_object_put(frontmatter);
    if (root) json_object_put(root);
    return NULL;
}

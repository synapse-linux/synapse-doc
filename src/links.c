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

#define SD_FRONTMATTER_LIMIT (64U * 1024U)

typedef enum {
    META_NONE,
    META_IGNORE,
    META_ALIASES,
    META_TAGS
} metadata_list;

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

static void trim_range(const char *data, size_t *start, size_t *end) {
    while (*start < *end && ascii_space(data[*start])) (*start)++;
    while (*end > *start && ascii_space(data[*end - 1U])) (*end)--;
}

static char *copy_range(const char *data, size_t start, size_t end, size_t limit) {
    if (end < start || end - start > limit) {
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

static int grow_array(void **items, size_t *capacity, size_t count, size_t item_size,
                      size_t maximum) {
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

void sd_link_index_init(sd_link_index *index) {
    if (index) memset(index, 0, sizeof(*index));
}

void sd_link_index_free(sd_link_index *index) {
    if (!index) return;
    for (size_t i = 0; i < index->alias_count; i++) free(index->aliases[i].value);
    for (size_t i = 0; i < index->tag_count; i++) free(index->tags[i].value);
    for (size_t i = 0; i < index->link_count; i++) {
        free(index->links[i].target);
        free(index->links[i].heading);
        free(index->links[i].block);
        free(index->links[i].label);
    }
    free(index->aliases);
    free(index->tags);
    free(index->links);
    sd_link_index_init(index);
}

const char *sd_link_kind_name(sd_link_kind kind) {
    switch (kind) {
    case SD_LINK_WIKILINK: return "wikilink";
    case SD_LINK_EMBED: return "embed";
    case SD_LINK_MARKDOWN: return "markdown";
    case SD_LINK_IMAGE: return "image";
    }
    return "unknown";
}

const char *sd_tag_source_name(sd_tag_source source) {
    switch (source) {
    case SD_TAG_FRONTMATTER: return "frontmatter";
    case SD_TAG_INLINE: return "inline";
    }
    return "unknown";
}

static int add_alias(sd_link_index *index, const char *data, size_t start, size_t end,
                     char *error, size_t error_size) {
    trim_range(data, &start, &end);
    if (start == end) return 0;
    if (grow_array((void **)&index->aliases, &index->alias_capacity,
                   index->alias_count, sizeof(*index->aliases),
                   SD_MAX_METADATA_VALUES) != 0) {
        set_error(error, error_size, "alias count exceeds bound or allocation failed");
        return -1;
    }
    char *value = copy_range(data, start, end, SD_TITLE_LIMIT);
    if (!value) {
        set_error(error, error_size, "alias exceeds bound or allocation failed");
        return -1;
    }
    index->aliases[index->alias_count++] = (sd_metadata_value){
        .value = value, .start_byte = start, .end_byte = end
    };
    return 0;
}

static int add_tag(sd_link_index *index, const char *data, size_t start, size_t end,
                   sd_tag_source source, char *error, size_t error_size) {
    trim_range(data, &start, &end);
    if (start < end && data[start] == '#') start++;
    trim_range(data, &start, &end);
    if (start == end) return 0;
    if (grow_array((void **)&index->tags, &index->tag_capacity,
                   index->tag_count, sizeof(*index->tags),
                   SD_MAX_LINKS) != 0) {
        set_error(error, error_size, "tag count exceeds bound or allocation failed");
        return -1;
    }
    char *value = copy_range(data, start, end, SD_TARGET_LIMIT);
    if (!value) {
        set_error(error, error_size, "tag exceeds bound or allocation failed");
        return -1;
    }
    index->tags[index->tag_count++] = (sd_tag){
        .value = value, .source = source, .start_byte = start, .end_byte = end
    };
    return 0;
}

static int is_external_target(const char *target) {
    return strstr(target, "://") != NULL || strncmp(target, "mailto:", 7) == 0
           || strncmp(target, "data:", 5) == 0;
}

static int escaped_at(const char *data, size_t position, size_t floor) {
    size_t slashes = 0;
    while (position > floor && data[position - 1U] == '\\') {
        position--;
        slashes++;
    }
    return (slashes & 1U) != 0;
}

static int split_destination(const char *data, size_t start, size_t end, int allow_empty,
                             char **target_out, char **heading_out, char **block_out,
                             char *error, size_t error_size) {
    trim_range(data, &start, &end);
    size_t hash = end;
    for (size_t i = start; i < end; i++) {
        if (data[i] == '#' && !escaped_at(data, i, start)) {
            hash = i;
            break;
        }
    }
    size_t target_end = hash;
    trim_range(data, &start, &target_end);
    size_t fragment_start = hash < end ? hash + 1U : end;
    size_t fragment_end = end;
    trim_range(data, &fragment_start, &fragment_end);

    char *target = copy_range(data, start, target_end, SD_TARGET_LIMIT);
    char *heading = NULL;
    char *block = NULL;
    if (!target) goto fail;
    if (fragment_start < fragment_end && data[fragment_start] == '^') {
        fragment_start++;
        trim_range(data, &fragment_start, &fragment_end);
        block = copy_range(data, fragment_start, fragment_end, SD_TARGET_LIMIT);
        heading = copy_range("", 0, 0, 0);
    } else {
        heading = copy_range(data, fragment_start, fragment_end, SD_TARGET_LIMIT);
        block = copy_range("", 0, 0, 0);
    }
    if (!heading || !block) goto fail;
    if (!allow_empty && !target[0] && !heading[0] && !block[0]) {
        set_error(error, error_size, "empty note link is not allowed");
        goto fail;
    }
    *target_out = target;
    *heading_out = heading;
    *block_out = block;
    return 0;
fail:
    free(target);
    free(heading);
    free(block);
    if (!error || error_size == 0 || !error[0])
        set_error(error, error_size, "link destination exceeds bound");
    return -1;
}

static int add_link(sd_link_index *index, sd_link_kind kind, const char *data,
                    size_t destination_start, size_t destination_end,
                    size_t label_start, size_t label_end, size_t start_byte,
                    size_t end_byte, char *error, size_t error_size) {
    if (end_byte < start_byte || end_byte > index->source_bytes) {
        set_error(error, error_size, "link source range is invalid");
        return -1;
    }
    if (grow_array((void **)&index->links, &index->link_capacity,
                   index->link_count, sizeof(*index->links), SD_MAX_LINKS) != 0) {
        set_error(error, error_size, "link count exceeds bound or allocation failed");
        return -1;
    }
    sd_link link = {.kind = kind, .start_byte = start_byte, .end_byte = end_byte};
    if (split_destination(data, destination_start, destination_end,
                          kind == SD_LINK_MARKDOWN || kind == SD_LINK_IMAGE,
                          &link.target, &link.heading, &link.block,
                          error, error_size) != 0) return -1;
    trim_range(data, &label_start, &label_end);
    link.label = copy_range(data, label_start, label_end, SD_LINK_LABEL_LIMIT);
    if (!link.label) {
        free(link.target); free(link.heading); free(link.block);
        set_error(error, error_size, "link label exceeds bound or allocation failed");
        return -1;
    }
    link.external = is_external_target(link.target);
    index->links[index->link_count++] = link;
    return 0;
}

static int range_equals(const char *data, size_t start, size_t end, const char *text) {
    size_t size = strlen(text);
    trim_range(data, &start, &end);
    return end - start == size && memcmp(data + start, text, size) == 0;
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

static int scalar_range(const char *data, size_t *start, size_t *end,
                        char *error, size_t error_size) {
    trim_range(data, start, end);
    if (*start == *end) return 0;
    char quote = data[*start];
    if (quote == '\'' || quote == '"') {
        if (*end - *start < 2U || data[*end - 1U] != quote) {
            set_error(error, error_size, "unterminated quoted frontmatter value");
            return -1;
        }
        for (size_t i = *start + 1U; i + 1U < *end; i++) {
            if (data[i] == '\\' || data[i] == quote) {
                set_error(error, error_size,
                          "escaped or nested quoted frontmatter values are unsupported");
                return -1;
            }
        }
        (*start)++;
        (*end)--;
        return 0;
    }
    int quoted = 0;
    for (size_t i = *start; i < *end; i++) {
        if (data[i] == '\'' || data[i] == '"') quoted = !quoted;
        if (data[i] == '#' && !quoted && (i == *start || ascii_space(data[i - 1U]))) {
            *end = i;
            break;
        }
    }
    trim_range(data, start, end);
    return 0;
}

static int add_metadata_scalar(sd_link_index *index, metadata_list list,
                               const char *data, size_t start, size_t end,
                               char *error, size_t error_size) {
    if (scalar_range(data, &start, &end, error, error_size) != 0) return -1;
    if (start == end) return 0;
    if (data[start] == '|' || data[start] == '>' || data[start] == '&'
        || data[start] == '*' || data[start] == '{' || data[start] == '!') {
        set_error(error, error_size, "complex frontmatter values are unsupported");
        return -1;
    }
    return list == META_ALIASES
           ? add_alias(index, data, start, end, error, error_size)
           : add_tag(index, data, start, end, SD_TAG_FRONTMATTER, error, error_size);
}

static int parse_metadata_array(sd_link_index *index, metadata_list list,
                                const char *data, size_t start, size_t end,
                                char *error, size_t error_size) {
    trim_range(data, &start, &end);
    if (end - start < 2U || data[start] != '[' || data[end - 1U] != ']') {
        set_error(error, error_size, "frontmatter list must be a scalar or bounded array");
        return -1;
    }
    start++;
    end--;
    size_t item_start = start;
    char quote = '\0';
    for (size_t i = start; i <= end; i++) {
        char ch = i < end ? data[i] : ',';
        if ((ch == '\'' || ch == '"') && !escaped_at(data, i, item_start)) {
            if (!quote) quote = ch;
            else if (quote == ch) quote = '\0';
        }
        if (ch == ',' && !quote) {
            if (add_metadata_scalar(index, list, data, item_start, i,
                                    error, error_size) != 0) return -1;
            item_start = i + 1U;
        }
    }
    if (quote) {
        set_error(error, error_size, "unterminated quote in frontmatter array");
        return -1;
    }
    return 0;
}

static int set_frontmatter_title(sd_link_index *index, const char *data,
                                 size_t start, size_t end, char *error,
                                 size_t error_size) {
    if (scalar_range(data, &start, &end, error, error_size) != 0) return -1;
    if (end - start > SD_TITLE_LIMIT) {
        set_error(error, error_size, "frontmatter title exceeds bound");
        return -1;
    }
    memcpy(index->title, data + start, end - start);
    index->title[end - start] = '\0';
    return 0;
}

static int parse_frontmatter(const char *data, size_t size, sd_link_index *index,
                             size_t *body_start, char *error, size_t error_size) {
    *body_start = 0;
    size_t first = size >= 3U && (unsigned char)data[0] == 0xefU
                   && (unsigned char)data[1] == 0xbbU
                   && (unsigned char)data[2] == 0xbfU ? 3U : 0U;
    size_t first_end = line_end_for(data, size, first);
    if (!range_equals(data, first, first_end, "---")) return 0;
    index->frontmatter_present = 1;
    metadata_list active = META_NONE;
    size_t position = next_line_for(data, size, first);
    while (position < size && position - first <= SD_FRONTMATTER_LIMIT) {
        size_t end = line_end_for(data, size, position);
        if (range_equals(data, position, end, "---")
            || range_equals(data, position, end, "...")) {
            *body_start = next_line_for(data, size, position);
            return 0;
        }
        size_t start = position;
        trim_range(data, &start, &end);
        if (start == end || data[start] == '#') {
            position = next_line_for(data, size, position);
            continue;
        }
        if (data[start] == '-' && start + 1U < end && ascii_space(data[start + 1U])) {
            if (active == META_NONE || active == META_IGNORE) {
                position = next_line_for(data, size, position);
                continue;
            }
            if (add_metadata_scalar(index, active, data, start + 2U, end,
                                    error, error_size) != 0) return -1;
            position = next_line_for(data, size, position);
            continue;
        }
        active = META_NONE;
        if (start != position) {
            position = next_line_for(data, size, position);
            continue;
        }
        size_t colon = start;
        while (colon < end && data[colon] != ':') colon++;
        if (colon == end) {
            position = next_line_for(data, size, position);
            continue;
        }
        size_t key_end = colon;
        trim_range(data, &start, &key_end);
        size_t value_start = colon + 1U;
        size_t value_end = end;
        trim_range(data, &value_start, &value_end);
        metadata_list list = META_NONE;
        int title = 0;
        if (range_equals(data, start, key_end, "alias")
            || range_equals(data, start, key_end, "aliases")) list = META_ALIASES;
        else if (range_equals(data, start, key_end, "tag")
                 || range_equals(data, start, key_end, "tags")) list = META_TAGS;
        else if (range_equals(data, start, key_end, "title")) title = 1;
        if (title) {
            if (set_frontmatter_title(index, data, value_start, value_end,
                                      error, error_size) != 0) return -1;
        } else if (list != META_NONE) {
            if (value_start == value_end) active = list;
            else if (data[value_start] == '[') {
                if (parse_metadata_array(index, list, data, value_start, value_end,
                                         error, error_size) != 0) return -1;
            } else if (add_metadata_scalar(index, list, data, value_start, value_end,
                                           error, error_size) != 0) return -1;
        } else if (value_start == value_end) active = META_IGNORE;
        position = next_line_for(data, size, position);
    }
    set_error(error, error_size, "frontmatter is unclosed or exceeds 64 KiB");
    return -1;
}

static size_t run_length(const char *data, size_t end, size_t position, char marker) {
    size_t length = 0;
    while (position + length < end && data[position + length] == marker) length++;
    return length;
}

static int only_space_after(const char *data, size_t start, size_t end) {
    while (start < end) {
        if (!ascii_space(data[start])) return 0;
        start++;
    }
    return 1;
}

static int tag_character(unsigned char ch) {
    return ch >= 0x80U || (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
           || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '/';
}

static int tag_boundary_before(const char *data, size_t position, size_t line_start) {
    if (position == line_start) return 1;
    unsigned char before = (unsigned char)data[position - 1U];
    return ascii_space((char)before) || before == '(' || before == '[' || before == '{'
           || before == '>' || before == ':' || before == ';' || before == ',';
}

static size_t find_wikilink_end(const char *data, size_t start, size_t end) {
    for (size_t i = start; i + 1U < end; i++) {
        if (data[i] == ']' && data[i + 1U] == ']' && !escaped_at(data, i, start)) return i;
    }
    return end;
}

static int scan_wikilink(const char *data, size_t line_start, size_t line_end,
                         size_t *position, sd_link_index *index, char *error,
                         size_t error_size) {
    size_t start = *position;
    int embed = data[start] == '!';
    size_t open = start + (embed ? 1U : 0U);
    if (open + 1U >= line_end || data[open] != '[' || data[open + 1U] != '[') return 0;
    if (escaped_at(data, open, line_start)) return 0;
    size_t content_start = open + 2U;
    size_t close = find_wikilink_end(data, content_start, line_end);
    if (close == line_end) {
        set_error(error, error_size, "unclosed wikilink at byte %zu", start);
        return -1;
    }
    size_t pipe = close;
    for (size_t i = content_start; i < close; i++) {
        if (data[i] == '|' && !escaped_at(data, i, content_start)) {
            pipe = i;
            break;
        }
    }
    if (add_link(index, embed ? SD_LINK_EMBED : SD_LINK_WIKILINK, data,
                 content_start, pipe, pipe < close ? pipe + 1U : close, close,
                 start, close + 2U, error, error_size) != 0) return -1;
    *position = close + 2U;
    return 1;
}

static int scan_markdown_link(const char *data, size_t line_start, size_t line_end,
                              size_t *position, sd_link_index *index, char *error,
                              size_t error_size) {
    size_t start = *position;
    int image = data[start] == '!';
    size_t open = start + (image ? 1U : 0U);
    if (open >= line_end || data[open] != '[' || escaped_at(data, open, line_start)) return 0;
    if (open + 1U < line_end && data[open + 1U] == '[') return 0;
    size_t close = open + 1U;
    while (close < line_end && (data[close] != ']' || escaped_at(data, close, open + 1U))) close++;
    if (close + 1U >= line_end || data[close + 1U] != '(') return 0;
    size_t destination_start = close + 2U;
    size_t cursor = destination_start;
    int depth = 1;
    while (cursor < line_end) {
        if (!escaped_at(data, cursor, destination_start)) {
            if (data[cursor] == '(') depth++;
            else if (data[cursor] == ')' && --depth == 0) break;
        }
        cursor++;
    }
    if (cursor >= line_end) return 0;
    size_t destination_end = cursor;
    trim_range(data, &destination_start, &destination_end);
    if (destination_start < destination_end && data[destination_start] == '<'
        && data[destination_end - 1U] == '>') {
        destination_start++;
        destination_end--;
    } else {
        size_t first_space = destination_start;
        while (first_space < destination_end && !ascii_space(data[first_space])) first_space++;
        destination_end = first_space;
    }
    if (add_link(index, image ? SD_LINK_IMAGE : SD_LINK_MARKDOWN, data,
                 destination_start, destination_end, open + 1U, close,
                 start, cursor + 1U, error, error_size) != 0) return -1;
    *position = cursor + 1U;
    return 1;
}

static int scan_body(const char *data, size_t size, size_t body_start,
                     sd_link_index *index, char *error, size_t error_size) {
    int in_fence = 0;
    char fence_marker = '\0';
    size_t fence_length = 0;
    size_t line_start = body_start;
    while (line_start < size) {
        size_t line_end = line_end_for(data, size, line_start);
        size_t first = line_start;
        while (first < line_end && (data[first] == ' ' || data[first] == '\t')) first++;
        if (first < line_end && (data[first] == '`' || data[first] == '~')) {
            size_t length = run_length(data, line_end, first, data[first]);
            if (length >= 3U) {
                if (!in_fence) {
                    in_fence = 1;
                    fence_marker = data[first];
                    fence_length = length;
                    line_start = next_line_for(data, size, line_start);
                    continue;
                }
                if (data[first] == fence_marker && length >= fence_length
                    && only_space_after(data, first + length, line_end)) {
                    in_fence = 0;
                    fence_marker = '\0';
                    fence_length = 0;
                }
                line_start = next_line_for(data, size, line_start);
                continue;
            }
        }
        if (in_fence) {
            line_start = next_line_for(data, size, line_start);
            continue;
        }
        size_t position = line_start;
        while (position < line_end) {
            if (data[position] == '`' && !escaped_at(data, position, line_start)) {
                size_t length = run_length(data, line_end, position, '`');
                size_t close = position + length;
                int found = 0;
                while (close < line_end) {
                    if (data[close] == '`' && run_length(data, line_end, close, '`') == length) {
                        close += length;
                        found = 1;
                        break;
                    }
                    close++;
                }
                position = found ? close : position + length;
                continue;
            }
            int result = 0;
            if ((data[position] == '[' || data[position] == '!')
                && position + 1U < line_end) {
                result = scan_wikilink(data, line_start, line_end, &position,
                                       index, error, error_size);
                if (result < 0) return -1;
                if (result > 0) continue;
                result = scan_markdown_link(data, line_start, line_end, &position,
                                            index, error, error_size);
                if (result < 0) return -1;
                if (result > 0) continue;
            }
            if (data[position] == '#' && !escaped_at(data, position, line_start)
                && tag_boundary_before(data, position, line_start)
                && position + 1U < line_end
                && tag_character((unsigned char)data[position + 1U])) {
                size_t end = position + 1U;
                int non_digit = 0;
                while (end < line_end && tag_character((unsigned char)data[end])) {
                    unsigned char ch = (unsigned char)data[end];
                    if (!(ch >= '0' && ch <= '9')) non_digit = 1;
                    end++;
                }
                if (non_digit && add_tag(index, data, position + 1U, end, SD_TAG_INLINE,
                                         error, error_size) != 0) return -1;
                position = end;
                continue;
            }
            position++;
        }
        line_start = next_line_for(data, size, line_start);
    }
    if (in_fence) {
        set_error(error, error_size, "unclosed Markdown code fence");
        return -1;
    }
    return 0;
}

int sd_link_index_load(const char *path, sd_link_index *index, char *error,
                       size_t error_size) {
    if (!index) {
        set_error(error, error_size, "link index is required");
        return -1;
    }
    char *data = NULL;
    size_t size = 0;
    if (sd_read_source(path, &data, &size, index->source_sha256,
                       error, error_size) != 0) return -1;
    index->source_bytes = size;
    size_t body_start = 0;
    int result = parse_frontmatter(data, size, index, &body_start, error, error_size);
    if (result == 0) result = scan_body(data, size, body_start, index, error, error_size);
    free(data);
    if (result != 0) {
        sd_link_index_free(index);
        return -1;
    }
    return 0;
}

static json_object *metadata_json(const sd_metadata_value *value) {
    json_object *item = json_object_new_object();
    if (!item) return NULL;
    json_object_object_add(item, "value", json_object_new_string(value->value));
    json_object_object_add(item, "startByte", json_object_new_int64((int64_t)value->start_byte));
    json_object_object_add(item, "endByte", json_object_new_int64((int64_t)value->end_byte));
    return item;
}

char *sd_link_index_to_json(const sd_link_index *index, size_t *size_out) {
    json_object *root = json_object_new_object();
    json_object *frontmatter = json_object_new_object();
    json_object *aliases = json_object_new_array();
    json_object *tags = json_object_new_array();
    json_object *links = json_object_new_array();
    if (!root || !frontmatter || !aliases || !tags || !links) goto fail;

    json_object_object_add(root, "schema", json_object_new_string("synapse.doc.links/v1"));
    json_object_object_add(root, "format", json_object_new_string("markdown"));
    json_object_object_add(root, "sourceSha256", json_object_new_string(index->source_sha256));
    json_object_object_add(root, "sourceBytes", json_object_new_int64((int64_t)index->source_bytes));
    json_object_object_add(frontmatter, "present", json_object_new_boolean(index->frontmatter_present));
    json_object_object_add(frontmatter, "title", json_object_new_string(index->title));
    for (size_t i = 0; i < index->alias_count; i++) {
        json_object *item = metadata_json(&index->aliases[i]);
        if (!item) goto fail;
        json_object_array_add(aliases, item);
    }
    json_object_object_add(frontmatter, "aliases", aliases);
    aliases = NULL;
    json_object_object_add(root, "frontmatter", frontmatter);
    frontmatter = NULL;

    for (size_t i = 0; i < index->tag_count; i++) {
        const sd_tag *tag = &index->tags[i];
        json_object *item = json_object_new_object();
        if (!item) goto fail;
        json_object_object_add(item, "value", json_object_new_string(tag->value));
        json_object_object_add(item, "source", json_object_new_string(sd_tag_source_name(tag->source)));
        json_object_object_add(item, "startByte", json_object_new_int64((int64_t)tag->start_byte));
        json_object_object_add(item, "endByte", json_object_new_int64((int64_t)tag->end_byte));
        json_object_array_add(tags, item);
    }
    json_object_object_add(root, "tags", tags);
    tags = NULL;

    for (size_t i = 0; i < index->link_count; i++) {
        const sd_link *link = &index->links[i];
        json_object *item = json_object_new_object();
        if (!item) goto fail;
        json_object_object_add(item, "kind", json_object_new_string(sd_link_kind_name(link->kind)));
        json_object_object_add(item, "target", json_object_new_string(link->target));
        json_object_object_add(item, "heading", json_object_new_string(link->heading));
        json_object_object_add(item, "block", json_object_new_string(link->block));
        json_object_object_add(item, "label", json_object_new_string(link->label));
        json_object_object_add(item, "external", json_object_new_boolean(link->external));
        json_object_object_add(item, "startByte", json_object_new_int64((int64_t)link->start_byte));
        json_object_object_add(item, "endByte", json_object_new_int64((int64_t)link->end_byte));
        json_object_array_add(links, item);
    }
    json_object_object_add(root, "links", links);
    links = NULL;

    const char *serialized = json_object_to_json_string_ext(
        root, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
    size_t size = strlen(serialized);
    char *copy = malloc(size + 1U);
    if (copy) {
        memcpy(copy, serialized, size + 1U);
        if (size_out) *size_out = size;
    }
    json_object_put(root);
    return copy;
fail:
    if (links) json_object_put(links);
    if (tags) json_object_put(tags);
    if (aliases) json_object_put(aliases);
    if (frontmatter) json_object_put(frontmatter);
    if (root) json_object_put(root);
    return NULL;
}

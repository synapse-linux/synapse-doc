// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SYNAPSE_DOC_INTERNAL_H
#define SYNAPSE_DOC_INTERNAL_H

#include <stddef.h>

#define SD_VERSION "0.1.0-alpha.1"
#define SD_INPUT_LIMIT (8U * 1024U * 1024U)
#define SD_OUTPUT_LIMIT (32U * 1024U * 1024U)
#define SD_MAX_BLOCKS 32768U
#define SD_MAX_LINES 262144U
#define SD_LINE_LIMIT 65536U
#define SD_BLOCK_TEXT_LIMIT (256U * 1024U)
#define SD_TITLE_LIMIT 512U
#define SD_TARGET_LIMIT 4096U
#define SD_MAX_LINKS 65536U
#define SD_MAX_METADATA_VALUES 4096U
#define SD_LINK_LABEL_LIMIT 4096U

typedef enum {
    SD_FORMAT_MARKDOWN,
    SD_FORMAT_ASCIIDOC,
    SD_FORMAT_RST
} sd_format;

typedef enum {
    SD_BLOCK_HEADING,
    SD_BLOCK_PARAGRAPH,
    SD_BLOCK_LIST_ITEM,
    SD_BLOCK_CODE,
    SD_BLOCK_QUOTE,
    SD_BLOCK_IMAGE,
    SD_BLOCK_ADMONITION,
    SD_BLOCK_RULE,
    SD_BLOCK_WARNING
} sd_block_kind;

typedef struct {
    sd_block_kind kind;
    int level;
    char *text;
    char *target;
    char *info;
} sd_block;

typedef struct {
    sd_format format;
    char title[SD_TITLE_LIMIT + 1];
    char source_sha256[65];
    sd_block *blocks;
    size_t block_count;
    size_t block_capacity;
    size_t warning_count;
    size_t source_bytes;
} sd_document;

typedef enum {
    SD_LINK_WIKILINK,
    SD_LINK_EMBED,
    SD_LINK_MARKDOWN,
    SD_LINK_IMAGE
} sd_link_kind;

typedef enum {
    SD_TAG_FRONTMATTER,
    SD_TAG_INLINE
} sd_tag_source;

typedef struct {
    char *value;
    size_t start_byte;
    size_t end_byte;
} sd_metadata_value;

typedef struct {
    char *value;
    sd_tag_source source;
    size_t start_byte;
    size_t end_byte;
} sd_tag;

typedef struct {
    sd_link_kind kind;
    char *target;
    char *heading;
    char *block;
    char *label;
    int external;
    size_t start_byte;
    size_t end_byte;
} sd_link;

typedef struct {
    char title[SD_TITLE_LIMIT + 1];
    char source_sha256[65];
    size_t source_bytes;
    sd_metadata_value *aliases;
    size_t alias_count;
    size_t alias_capacity;
    sd_tag *tags;
    size_t tag_count;
    size_t tag_capacity;
    sd_link *links;
    size_t link_count;
    size_t link_capacity;
    int frontmatter_present;
} sd_link_index;

void sd_document_init(sd_document *document);
void sd_document_free(sd_document *document);
int sd_read_source(const char *path, char **data_out, size_t *size_out, char hash_out[65],
                   char *error, size_t error_size);
int sd_document_load(const char *path, const char *format_option, sd_document *document,
                     char *error, size_t error_size);
const char *sd_format_name(sd_format format);
const char *sd_block_kind_name(sd_block_kind kind);
char *sd_document_to_json(const sd_document *document, size_t *size_out);

void sd_link_index_init(sd_link_index *index);
void sd_link_index_free(sd_link_index *index);
int sd_link_index_load(const char *path, sd_link_index *index, char *error,
                       size_t error_size);
const char *sd_link_kind_name(sd_link_kind kind);
const char *sd_tag_source_name(sd_tag_source source);
char *sd_link_index_to_json(const sd_link_index *index, size_t *size_out);
char *sd_render_terminal(const sd_document *document, int colors, size_t width,
                         size_t *size_out, char *error, size_t error_size);
char *sd_render_interactive_html(const sd_document *document, size_t *size_out,
                                 char *error, size_t error_size);
int sd_run_tui(const sd_document *document);
int sd_publish_file(const char *path, const void *data, size_t size, unsigned mode,
                    char *error, size_t error_size);
int sd_sha256_hex(const void *data, size_t size, char out[65]);

#endif

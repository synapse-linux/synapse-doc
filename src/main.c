// SPDX-License-Identifier: GPL-3.0-or-later
#define _POSIX_C_SOURCE 200809L

#include "doc_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <synapse/core.h>

typedef enum { OUTPUT_TEXT, OUTPUT_JSON } output_format;
typedef enum { COLOR_AUTO, COLOR_ALWAYS, COLOR_NEVER } color_mode;

static void usage(FILE *output) {
    fputs("Usage:\n"
          "  synapse-doc inspect INPUT [--input auto|markdown|asciidoc|rst] [--format text|json]\n"
          "  synapse-doc view INPUT [--input auto|markdown|asciidoc|rst] [--format text|json]\n"
          "                         [--color auto|always|never] [--width COLUMNS]\n"
          "  synapse-doc present INPUT [--input auto|markdown] [--format text|json]\n"
          "  synapse-doc links INPUT [--format text|json]\n"
          "  synapse-doc tui INPUT [--input auto|markdown|asciidoc|rst]\n"
          "  synapse-doc export INPUT --artifact text|interactive-html --output FILE\n"
          "                          [--input auto|markdown|asciidoc|rst] [--format text|json]\n"
          "\n"
          "Alpha 1 implements bounded, non-executing safe reader profiles for\n"
          "Markdown, AsciiDoc and reStructuredText. Includes, raw HTML, raw\n"
          "directives, extensions and remote image loading are disabled.\n",
          output);
}

static int parse_format(const char *text, output_format *format) {
    if (strcmp(text, "text") == 0) *format = OUTPUT_TEXT;
    else if (strcmp(text, "json") == 0) *format = OUTPUT_JSON;
    else return -1;
    return 0;
}

static int parse_color(const char *text, color_mode *mode) {
    if (strcmp(text, "auto") == 0) *mode = COLOR_AUTO;
    else if (strcmp(text, "always") == 0) *mode = COLOR_ALWAYS;
    else if (strcmp(text, "never") == 0) *mode = COLOR_NEVER;
    else return -1;
    return 0;
}

static int load_document(const char *path, const char *input, sd_document *document) {
    char error[512] = {0};
    if (sd_document_load(path, input, document, error, sizeof(error)) != 0) {
        fprintf(stderr, "synapse-doc: %s\n", error[0] ? error : strerror(errno));
        return -1;
    }
    return 0;
}

static void inspect_output(const sd_document *document, output_format format) {
    size_t kinds[9] = {0};
    for (size_t i = 0; i < document->block_count; i++) kinds[document->blocks[i].kind]++;
    if (format == OUTPUT_JSON) {
        fputs("{\"schema\":\"synapse.doc.inspect/v1\",\"format\":", stdout);
        synapse_json_string(stdout, sd_format_name(document->format));
        fputs(",\"title\":", stdout); synapse_json_string(stdout, document->title);
        printf(",\"sourceSha256\":\"%s\",\"sourceBytes\":%zu,\"blocks\":%zu,\"warnings\":%zu,\"blocksByKind\":{",
               document->source_sha256, document->source_bytes, document->block_count,
               document->warning_count);
        for (int i = SD_BLOCK_HEADING; i <= SD_BLOCK_WARNING; i++) {
            if (i) fputc(',', stdout);
            synapse_json_string(stdout, sd_block_kind_name((sd_block_kind)i));
            printf(":%zu", kinds[i]);
        }
        fputs("}}\n", stdout);
    } else {
        printf("%s\n%s: %zu bytes, %zu blocks, %zu warnings\n", document->title,
               sd_format_name(document->format), document->source_bytes,
               document->block_count, document->warning_count);
    }
}

static int command_inspect(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *path = argv[2], *input = "auto";
    output_format format = OUTPUT_TEXT;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
        else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            if (parse_format(argv[++i], &format) != 0) return 2;
        } else return 2;
    }
    sd_document document; sd_document_init(&document);
    if (load_document(path, input, &document) != 0) { sd_document_free(&document); return 1; }
    inspect_output(&document, format);
    sd_document_free(&document); return 0;
}

static int command_view(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *path = argv[2], *input = "auto";
    output_format format = OUTPUT_TEXT;
    color_mode colors = COLOR_AUTO;
    size_t width = 88;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
        else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            if (parse_format(argv[++i], &format) != 0) return 2;
        } else if (strcmp(argv[i], "--color") == 0 && i + 1 < argc) {
            if (parse_color(argv[++i], &colors) != 0) return 2;
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            char *end = NULL; unsigned long parsed = strtoul(argv[++i], &end, 10);
            if (!end || *end || parsed < 20 || parsed > 240) return 2;
            width = parsed;
        } else return 2;
    }
    sd_document document; sd_document_init(&document);
    if (load_document(path, input, &document) != 0) { sd_document_free(&document); return 1; }
    if (format == OUTPUT_JSON) {
        size_t size = 0; char *json = sd_document_to_json(&document, &size);
        if (!json) { sd_document_free(&document); return 1; }
        fwrite(json, 1, size, stdout); fputc('\n', stdout); free(json);
    } else {
        int use_colors = colors == COLOR_ALWAYS || (colors == COLOR_AUTO && isatty(STDOUT_FILENO)
                                                     && !getenv("NO_COLOR"));
        char error[256] = {0}; size_t size = 0;
        char *rendered = sd_render_terminal(&document, use_colors, width, &size,
                                            error, sizeof(error));
        if (!rendered) { fprintf(stderr, "synapse-doc: %s\n", error); sd_document_free(&document); return 1; }
        fwrite(rendered, 1, size, stdout); free(rendered);
    }
    sd_document_free(&document); return 0;
}

static int command_present(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *path = argv[2], *input = "auto";
    output_format format = OUTPUT_TEXT;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
        else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            if (parse_format(argv[++i], &format) != 0) return 2;
        } else return 2;
    }
    sd_presentation presentation;
    sd_presentation_init(&presentation);
    char error[512] = {0};
    if (sd_presentation_load(path, input, &presentation,
                             error, sizeof(error)) != 0) {
        fprintf(stderr, "synapse-doc: %s\n",
                error[0] ? error : strerror(errno));
        sd_presentation_free(&presentation);
        return 1;
    }
    if (format == OUTPUT_JSON) {
        size_t size = 0;
        char *json = sd_presentation_to_json(&presentation, &size);
        if (!json) {
            fprintf(stderr, "synapse-doc: presentation output exceeds bound\n");
            sd_presentation_free(&presentation);
            return 1;
        }
        fwrite(json, 1, size, stdout);
        fputc('\n', stdout);
        free(json);
    } else {
        printf("Semantic Markdown presentation: %zu blocks, %zu inline runs, "
               "%zu warnings\n", presentation.block_count,
               presentation.run_count, presentation.warning_count);
    }
    sd_presentation_free(&presentation);
    return 0;
}

static int command_links(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *path = argv[2];
    output_format format = OUTPUT_TEXT;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            if (parse_format(argv[++i], &format) != 0) return 2;
        } else return 2;
    }
    sd_link_index index;
    sd_link_index_init(&index);
    char error[512] = {0};
    if (sd_link_index_load(path, &index, error, sizeof(error)) != 0) {
        fprintf(stderr, "synapse-doc: %s\n", error[0] ? error : strerror(errno));
        sd_link_index_free(&index);
        return 1;
    }
    if (format == OUTPUT_JSON) {
        size_t size = 0;
        char *json = sd_link_index_to_json(&index, &size);
        if (!json) {
            sd_link_index_free(&index);
            return 1;
        }
        fwrite(json, 1, size, stdout);
        fputc('\n', stdout);
        free(json);
    } else {
        printf("Markdown link inventory: %zu links, %zu tags, %zu aliases\n",
               index.link_count, index.tag_count, index.alias_count);
    }
    sd_link_index_free(&index);
    return 0;
}

static int command_tui(int argc, char **argv) {
    if (argc < 3) return 2;
    const char *path = argv[2], *input = "auto";
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
        else return 2;
    }
    sd_document document; sd_document_init(&document);
    if (load_document(path, input, &document) != 0) { sd_document_free(&document); return 1; }
    int result = sd_run_tui(&document);
    sd_document_free(&document); return result;
}

static int command_export(int argc, char **argv) {
    if (argc < 7) return 2;
    const char *path = argv[2], *input = "auto", *artifact = NULL, *output = NULL;
    output_format format = OUTPUT_TEXT;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) input = argv[++i];
        else if (strcmp(argv[i], "--artifact") == 0 && i + 1 < argc) artifact = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) output = argv[++i];
        else if (strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            if (parse_format(argv[++i], &format) != 0) return 2;
        } else return 2;
    }
    if (!artifact || !output) return 2;
    sd_document document; sd_document_init(&document);
    if (load_document(path, input, &document) != 0) { sd_document_free(&document); return 1; }
    char error[512] = {0}; size_t size = 0; char *data = NULL;
    if (strcmp(artifact, "text") == 0)
        data = sd_render_terminal(&document, 0, 88, &size, error, sizeof(error));
    else if (strcmp(artifact, "interactive-html") == 0)
        data = sd_render_interactive_html(&document, &size, error, sizeof(error));
    else { fprintf(stderr, "synapse-doc: unsupported artifact\n"); sd_document_free(&document); return 2; }
    if (!data || sd_publish_file(output, data, size, 0644, error, sizeof(error)) != 0) {
        fprintf(stderr, "synapse-doc: %s\n", error[0] ? error : "render failed");
        free(data); sd_document_free(&document); return 1;
    }
    char hash[65];
    if (sd_sha256_hex(data, size, hash) != 0) { free(data); sd_document_free(&document); return 1; }
    free(data);
    if (format == OUTPUT_JSON) {
        fputs("{\"schema\":\"synapse.doc.export/v1\",\"inputFormat\":", stdout);
        synapse_json_string(stdout, sd_format_name(document.format));
        fputs(",\"artifact\":", stdout); synapse_json_string(stdout, artifact);
        printf(",\"bytes\":%zu,\"sha256\":\"%s\",\"warnings\":%zu}\n",
               size, hash, document.warning_count);
    } else printf("%s: %zu bytes, sha256 %s, %zu warnings\n",
                  artifact, size, hash, document.warning_count);
    sd_document_free(&document); return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--version") == 0) { puts("synapse-doc " SD_VERSION); return 0; }
    if (argc == 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) {
        usage(stdout); return 0;
    }
    int result = 2;
    if (argc >= 2 && strcmp(argv[1], "inspect") == 0) result = command_inspect(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "view") == 0) result = command_view(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "present") == 0) result = command_present(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "links") == 0) result = command_links(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "tui") == 0) result = command_tui(argc, argv);
    else if (argc >= 2 && strcmp(argv[1], "export") == 0) result = command_export(argc, argv);
    if (result == 2) usage(stderr);
    return result;
}

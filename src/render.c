// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L

#include "doc_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <synapse/core.h>

static void html_text(FILE *output, const char *text) {
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    while (*cursor) {
        switch (*cursor) {
        case '&': fputs("&amp;", output); break;
        case '<': fputs("&lt;", output); break;
        case '>': fputs("&gt;", output); break;
        case '"': fputs("&quot;", output); break;
        case '\'': fputs("&#39;", output); break;
        default:
            if (*cursor >= 0x20 || *cursor == '\t' || *cursor == '\n') fputc(*cursor, output);
        }
        cursor++;
    }
}

static void terminal_wrapped(FILE *output, const char *text, const char *prefix,
                             size_t width, const char *start, const char *end) {
    if (width < 20) width = 20;
    size_t prefix_size = strlen(prefix);
    size_t available = width > prefix_size + 4 ? width - prefix_size : 16;
    const char *cursor = text;
    int first = 1;
    while (*cursor) {
        while (*cursor == ' ') cursor++;
        size_t remaining = strlen(cursor);
        size_t take = remaining <= available ? remaining : available;
        if (take < remaining) {
            size_t split = take;
            while (split > 0 && cursor[split] != ' ') split--;
            if (split > available / 3) take = split;
            while (take > 0 && ((unsigned char)cursor[take] & 0xc0U) == 0x80U) take--;
            if (!take) take = available;
        }
        fputs(start, output);
        fputs(first ? prefix : "  ", output);
        fwrite(cursor, 1, take, output);
        fputs(end, output);
        fputc('\n', output);
        cursor += take;
        first = 0;
    }
    if (first) { fputs(prefix, output); fputc('\n', output); }
}

char *sd_render_terminal(const sd_document *document, int colors, size_t width,
                         size_t *size_out, char *error, size_t error_size) {
    char *data = NULL; size_t size = 0;
    FILE *output = open_memstream(&data, &size);
    if (!output) return NULL;
    const char *reset = colors ? "\033[0m" : "";
    for (size_t i = 0; i < document->block_count; i++) {
        const sd_block *block = &document->blocks[i];
        switch (block->kind) {
        case SD_BLOCK_HEADING: {
            const char *color = colors ? (block->level <= 1 ? "\033[1;38;5;111m" : "\033[1;38;5;75m") : "";
            terminal_wrapped(output, block->text, block->level <= 1 ? "◆ " : "◇ ", width,
                             color, reset);
            fputc('\n', output);
            break;
        }
        case SD_BLOCK_PARAGRAPH:
            terminal_wrapped(output, block->text, "", width, "", "");
            fputc('\n', output);
            break;
        case SD_BLOCK_LIST_ITEM:
            terminal_wrapped(output, block->text,
                             strcmp(block->info, "ordered") == 0 ? "1. " : "• ", width,
                             colors ? "\033[38;5;252m" : "", reset);
            break;
        case SD_BLOCK_CODE: {
            if (block->info[0]) fprintf(output, "%s[%s]%s\n", colors ? "\033[38;5;109m" : "", block->info, reset);
            const char *cursor = block->text;
            do {
                const char *newline = strchr(cursor, '\n');
                size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
                fputs(colors ? "\033[38;5;150m  │ " : "  | ", output);
                fwrite(cursor, 1, length, output);
                fputs(reset, output); fputc('\n', output);
                if (!newline) break;
                cursor = newline + 1;
            } while (1);
            fputc('\n', output);
            break;
        }
        case SD_BLOCK_QUOTE:
            terminal_wrapped(output, block->text, "│ ", width,
                             colors ? "\033[3;38;5;146m" : "", reset);
            fputc('\n', output);
            break;
        case SD_BLOCK_IMAGE:
            terminal_wrapped(output, block->text, "▣ ", width,
                             colors ? "\033[1;38;5;141m" : "", reset);
            if (block->target[0]) fprintf(output, "  %s%s%s\n\n", colors ? "\033[38;5;244m" : "", block->target, reset);
            break;
        case SD_BLOCK_ADMONITION:
            terminal_wrapped(output, block->text, "! ", width,
                             colors ? "\033[1;38;5;214m" : "", reset);
            fputc('\n', output);
            break;
        case SD_BLOCK_RULE:
            for (size_t j = 0; j < (width < 72 ? width : 72); j++) fputs("─", output);
            fputc('\n', output); break;
        case SD_BLOCK_WARNING:
            terminal_wrapped(output, block->text, "⚠ ", width,
                             colors ? "\033[1;38;5;203m" : "", reset);
            break;
        }
    }
    if (fclose(output) != 0 || size > SD_OUTPUT_LIMIT) {
        free(data); data = NULL;
        (void)snprintf(error, error_size, "terminal rendering exceeds output limit");
    }
    if (data && size_out) *size_out = size;
    return data;
}

char *sd_render_interactive_html(const sd_document *document, size_t *size_out,
                                 char *error, size_t error_size) {
    char *data = NULL; size_t size = 0;
    FILE *output = open_memstream(&data, &size);
    if (!output) return NULL;
    fputs("<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">", output);
    fputs("<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; img-src data:; connect-src 'none'; object-src 'none'; base-uri 'none'; form-action 'none'\"><title>", output);
    html_text(output, document->title);
    fputs("</title><style>:root{color-scheme:dark;font-family:'Noto Sans',system-ui,sans-serif}*{box-sizing:border-box}body{margin:0;background:#0b1120;color:#dbeafe}header{position:sticky;top:0;z-index:3;display:flex;gap:12px;align-items:center;padding:12px 20px;background:#111827;border-bottom:1px solid #334155}header strong{white-space:nowrap}input,button{background:#172033;color:#e2e8f0;border:1px solid #475569;border-radius:8px;padding:8px 10px}input{width:min(420px,45vw)}main{display:grid;grid-template-columns:250px minmax(0,820px);gap:36px;justify-content:center;padding:30px 24px}nav{position:sticky;top:86px;align-self:start;max-height:calc(100vh - 110px);overflow:auto}nav a{display:block;color:#93c5fd;text-decoration:none;padding:5px 2px}article{min-width:0}h1,h2,h3,h4,h5,h6{color:#bfdbfe;scroll-margin-top:80px}p,li,blockquote{line-height:1.65}pre{overflow:auto;background:#111827;border:1px solid #334155;border-radius:10px;padding:16px;color:#bbf7d0}blockquote{margin-left:0;border-left:4px solid #818cf8;padding:8px 16px;color:#c4b5fd}.image{border:1px solid #475569;border-radius:10px;padding:18px;color:#d8b4fe}.admonition,.warning{border-radius:10px;padding:12px 16px;margin:14px 0}.admonition{background:#422006;color:#fde68a}.warning{background:#450a0a;color:#fecaca}.dim{display:none}code{color:#bbf7d0}@media(max-width:760px){main{display:block}nav{position:static;max-height:180px;margin-bottom:24px}}</style></head><body>", output);
    fputs("<header><strong>", output); html_text(output, document->title);
    fputs("</strong><input id=\"search\" type=\"search\" placeholder=\"Search\"><button id=\"clear\">Clear</button><span id=\"count\"></span></header><main><nav aria-label=\"Table of contents\"><strong id=\"tocTitle\">Contents</strong>", output);
    size_t heading_index = 0;
    for (size_t i = 0; i < document->block_count; i++) {
        const sd_block *block = &document->blocks[i];
        if (block->kind == SD_BLOCK_HEADING) {
            fprintf(output, "<a href=\"#section-%zu\" style=\"padding-left:%dem\">", heading_index,
                    block->level > 1 ? block->level - 1 : 0);
            html_text(output, block->text); fputs("</a>", output); heading_index++;
        }
    }
    fputs("</nav><article id=\"document\">", output);
    heading_index = 0;
    for (size_t i = 0; i < document->block_count; i++) {
        const sd_block *block = &document->blocks[i];
        switch (block->kind) {
        case SD_BLOCK_HEADING: {
            int level = block->level; if (level < 1) level = 1; if (level > 6) level = 6;
            fprintf(output, "<h%d id=\"section-%zu\" data-search=\"1\">", level, heading_index++);
            html_text(output, block->text); fprintf(output, "</h%d>", level); break;
        }
        case SD_BLOCK_PARAGRAPH:
            fputs("<p data-search=\"1\">", output); html_text(output, block->text); fputs("</p>", output); break;
        case SD_BLOCK_LIST_ITEM:
            fputs("<p data-search=\"1\">• ", output); html_text(output, block->text); fputs("</p>", output); break;
        case SD_BLOCK_CODE:
            fputs("<pre data-search=\"1\"><code>", output); html_text(output, block->text); fputs("</code></pre>", output); break;
        case SD_BLOCK_QUOTE:
            fputs("<blockquote data-search=\"1\">", output); html_text(output, block->text); fputs("</blockquote>", output); break;
        case SD_BLOCK_IMAGE:
            fputs("<section class=\"image\" data-search=\"1\"><strong>▣ ", output); html_text(output, block->text);
            fputs("</strong><br><code>", output); html_text(output, block->target); fputs("</code></section>", output); break;
        case SD_BLOCK_ADMONITION:
            fputs("<aside class=\"admonition\" data-search=\"1\"><strong>", output); html_text(output, block->info);
            fputs("</strong> ", output); html_text(output, block->text); fputs("</aside>", output); break;
        case SD_BLOCK_RULE: fputs("<hr>", output); break;
        case SD_BLOCK_WARNING:
            fputs("<aside class=\"warning\" data-search=\"1\"><strong>⚠</strong> ", output); html_text(output, block->text); fputs("</aside>", output); break;
        }
    }
    fputs("</article></main><script>'use strict';const M={\"en-US\":{search:'Search',clear:'Clear',contents:'Contents',shown:'blocks'},\"it-IT\":{search:'Cerca',clear:'Pulisci',contents:'Indice',shown:'blocchi'}};const L=(navigator.language||'en-US').toLowerCase().startsWith('it')?'it-IT':'en-US',T=M[L],q=document.getElementById('search'),items=[...document.querySelectorAll('[data-search]')],count=document.getElementById('count');document.documentElement.lang=L;q.placeholder=T.search;document.getElementById('clear').textContent=T.clear;document.getElementById('tocTitle').textContent=T.contents;function apply(){const value=q.value.trim().toLowerCase();let shown=0;for(const item of items){const yes=!value||item.textContent.toLowerCase().includes(value);item.classList.toggle('dim',!yes);if(yes)shown++}count.textContent=`${shown}/${items.length} ${T.shown}`}q.addEventListener('input',apply);document.getElementById('clear').addEventListener('click',()=>{q.value='';apply();q.focus()});apply();</script></body></html>\n", output);
    if (fclose(output) != 0 || size > SD_OUTPUT_LIMIT) {
        free(data); data = NULL;
        (void)snprintf(error, error_size, "interactive HTML exceeds output limit");
    }
    if (data && size_out) *size_out = size;
    return data;
}

static int parent_directory(const char *path, char *directory, size_t size) {
    const char *slash = strrchr(path, '/');
    if (!slash) { if (size < 2) return -1; strcpy(directory, "."); return 0; }
    size_t length = slash == path ? 1U : (size_t)(slash - path);
    if (length >= size) return -1;
    memcpy(directory, path, length); directory[length] = '\0';
    return 0;
}

int sd_publish_file(const char *path, const void *data, size_t size, unsigned mode,
                    char *error, size_t error_size) {
    if (!path || !*path || size > SD_OUTPUT_LIMIT) { (void)snprintf(error, error_size, "invalid output"); return -1; }
    struct stat existing;
    if (lstat(path, &existing) == 0 || errno != ENOENT) { (void)snprintf(error, error_size, "output already exists"); return -1; }
    char directory[SYNAPSE_PATH_MAX];
    if (parent_directory(path, directory, sizeof(directory)) != 0
        || synapse_mkdir_p(directory, 0755) != 0) { (void)snprintf(error, error_size, "cannot create output directory"); return -1; }
    char temporary[SYNAPSE_PATH_MAX];
    int length = snprintf(temporary, sizeof(temporary), "%s/.synapse-doc-%ld-XXXXXX", directory, (long)getpid());
    if (length < 0 || (size_t)length >= sizeof(temporary)) return -1;
    int descriptor = mkstemp(temporary), result = -1;
    if (descriptor < 0) return -1;
    if (fchmod(descriptor, (mode_t)mode) != 0) goto done;
    const unsigned char *cursor = data; size_t remaining = size;
    while (remaining) {
        ssize_t written = write(descriptor, cursor, remaining);
        if (written < 0) { if (errno == EINTR) continue; goto done; }
        cursor += (size_t)written; remaining -= (size_t)written;
    }
    if (fsync(descriptor) != 0 || close(descriptor) != 0) { descriptor = -1; goto done; }
    descriptor = -1;
    if (link(temporary, path) != 0 || unlink(temporary) != 0) { unlink(path); goto done; }
    {
        int directory_fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory_fd >= 0) { (void)fsync(directory_fd); close(directory_fd); }
    }
    result = 0;
done:
    if (descriptor >= 0) close(descriptor);
    if (result != 0) { int saved = errno; unlink(temporary); (void)snprintf(error, error_size, "cannot publish output: %s", strerror(saved)); }
    return result;
}

static void tui_colors(void) {
    if (!has_colors()) return;
    start_color(); use_default_colors();
    init_pair(1, COLOR_CYAN, -1);
    init_pair(2, COLOR_GREEN, -1);
    init_pair(3, COLOR_MAGENTA, -1);
    init_pair(4, COLOR_YELLOW, -1);
    init_pair(5, COLOR_RED, -1);
}

int sd_run_tui(const sd_document *document) {
    setlocale(LC_ALL, "");
    size_t rendered_size = 0; char error[128] = {0};
    char *rendered = sd_render_terminal(document, 0, 100, &rendered_size, error, sizeof(error));
    if (!rendered) return 1;
    size_t line_count = 1;
    for (size_t i = 0; i < rendered_size; i++) if (rendered[i] == '\n') line_count++;
    char **lines = calloc(line_count + 1, sizeof(*lines));
    if (!lines) { free(rendered); return 1; }
    size_t index = 0; lines[index++] = rendered;
    for (size_t i = 0; i < rendered_size; i++) if (rendered[i] == '\n') { rendered[i] = '\0'; lines[index++] = rendered + i + 1; }
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0); tui_colors();
    size_t top = 0; int done = 0;
    while (!done) {
        erase(); int rows, columns; getmaxyx(stdscr, rows, columns);
        attron(A_BOLD | (has_colors() ? COLOR_PAIR(1) : 0));
        mvaddnstr(0, 1, document->title, columns > 2 ? columns - 2 : 0);
        attroff(A_BOLD | (has_colors() ? COLOR_PAIR(1) : 0));
        int body_rows = rows > 3 ? rows - 3 : 0;
        for (int row = 0; row < body_rows && top + (size_t)row < line_count; row++) {
            const char *line = lines[top + (size_t)row];
            int pair = 0;
            if (strncmp(line, "◆", strlen("◆")) == 0 || strncmp(line, "◇", strlen("◇")) == 0) pair = 1;
            else if (strncmp(line, "  |", 3) == 0 || strncmp(line, "  │", strlen("  │")) == 0) pair = 2;
            else if (strncmp(line, "▣", strlen("▣")) == 0) pair = 3;
            else if (line[0] == '!' || strncmp(line, "⚠", strlen("⚠")) == 0) pair = 4;
            if (pair && has_colors()) attron(COLOR_PAIR(pair));
            mvaddnstr(row + 1, 1, line, columns > 2 ? columns - 2 : 0);
            if (pair && has_colors()) attroff(COLOR_PAIR(pair));
        }
        if (rows > 1) {
            attron(A_REVERSE);
            char status[256];
            (void)snprintf(status, sizeof(status), " %s · %s · ↑↓ PgUp/PgDn · q ",
                           sd_format_name(document->format), document->warning_count ? "warnings" : "safe");
            mvaddnstr(rows - 1, 0, status, columns);
            for (int x = (int)strlen(status); x < columns; x++) mvaddch(rows - 1, x, ' ');
            attroff(A_REVERSE);
        }
        refresh();
        int key = getch();
        size_t page = body_rows > 2 ? (size_t)body_rows - 2 : 1;
        if (key == 'q' || key == 27) done = 1;
        else if ((key == KEY_DOWN || key == 'j') && top + 1 < line_count) top++;
        else if ((key == KEY_UP || key == 'k') && top > 0) top--;
        else if (key == KEY_NPAGE) top = top + page < line_count ? top + page : line_count - 1;
        else if (key == KEY_PPAGE) top = top > page ? top - page : 0;
        else if (key == KEY_HOME || key == 'g') top = 0;
        else if (key == KEY_END || key == 'G') top = line_count ? line_count - 1 : 0;
    }
    endwin(); free(lines); free(rendered); return 0;
}

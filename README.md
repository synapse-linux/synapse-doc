# Synapse Doc

Console-first document reader for Markdown, AsciiDoc and reStructuredText.
Synapse Doc parses bounded UTF-8 documents into a typed AST and presents them as
plain/ANSI terminal text, an ncurses reader, JSON, or a self-contained offline
interactive HTML artifact.

```bash
synapse-doc inspect README.md --format json
synapse-doc view README.adoc
synapse-doc tui architecture.rst
synapse-doc export README.md --artifact interactive-html --output README.html
```

The TUI supports scrolling, paging and Home/End without Wayland. The HTML export
provides a table of contents, localized search controls and responsive layout.
All document strings are escaped; model content never becomes raw HTML or
JavaScript.

## Alpha 1 reader profiles

Implemented common safe structures:

- Markdown headings, paragraphs, lists, quotes, fenced code, rules and images;
- AsciiDoc headings, paragraphs, lists, source blocks, admonitions and images;
- reStructuredText underlined headings, paragraphs, lists, code directives,
  admonitions, quotes and images.

Security-sensitive features are intentionally disabled and reported as warning
blocks: raw HTML/passthrough, includes, arbitrary directives/extensions and
remote image loading. Images currently render as a semantic placeholder with alt
text and target. Bounded local Sixel rendering is planned separately.

This alpha does not claim full CommonMark/GFM, Asciidoctor or Docutils semantic
compatibility. Maintained canonical adapters or deeper native parsers remain a
separate compatibility phase.

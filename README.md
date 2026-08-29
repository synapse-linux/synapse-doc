# Synapse Doc

Console-first document reader for Markdown, AsciiDoc and reStructuredText.
Synapse Doc parses bounded UTF-8 documents into a typed AST and presents them as
plain/ANSI terminal text, an ncurses reader, JSON, or a self-contained offline
interactive HTML artifact.

```bash
synapse-doc inspect README.md --format json
synapse-doc links notes/home.md --format json
synapse-doc present notes/home.md --format json
synapse-doc view README.adoc
synapse-doc tui architecture.rst
synapse-doc export README.md --artifact interactive-html --output README.html
```

The TUI supports scrolling, paging and Home/End without Wayland. The HTML export
provides a table of contents, localized search controls and responsive layout.
All document strings are escaped; model content never becomes raw HTML or
JavaScript.

The Markdown-only `links` command extracts a bounded source-ranged knowledge
inventory without changing the reader AST. It recognizes frontmatter title,
aliases and tags; inline tags; standard Markdown links and images; and
Obsidian-compatible wikilinks, embeds, heading references and block references.
Fenced and inline code are inert. Complex relevant YAML values, incomplete
fences and incomplete wikilinks fail explicitly rather than producing an
apparently complete index. A truncated ordinary Markdown link or image is inert
text because it cannot create a resolved knowledge edge.

The Markdown-only `present` command produces a separate bounded semantic
presentation contract. Leading frontmatter is excluded from visual blocks and a
simple title is exposed as metadata. Body blocks retain half-open source ranges
and contain source-ranged inline runs for text, emphasis, strong text, inline
code, Markdown links, wikilinks, embeds, images and tags. Fenced and inline code
remain opaque. Unsupported or incomplete inline syntax is inert text so an
in-progress Editor preview does not fail merely because a closing marker has not
yet been typed. Raw HTML is always text; links and media are identities only and
never trigger resource loading.

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

This alpha does not claim full CommonMark/GFM, Obsidian, YAML, Asciidoctor or
Docutils semantic compatibility. Nested inline emphasis, tables, task lists and
arbitrary YAML values remain outside the semantic presentation profile.
Knowledge-link inventory and semantic presentation are explicit safe profiles,
not claims of complete Obsidian vault resolution. Maintained canonical adapters
or deeper native parsers remain a separate compatibility phase.

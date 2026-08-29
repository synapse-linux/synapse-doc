# Changelog

## Unreleased — 0.1.0-alpha.2

- Add bounded Markdown semantic presentation as
  `synapse.doc.presentation/v1` without changing `synapse.doc.ast/v1`.
- Separate leading YAML frontmatter from visual body blocks and expose only a
  bounded simple title in the presentation metadata.
- Add source-ranged text, emphasis, strong, inline-code, Markdown-link,
  wikilink, embed, image and tag runs while preserving unsupported syntax as
  inert text.
- Keep fenced code and inline code opaque, render raw HTML only as text, and
  emit explicit generated warnings for raw HTML and remote media.

## 0.1.0-alpha.1

- Add bounded native reader profiles for Markdown, AsciiDoc and
  reStructuredText.
- Add versioned document AST and inspect/export receipts.
- Add bounded Markdown wikilink, frontmatter, tag and source-range inventory as
  `synapse.doc.links/v1` without changing the reader AST.
- Add ANSI/plain rendering and an ncurses reader.
- Add self-contained offline interactive HTML with table of contents and search.
- Disable and expose raw, include, extension and remote-resource constructs.

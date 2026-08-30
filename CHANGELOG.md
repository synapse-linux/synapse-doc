# Changelog

## Unreleased — 0.1.0-alpha.3

- Add read-only `synapse.doc.link-rewrite-plan/v1` and
  `synapse.doc.link-rewrite/v1` contracts that revalidate source-ranged links and
  emit exact target preimages/replacements without changing `links/v1`.
- Preserve labels, fragments, Markdown titles, angle enclosure and unrelated
  source bytes while rejecting unrepresentable or external replacement targets.
- Parse angle-enclosed Markdown destinations followed by optional titles.

## 0.1.0-alpha.2

- Re-license the first-party parser, renderer and source metadata under the MIT License.
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

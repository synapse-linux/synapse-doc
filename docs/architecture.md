# Architecture

```text
Markdown    AsciiDoc    reStructuredText
    │           │              │
    └──── bounded format adapters ────┐
                                      ▼
                           synapse.doc.ast/v1
                         ┌────────┼──────────┐
                         ▼        ▼          ▼
                    terminal   ncurses   offline HTML
```

Input adapters preserve format identity while normalizing safe document blocks.
Unknown or intentionally disabled constructs are visible warnings. The AST does
not contain executable callbacks, raw HTML or fetched resources.

Markdown knowledge extraction is a sibling read-only projection rather than an
extension of `synapse.doc.ast/v1`:

```text
bounded Markdown source
          │
          ├── block reader ──────► synapse.doc.ast/v1
          └── link inventory ────► synapse.doc.links/v1
```

The link inventory retains byte ranges and typed destinations for wikilinks,
embeds, Markdown links, frontmatter aliases and tags. It ignores code and does
not resolve another file, traverse a vault, or execute `.obsidian` content.
Vault identity and ambiguity resolution belong to a future knowledge indexer.

The terminal and TUI paths have no graphical dependency. Interactive HTML is a
derived viewer containing fixed first-party presentation code and escaped model
text. It cannot alter source documents.

Synapse Files integration should delegate owned document MIME types to this
executable through direct argv. Files remains the navigator; Synapse Doc owns
parsing and document presentation. Other consumers should initially use the
exact-major JSON contracts; an in-process parsing ABI is deferred until live
editor latency demonstrates that it is necessary.

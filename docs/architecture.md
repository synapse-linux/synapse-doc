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

Markdown knowledge extraction and semantic presentation are sibling read-only
projections rather than extensions of `synapse.doc.ast/v1`:

```text
bounded Markdown source
          │
          ├── block reader ─────────► synapse.doc.ast/v1
          ├── link inventory ───────► synapse.doc.links/v1
          ├── link rewrite planner ─► synapse.doc.link-rewrite/v1
          └── semantic presentation ► synapse.doc.presentation/v1
```

The link inventory retains byte ranges and typed destinations for wikilinks,
embeds, Markdown links, frontmatter aliases and tags. It ignores code and does
not resolve another file, traverse a vault, or execute `.obsidian` content.
Vault identity and ambiguity resolution belong to Synapse Knowledge.

The rewrite planner consumes ranges from that unchanged link inventory and
revalidates them against the same source hash, parsed kind and semantic fields.
It exposes only the smallest exact target preimage/replacement range needed for
an authorized rename. It never writes a file or decides vault-relative target
spelling: Knowledge owns identity/relative-path policy and Editor owns the
multi-file CAS transaction.

Semantic presentation excludes frontmatter from body blocks from body blocks and retains bounded
source ranges for visual blocks and inline roles. It is intentionally tolerant
of incomplete inline markup so unsaved Editor bytes remain previewable. Code is
opaque, raw HTML remains text and remote media is never loaded. Presentation
clients style the typed roles but do not reinterpret Markdown or source text.

The terminal and TUI paths have no graphical dependency. Interactive HTML is a
derived viewer containing fixed first-party presentation code and escaped model
text. It cannot alter source documents.

Synapse Files integration should delegate owned document MIME types to this
executable through direct argv. Files remains the navigator; Synapse Doc owns
parsing and document presentation. Other consumers should initially use the
exact-major JSON contracts; an in-process parsing ABI is deferred until live
editor latency demonstrates that it is necessary.

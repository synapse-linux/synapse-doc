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

The terminal and TUI paths have no graphical dependency. Interactive HTML is a
derived viewer containing fixed first-party presentation code and escaped model
text. It cannot alter source documents.

Synapse Files integration should delegate owned document MIME types to this
executable through direct argv. Files remains the navigator; Synapse Doc owns
parsing and document presentation.

# Synapse Doc agent contract

- The authoritative implementation is a C17 CLI/TUI built against released
  `libsynapse-core`; it must remain useful on a TTY, over SSH and in Recovery.
- Markdown, AsciiDoc and reStructuredText are separate input profiles normalized
  into exact-major `synapse.doc.ast/v1`. Markdown semantic presentation is a
  sibling source-ranged contract and must not silently change the reader AST.
  Unsupported constructs remain explicit; never silently claim complete language
  compatibility.
- Raw HTML, passthrough, extensions, remote resources, executable directives and
  unconstrained includes are disabled. Future local includes require a bounded
  root and cycle/depth guards.
- Render model strings only as escaped text. Never pass source prose, paths,
  roles, attributes or directives to a shell.
- Interactive HTML is offline, self-contained and derived. It performs no
  network requests and must not execute document-provided code.
- Images are typed document nodes. Alpha fallbacks display alt text and target;
  future Sixel decoding remains bounded and local-only.
- `synapse-chart` owns chart semantics. Synapse Doc may reference verified chart
  artifacts but must not duplicate chart parsing or layout.
- Bound input bytes, UTF-8, line/block counts, block text, output bytes, parser
  depth and subprocesses. Atomically publish generated artifacts without
  overwrite.
- Every command has useful text and versioned JSON. User-facing prose is prepared
  for stable message IDs and locale catalogs; machine keys remain neutral.
- Pass strict GCC/Clang, ASan/UBSan, analyzer, hostile syntax/path tests, PTY,
  reproducibility, hardening and `x86-64-baseline` gates before packaging.

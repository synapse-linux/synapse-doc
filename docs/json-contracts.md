# JSON contracts

## `synapse.doc.inspect/v1`

Bounded read-only summary containing input format, title, source hash/bytes,
block count, warning count and counts by block kind.

## `synapse.doc.ast/v1`

Typed normalized document returned by `view --format json`. Each block has exact
keys `kind`, `level`, `text`, `target` and `info`. Supported Alpha 1 kinds are
heading, paragraph, list-item, code, quote, image, admonition, rule and warning.
Unknown major versions and kinds fail closed for future consumers.

## `synapse.doc.links/v1`

Markdown-only, source-ranged inventory for downstream knowledge indexing. It
contains the immutable source hash and size, bounded frontmatter title and
aliases, typed tags with `frontmatter` or `inline` provenance, and typed links.
Link kinds are `wikilink`, `embed`, `markdown` and `image`; every link exposes
its note target, optional heading or block target, display label, external flag
and half-open byte range. The scanner does not resolve paths or read another
file. Fenced code, inline code and escaped wikilinks are inert. Unsupported
complex values and incomplete relevant syntax fail explicitly.

## `synapse.doc.export/v1`

Receipt for an atomically published text or interactive-HTML artifact. Includes
input format, artifact, bytes, SHA-256 and warnings without echoing the path.

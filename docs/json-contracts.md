# JSON contracts

## `synapse.doc.inspect/v1`

Bounded read-only summary containing input format, title, source hash/bytes,
block count, warning count and counts by block kind.

## `synapse.doc.ast/v1`

Typed normalized document returned by `view --format json`. Each block has exact
keys `kind`, `level`, `text`, `target` and `info`. Supported Alpha 1 kinds are
heading, paragraph, list-item, code, quote, image, admonition, rule and warning.
Unknown major versions and kinds fail closed for future consumers.

## `synapse.doc.export/v1`

Receipt for an atomically published text or interactive-HTML artifact. Includes
input format, artifact, bytes, SHA-256 and warnings without echoing the path.

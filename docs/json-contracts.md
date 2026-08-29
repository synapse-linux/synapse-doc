# JSON contracts

## `synapse.doc.inspect/v1`

Bounded read-only summary containing input format, title, source hash/bytes,
block count, warning count and counts by block kind.

## `synapse.doc.ast/v1`

Typed normalized document returned by `view --format json`. Each block has exact
keys `kind`, `level`, `text`, `target` and `info`. Supported Alpha 1 kinds are
heading, paragraph, list-item, code, quote, image, admonition, rule and warning.
Unknown major versions and kinds fail closed for future consumers.

## `synapse.doc.presentation/v1`

Markdown-only semantic presentation returned by `present --format json`. The
exact root contains `schema`, `format`, `title`, source SHA-256/bytes, bounded
frontmatter metadata, warning count and ordered blocks. Frontmatter exposes only
`present`, its half-open source range and a bounded simple `title`; frontmatter
bytes never become visual body blocks.

Each block has exact keys `kind`, `level`, `generated`, `text`, `target`, `info`,
`startByte`, `endByte`, `textStartByte`, `textEndByte` and `runs`. Non-generated
blocks retain ranges into the immutable source. Generated security/parser
warnings use an empty range at the related source boundary. Runs use exact keys
`kind`, `text`, `target`, `heading`, `block`, `external`, `startByte`, `endByte`,
`textStartByte` and `textEndByte`. Supported run kinds are `text`, `emphasis`,
`strong`, `code`, `link`, `wikilink`, `embed`, `image` and `tag`.

Run text is the exact UTF-8 source payload inside its text range; presentation
clients may style it but must not interpret it as HTML or executable input.
Inline and fenced code are opaque, incomplete inline markup is inert text, and
remote media produces a generated warning instead of a load request. The
contract is bounded to 32,768 blocks, 262,144 runs, 8 MiB input and 32 MiB JSON.
It does not alter or supersede `synapse.doc.ast/v1`.

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

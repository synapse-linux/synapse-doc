#!/usr/bin/env bash
set -euo pipefail

binary=${1:?binary path required}
project=$(cd "$(dirname "$0")/.." && pwd)
root=$(mktemp -d)
trap 'rm -rf "$root"' EXIT
fixtures=$(cd "$(dirname "$0")/fixtures" && pwd)

[[ $($binary --version) == 'synapse-doc 0.1.0-alpha.1' ]]
$binary --help | grep -Fq 'Markdown, AsciiDoc and reStructuredText'
if command -v desktop-file-validate >/dev/null; then desktop-file-validate "$project/data/org.synapse.Doc.desktop"; fi

for spec in 'sample.md:markdown:Synapse Markdown' 'sample.adoc:asciidoc:Synapse AsciiDoc' 'sample.rst:rst:Synapse reStructuredText'; do
  file=${spec%%:*}; rest=${spec#*:}; format=${rest%%:*}; title=${rest#*:}
  result=$($binary inspect "$fixtures/$file" --format json)
  printf '%s' "$result" | python -c '
import json,sys
expected_format,expected_title=sys.argv[1:]
j=json.load(sys.stdin)
assert j["schema"]=="synapse.doc.inspect/v1"
assert j["format"]==expected_format and j["title"]==expected_title
assert j["blocks"]>=7 and j["warnings"]>=1
assert j["blocksByKind"]["heading"]>=2
assert j["blocksByKind"]["code"]==1
assert j["blocksByKind"]["image"]==1
' "$format" "$title"
  $binary view "$fixtures/$file" --format json | python -c '
import json,sys
j=json.load(sys.stdin)
assert j["schema"]=="synapse.doc.ast/v1" and j["blocks"]
assert all(set(x)=={"kind","level","text","target","info"} for x in j["blocks"])
'
  $binary view "$fixtures/$file" --color never --width 60 >"$root/$format.txt"
  grep -Fq "$title" "$root/$format.txt"
  grep -Fiq 'architecture' "$root/$format.txt"
  if grep -q $'\033' "$root/$format.txt"; then echo 'color leaked into never mode' >&2; exit 1; fi
  NO_COLOR=1 $binary view "$fixtures/$file" >"$root/$format-nocolor.txt"
  if grep -q $'\033' "$root/$format-nocolor.txt"; then echo 'NO_COLOR ignored' >&2; exit 1; fi
  $binary view "$fixtures/$file" --color always >"$root/$format-color.txt"
  grep -q $'\033\[' "$root/$format-color.txt"
done

# Markdown knowledge-link extraction is source-ranged, bounded and code-aware.
$binary links "$fixtures/knowledge.md" --format json >"$root/links.json"
$binary links "$fixtures/knowledge.md" >"$root/links.txt"
grep -Fxq 'Markdown link inventory: 9 links, 5 tags, 2 aliases' "$root/links.txt"
python - "$fixtures/knowledge.md" "$root/links.json" <<'PY'
import hashlib,json,sys
source_path,result_path=sys.argv[1:]
data=open(source_path,'rb').read()
j=json.load(open(result_path,encoding='utf-8'))
assert set(j)=={'schema','format','sourceSha256','sourceBytes','frontmatter','tags','links'}
assert j['schema']=='synapse.doc.links/v1' and j['format']=='markdown'
assert j['sourceSha256']==hashlib.sha256(data).hexdigest()
assert j['sourceBytes']==len(data)
assert j['frontmatter']['present'] is True
assert j['frontmatter']['title']=='Knowledge Home'
assert [x['value'] for x in j['frontmatter']['aliases']]==['Home','Start Here']
assert len(j['links'])==9
assert [(x['kind'],x['target'],x['heading'],x['block']) for x in j['links']]==[
 ('wikilink','Architecture','',''),
 ('wikilink','Folder/Note','',''),
 ('wikilink','Architecture','Components',''),
 ('wikilink','Architecture','','block-7'),
 ('wikilink','','Local heading',''),
 ('embed','assets/diagram.svg','',''),
 ('markdown','docs/guide.md','Install',''),
 ('markdown','https://example.com/path','anchor',''),
 ('image','assets/image.png','',''),
]
assert j['links'][1]['label']=='Displayed note'
assert j['links'][5]['label']=='Diagram'
assert j['links'][7]['external'] is True
assert all(not x['external'] for i,x in enumerate(j['links']) if i != 7)
assert [(x['value'],x['source']) for x in j['tags']]==[
 ('knowledge','frontmatter'),('synapse/wiki','frontmatter'),
 ('inline-tag','inline'),('nested/tag','inline'),('c17','inline')]
for item in j['links']:
    span=data[item['startByte']:item['endByte']]
    assert span.startswith((b'[[',b'![[',b'[',b'!['))
for item in j['frontmatter']['aliases']+j['tags']:
    span=data[item['startByte']:item['endByte']].decode('utf-8')
    assert span.lstrip('#')==item['value']
assert b'Fenced code' not in open(result_path,'rb').read()
assert b'Inline code' not in open(result_path,'rb').read()
assert b'Not a link' not in open(result_path,'rb').read()
PY
LC_ALL=C $binary links "$fixtures/knowledge.md" --format json >"$root/links-c.json"
LC_ALL=it_IT.UTF-8 $binary links "$fixtures/knowledge.md" --format json >"$root/links-it.json"
cmp "$root/links-c.json" "$root/links-it.json"

printf '%s\n' '` unmatched [[literal-after-backtick]] and [empty]()' >"$root/unmatched-inline.md"
$binary links "$root/unmatched-inline.md" --format json | python -c '
import json,sys
j=json.load(sys.stdin)
assert [(x["kind"],x["target"]) for x in j["links"]]==[("wikilink","literal-after-backtick"),("markdown","")]
'
printf '%s\n' '---' 'nested:' '  aliases: not-top-level' '---' '[[ok]]' >"$root/nested-frontmatter.md"
$binary links "$root/nested-frontmatter.md" --format json | python -c '
import json,sys
j=json.load(sys.stdin)
assert j["frontmatter"]["aliases"]==[] and len(j["links"])==1
'

printf '%s\n' '[[unclosed' >"$root/unclosed-link.md"
printf '%s\n' '```' '[[hidden]]' >"$root/unclosed-fence.md"
printf '%s\n' '---' 'aliases: {bad: value}' '---' 'body' >"$root/complex-frontmatter.md"
for bad in unclosed-link.md unclosed-fence.md complex-frontmatter.md; do
  if $binary links "$root/$bad" --format json >/dev/null 2>&1; then
    echo "accepted incomplete knowledge syntax $bad" >&2; exit 1
  fi
done
export SOURCE_DATE_EPOCH=1787732185
$binary export "$fixtures/sample.md" --artifact interactive-html --output "$root/a.html" --format json >"$root/export.json"
$binary export "$fixtures/sample.md" --artifact interactive-html --output "$root/b.html" >/dev/null
cmp "$root/a.html" "$root/b.html"
$binary export "$fixtures/sample.adoc" --artifact text --output "$root/document.txt" --format json >/dev/null
python - "$root/a.html" "$root/export.json" <<'PY'
import hashlib,json,sys
html,receipt=sys.argv[1:]
h=open(html,encoding='utf-8').read()
assert h.startswith('<!doctype html>')
assert 'id="search"' in h and 'Table of contents' in h
assert '<img src=x onerror=alert(1)>' not in h
assert '&lt;img src=x onerror=alert(1)&gt;' in h
assert '<script src=' not in h and 'https://' not in h
assert 'fetch(' not in h and 'XMLHttpRequest' not in h and 'eval(' not in h
j=json.load(open(receipt)); data=open(html,'rb').read()
assert j['schema']=='synapse.doc.export/v1'
assert j['inputFormat']=='markdown' and j['artifact']=='interactive-html'
assert j['sha256']==hashlib.sha256(data).hexdigest()
PY
if command -v node >/dev/null; then
  python - "$root/a.html" "$root/document.js" <<'PY'
import sys
text=open(sys.argv[1],encoding='utf-8').read()
script=text.split('<script>',1)[1].split('</script>',1)[0]
open(sys.argv[2],'w',encoding='utf-8').write(script)
PY
  node --check "$root/document.js" >/dev/null
fi

# TUI starts and exits without a GUI or Wayland.
if command -v script >/dev/null; then
  printf q | TERM=xterm-256color script -qfec "stty cols 80 rows 24; '$binary' tui '$fixtures/sample.md'" /dev/null >"$root/tui.log" 2>&1
  grep -aFq 'Synapse Markdown' "$root/tui.log"
fi

# No-overwrite and unsupported behavior fail closed.
if $binary export "$fixtures/sample.md" --artifact text --output "$root/document.txt" >/dev/null 2>&1; then exit 1; fi
if $binary export "$fixtures/sample.md" --artifact pdf --output "$root/no.pdf" >/dev/null 2>&1; then exit 1; fi
if $binary inspect "$fixtures/sample.md" --format yaml >/dev/null 2>&1; then exit 1; fi
cp "$fixtures/sample.md" "$root/no-extension"
if $binary inspect "$root/no-extension" >/dev/null 2>&1; then exit 1; fi
$binary inspect "$root/no-extension" --input markdown --format json >/dev/null
printf '\xff\xfe' >"$root/invalid.md"
printf 'ok\0hidden' >"$root/nul.md"
truncate -s 8388609 "$root/oversize.md"
python - "$root/long-line.md" <<'PY'
import sys
open(sys.argv[1],'w').write('x'*65537+'\n')
PY
ln -s "$fixtures/sample.md" "$root/link.md"
if $binary links "$root/link.md" --format json >/dev/null 2>&1; then
  echo 'links accepted symlink input' >&2; exit 1
fi
for bad in invalid.md nul.md oversize.md long-line.md link.md; do
  if $binary inspect "$root/$bad" >/dev/null 2>&1; then echo "accepted hostile input $bad" >&2; exit 1; fi
done

hostile="$root/document;touch PWNED.md"
cp "$fixtures/sample.md" "$hostile"
$binary inspect "$hostile" --input markdown --format json >/dev/null
[[ ! -e PWNED && ! -e "$root/PWNED" ]]

echo 'synapse-doc tests: PASS'

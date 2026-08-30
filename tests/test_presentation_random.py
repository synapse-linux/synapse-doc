#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Deterministic hostile-source validation for synapse.doc.presentation/v1."""

import hashlib
import json
import pathlib
import random
import subprocess
import sys
import tempfile


def boundary(data: bytes, offset: int) -> bool:
    return offset in (0, len(data)) or data[offset] & 0xC0 != 0x80


def validate(data: bytes, packet: dict) -> None:
    assert set(packet) == {
        "schema", "format", "title", "sourceSha256", "sourceBytes",
        "frontmatter", "warnings", "blocks",
    }
    assert packet["schema"] == "synapse.doc.presentation/v1"
    assert packet["format"] == "markdown"
    assert packet["sourceSha256"] == hashlib.sha256(data).hexdigest()
    assert packet["sourceBytes"] == len(data)
    frontmatter = packet["frontmatter"]
    assert set(frontmatter) == {"present", "startByte", "endByte", "title"}
    assert 0 <= frontmatter["startByte"] <= frontmatter["endByte"] <= len(data)
    assert boundary(data, frontmatter["startByte"])
    assert boundary(data, frontmatter["endByte"])
    if frontmatter["present"]:
        assert data[frontmatter["startByte"]:].startswith(b"---")
        previous = frontmatter["endByte"]
    else:
        assert frontmatter == {
            "present": False, "startByte": 0, "endByte": 0, "title": ""
        }
        previous = 0
    warnings = 0
    run_total = 0
    for block in packet["blocks"]:
        assert set(block) == {
            "kind", "level", "generated", "text", "target", "info",
            "startByte", "endByte", "textStartByte", "textEndByte", "runs",
        }
        assert previous <= block["startByte"] <= block["textStartByte"]
        assert block["textStartByte"] <= block["textEndByte"] <= block["endByte"]
        assert block["endByte"] <= len(data)
        for key in ("startByte", "endByte", "textStartByte", "textEndByte"):
            assert boundary(data, block[key])
        previous = block["endByte"]
        if block["kind"] == "warning":
            warnings += 1
            assert block["generated"] and not block["runs"]
            assert block["startByte"] == block["endByte"]
        elif block["kind"] == "code":
            assert not block["runs"]
            assert block["text"].encode() == data[
                block["textStartByte"]:block["textEndByte"]
            ]
        elif block["kind"] == "rule":
            assert not block["text"] and not block["runs"]
        else:
            assert not block["generated"]
            assert block["text"] == "".join(run["text"] for run in block["runs"])
        run_previous = block["textStartByte"]
        for run in block["runs"]:
            assert set(run) == {
                "kind", "text", "target", "heading", "block", "external",
                "startByte", "endByte", "textStartByte", "textEndByte",
            }
            assert run_previous <= run["startByte"] <= run["textStartByte"]
            assert run["textStartByte"] <= run["textEndByte"] <= run["endByte"]
            assert run["endByte"] <= block["textEndByte"]
            for key in ("startByte", "endByte", "textStartByte", "textEndByte"):
                assert boundary(data, run[key])
            source = data[run["textStartByte"]:run["textEndByte"]].decode("utf-8")
            assert source == run["text"]
            run_previous = run["endByte"]
            run_total += 1
    assert packet["warnings"] == warnings
    assert len(packet["blocks"]) <= 32768 and run_total <= 262144


def source_for(randomizer: random.Random, sequence: int) -> bytes:
    inline = [
        "plain", "🚀", "é", "**bold**", "*emphasis*", "__strong__",
        "_style_", "`code [[inert]]`", "[[Note]]", "[[Note|Label]]",
        "![[assets/image.png|Embed]]", "[Guide](docs/Guide.md#Install)",
        "![Remote](https://example.com/image.png)", "#nested/tag",
        r"\*escaped*", "~~unknown~~", "[[unfinished", "*unfinished",
        "<img src=x onerror=alert(1)>",
    ]
    lines = []
    if sequence % 4 == 0:
        lines.extend([
            "---", f"title: Random {sequence}", "aliases: [One, Two]",
            "unknown: {opaque: true}", "---",
        ])
    count = 1 + randomizer.randrange(18)
    for index in range(count):
        body = " ".join(randomizer.choice(inline) for _ in range(1 + randomizer.randrange(7)))
        kind = randomizer.randrange(7)
        if kind == 0:
            lines.append("# " + body)
        elif kind == 1:
            lines.append("- " + body)
        elif kind == 2:
            lines.append("> " + body)
        elif kind == 3:
            lines.extend(["```text", body, "```"] if index % 3 else ["```text", body])
        elif kind == 4:
            lines.append("***")
        else:
            lines.append(body)
        if randomizer.randrange(3) == 0:
            lines.append("")
    ending = "\r\n" if sequence % 5 == 0 else "\n"
    encoded = ending.join(lines).encode("utf-8") + ending.encode("ascii")
    return (b"\xef\xbb\xbf" + encoded) if sequence % 7 == 0 else encoded


def main() -> int:
    if len(sys.argv) != 2:
        raise SystemExit("binary path required")
    binary = sys.argv[1]
    randomizer = random.Random(0xD0C51A)
    with tempfile.TemporaryDirectory(prefix="synapse-doc-presentation-random-") as root:
        path = pathlib.Path(root, "random.md")
        for sequence in range(300):
            data = source_for(randomizer, sequence)
            path.write_bytes(data)
            result = subprocess.run(
                [binary, "present", str(path), "--format", "json"],
                check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            )
            if result.returncode != 0:
                raise AssertionError(
                    f"sequence {sequence} failed: {result.stderr.decode(errors='replace')}\n"
                    f"{data.decode(errors='replace')}"
                )
            validate(data, json.loads(result.stdout))
    print("synapse.doc.presentation/v1 randomized sources: PASS (300)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

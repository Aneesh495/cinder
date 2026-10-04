#!/usr/bin/env python3
"""Count substantive authored C production lines under explicit rules."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


def strip_comments(text: str) -> str:
    result: list[str] = []
    i = 0
    state = "code"
    while i < len(text):
        ch = text[i]
        nxt = text[i + 1] if i + 1 < len(text) else ""
        if state in {"string", "char"}:
            result.append(ch)
            if ch == "\\" and nxt:
                result.append(nxt)
                i += 2
                continue
            if ch == ('"' if state == "string" else "'"):
                state = "code"
            i += 1
            continue
        if state == "code" and ch in {'"', "'"}:
            state = "string" if ch == '"' else "char"
            result.append(ch)
            i += 1
            continue
        if state == "code" and ch == "/" and nxt == "/":
            state = "line"
            result.append(" ")
            i += 2
            continue
        if state == "code" and ch == "/" and nxt == "*":
            state = "block"
            result.append(" ")
            i += 2
            continue
        if state == "line":
            if ch == "\n":
                state = "code"
                result.append("\n")
            else:
                result.append(" ")
            i += 1
            continue
        if state == "block":
            if ch == "*" and nxt == "/":
                state = "code"
                result.extend((" ", " "))
                i += 2
            else:
                result.append("\n" if ch == "\n" else " ")
                i += 1
            continue
        result.append(ch)
        i += 1
    return "".join(result)


def count_file(path: Path) -> int:
    text = strip_comments(path.read_text(encoding="utf-8"))
    count = 0
    directive = False
    for line in text.splitlines():
        stripped = line.strip()
        if directive or stripped.startswith('#'):
            directive = stripped.endswith('\\')
            continue
        if not stripped or re.fullmatch(r'[{}();,\s]+', stripped):
            continue
        if path.suffix == '.h' and re.fullmatch(r'[^=;{}]*\bcinder_\w+\([^;{}]*\);', stripped):
            continue
        count += 1
    return count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    files = sorted((root / "source").rglob("*.c")) + sorted((root / "source").rglob("*.h"))
    entries = [{"path": str(path.relative_to(root)), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "substantive_lines": count_file(path)} for path in files]
    payload = {
        "schema": 2,
        "rules": {
            "included": "Original source/**/*.c and source/**/*.h after quote-aware comment removal; exclude blank/punctuation-only lines, preprocessor directives/continuations, and single-line cinder API prototypes in headers",
            "excluded": "tests, examples, docs, build configuration, generated output, vendored code, and private evidence",
        },
        "files": entries,
        "total_substantive_lines": sum(entry["substantive_lines"] for entry in entries),
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("Private source census written.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/bin/sh
set -eu
ccbin=${1:?compiler path}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
evidence="$root/.agent-local/evidence"
mkdir -p "$evidence"
"$ccbin" --version > "$evidence/compiler.version"
"$ccbin" -fverify-each -O2 -c "$root/examples/hello.c" -o "$evidence/hello.o"
file "$evidence/hello.o" > "$evidence/object.file"
"$ccbin" --emit-ir -O2 "$root/examples/hello.c" > "$evidence/hello.ir"
"$ccbin" --dump-tokens "$root/examples/hello.c" > "$evidence/hello.tokens"
"$ccbin" --interpret -O2 "$root/examples/hello.c" > "$evidence/interpreter.txt"
python3 "$root/tools/source_census.py" --root "$root" --output "$root/.agent-local/source-census.json" > "$evidence/census.summary"
python3 - "$evidence" <<'PY'
import hashlib
import json
import pathlib
import sys

base = pathlib.Path(sys.argv[1])
files = {}
for path in sorted(base.iterdir()):
    if path.is_file() and path.name != "ACCEPTANCE.json":
        files[path.name] = {"bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
payload = {
    "schema": 1,
    "status": "incomplete",
    "profile": "c17-core/linux-x86-64-elf64",
    "compiler": files.get("compiler.version", {}),
    "evidence": files,
    "gates": {
        "frontend-smoke": {"status": "pass", "runs": 1},
        "ir-interpreter": {"status": "pass", "runs": 1},
        "elf-object-smoke": {"status": "pass", "runs": 1},
        "linux-native-execution": {"status": "unverified", "runs": 0},
        "self-hosting": {"status": "unverified", "runs": 0},
        "full-differential-campaign": {"status": "unverified", "runs": 0},
        "full-fuzz-campaign": {"status": "unverified", "runs": 0},
        "private-size-threshold": {"status": "unmet", "runs": 1},
    },
}
(base / "ACCEPTANCE.json").write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n")
PY

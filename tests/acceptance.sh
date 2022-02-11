#!/bin/sh
set -eu
ccbin=${1:?compiler path}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
evidence="$root/.agent-local/evidence"
mkdir -p "$evidence"
"$ccbin" --version > "$evidence/compiler.version"
printf '%s\n' "$(git -C "$root" rev-parse HEAD)" > "$evidence/source-revision"
shasum -a 256 "$ccbin" > "$evidence/compiler.sha256"
uname -a > "$evidence/host.txt"
"$ccbin" -fverify-each -O2 -c "$root/examples/hello.c" -o "$evidence/hello.o"
file "$evidence/hello.o" > "$evidence/object.file"
"$ccbin" --emit-ir -O2 "$root/examples/hello.c" > "$evidence/hello.ir"
"$ccbin" --dump-tokens "$root/examples/hello.c" > "$evidence/hello.tokens"
"$ccbin" --interpret -O2 "$root/examples/hello.c" > "$evidence/interpreter.txt"
tests/run_globals.sh "$ccbin" > "$evidence/globals.txt"
tests/run_multi.sh "$ccbin" > "$evidence/multi.txt"
tests/run_varargs.sh "$ccbin" > "$evidence/varargs.txt"
tests/run_apps.sh "$ccbin" > "$evidence/apps.txt"
python3 "$root/tools/run_defined_cases.py" "$ccbin" --count 1200 --output "$root/.agent-local/generated-summary.json" > "$evidence/generated.txt"
python3 "$root/tools/source_census.py" --root "$root" --output "$root/.agent-local/source-census.json" > "$evidence/census.summary"
python3 - "$evidence" "$root" <<'PY'
import hashlib
import json
import pathlib
import subprocess
import sys

base = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2])
files = {}
for path in sorted(base.iterdir()):
    if path.is_file() and path.name != "ACCEPTANCE.json":
        files[path.name] = {"bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
payload = {
    "schema": 1,
    "status": "incomplete",
    "source_revision": subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip(),
    "profile": "c17-core/linux-x86-64-elf64",
    "compiler": files.get("compiler.version", {}),
    "compiler_binary_sha256": files.get("compiler.sha256", {}),
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

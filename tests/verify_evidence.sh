#!/bin/sh
set -eu
ccbin=${1:?compiler path}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
manifest="$root/artifacts/evidence/ACCEPTANCE.json"
if [ ! -f "$manifest" ]; then
  printf '%s\n' 'No acceptance evidence has been generated; verification fails honestly.' >&2
  exit 1
fi
python3 - "$manifest" "$root" <<'PY'
import hashlib
import json
import pathlib
import subprocess
import sys

manifest = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2])
data = json.loads(manifest.read_text())
if data.get("schema") != 1 or data.get("status") != "incomplete":
    raise SystemExit("invalid acceptance manifest status")
expected_revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
if data.get("source_revision") != expected_revision:
    raise SystemExit("evidence source revision does not match the checked-out tip")
base = manifest.parent
for name, record in data.get("evidence", {}).items():
    path = base / name
    if not path.is_file():
        raise SystemExit(f"missing evidence: {name}")
    if path.stat().st_size != record.get("bytes"):
        raise SystemExit(f"evidence size changed: {name}")
    if hashlib.sha256(path.read_bytes()).hexdigest() != record.get("sha256"):
        raise SystemExit(f"evidence digest changed: {name}")
failed = [name for name, gate in data.get("gates", {}).items() if gate.get("status") not in {"pass"}]
if failed:
    raise SystemExit("required gates are not complete: " + ", ".join(failed))
PY
"$ccbin" --version >/dev/null

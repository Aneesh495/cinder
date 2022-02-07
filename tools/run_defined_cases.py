#!/usr/bin/env python3
"""Generate bounded defined integer programs and compare Cinder's IR interpreter."""
from __future__ import annotations

import argparse
import json
import random
import re
import subprocess
import tempfile
from pathlib import Path

RESULT = re.compile(r"interpret main => (-?\d+)")


def make_case(rng: random.Random) -> tuple[str, int]:
    a = rng.randrange(0, 32)
    b = rng.randrange(0, 32)
    c = rng.randrange(1, 16)
    d = rng.randrange(0, 32)
    e = rng.randrange(0, 32)
    branch = ((a + b) & 31) if d < 16 else ((c * e) & 31)
    expected = (((a + b) & 63) ^ ((c << 1) & 63) ^ branch) & 255
    source = (
        "int main(void) {\n"
        f"    int a = {a}; int b = {b}; int c = {c}; int d = {d}; int e = {e};\n"
        f"    int result = ((a + b) & 63) ^ ((c << 1) & 63);\n"
        f"    if (d < 16) result = result ^ ((a + b) & 31);\n"
        f"    else result = result ^ ((c * e) & 31);\n"
        "    return result & 255;\n"
        "}\n"
    )
    return source, expected


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler", type=Path)
    parser.add_argument("--count", type=int, default=1200)
    parser.add_argument("--seed", type=int, default=0xC1D3)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    attempts = accepted = interpreted = mismatches = 0
    reference_attempts = reference_runs = reference_mismatches = 0
    compiler = str(args.compiler.resolve())
    with tempfile.TemporaryDirectory(prefix="cinder-cases-") as directory:
        root = Path(directory)
        for index in range(args.count):
            attempts += 1
            source, expected = make_case(rng)
            path = root / f"case-{index:04d}.c"
            path.write_text(source, encoding="utf-8")
            accepted += 1
            result = subprocess.run([compiler, "--interpret", "-O2", str(path)], text=True, capture_output=True, check=False)
            match = RESULT.search(result.stdout)
            if match is None:
                mismatches += 1
                continue
            interpreted += 1
            if int(match.group(1)) != expected:
                mismatches += 1
            if index < min(100, args.count):
                reference_attempts += 1
                native = root / f"reference-{index:04d}"
                build = subprocess.run(["cc", "-std=c17", "-O0", str(path), "-o", str(native)], text=True, capture_output=True, check=False)
                if build.returncode == 0:
                    reference_runs += 1
                    run = subprocess.run([str(native)], check=False)
                    if (run.returncode & 255) != expected:
                        reference_mismatches += 1
    summary = {
        "schema": 1,
        "seed": args.seed,
        "attempts": attempts,
        "accepted_defined_programs": accepted,
        "interpreter_runs": interpreted,
        "interpreter_mismatches": mismatches,
        "reference_attempts": reference_attempts,
        "reference_runs": reference_runs,
        "reference_mismatches": reference_mismatches,
        "status": "pass" if mismatches == 0 and reference_mismatches == 0 and interpreted == accepted else "fail",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))
    return 0 if summary["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Generate bounded defined integer programs and compare Cinder's IR interpreter."""
from __future__ import annotations

import argparse
import concurrent.futures
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


def run_case(item: tuple[int, Path, int], compiler: str, root: Path) -> tuple[bool, bool, bool, bool]:
    index, path, expected = item
    result = subprocess.run([compiler, "--interpret", "-O2", str(path)], text=True, capture_output=True, check=False)
    match = RESULT.search(result.stdout)
    interpreted_ok = match is not None and int(match.group(1)) == expected
    reference_attempted = index < 100
    reference_ok = True
    if reference_attempted:
        native = root / f"reference-{index:04d}"
        build = subprocess.run(["cc", "-std=c17", "-O0", str(path), "-o", str(native)], text=True, capture_output=True, check=False)
        if build.returncode != 0:
            reference_ok = False
        else:
            run = subprocess.run([str(native)], check=False)
            reference_ok = (run.returncode & 255) == expected
    return match is not None, interpreted_ok, reference_attempted and build.returncode == 0 if reference_attempted else False, reference_ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("compiler", type=Path)
    parser.add_argument("--count", type=int, default=1200)
    parser.add_argument("--seed", type=int, default=0xC1D3)
    parser.add_argument("--jobs", type=int, default=8)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    compiler = str(args.compiler.resolve())
    with tempfile.TemporaryDirectory(prefix="cinder-cases-") as directory:
        root = Path(directory)
        cases: list[tuple[int, Path, int]] = []
        for index in range(args.count):
            source, expected = make_case(rng)
            path = root / f"case-{index:04d}.c"
            path.write_text(source, encoding="utf-8")
            cases.append((index, path, expected))
        interpreted_runs = interpreted_mismatches = reference_attempts = reference_runs = reference_mismatches = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
            futures = [executor.submit(run_case, item, compiler, root) for item in cases]
            for future in concurrent.futures.as_completed(futures):
                found, interpreter_ok, reference_ran, reference_ok = future.result()
                interpreted_runs += int(found)
                interpreted_mismatches += int(not interpreter_ok)
                reference_attempts += int(reference_ran or not reference_ran and False)
                reference_runs += int(reference_ran)
                reference_mismatches += int(reference_ran and not reference_ok)
    summary = {
        "schema": 1,
        "seed": args.seed,
        "jobs": args.jobs,
        "attempts": args.count,
        "accepted_defined_programs": args.count,
        "interpreter_runs": interpreted_runs,
        "interpreter_mismatches": interpreted_mismatches,
        "reference_attempts": min(100, args.count),
        "reference_runs": reference_runs,
        "reference_mismatches": reference_mismatches,
        "status": "pass" if interpreted_mismatches == 0 and reference_mismatches == 0 and interpreted_runs == args.count else "fail",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(summary, sort_keys=True))
    return 0 if summary["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())

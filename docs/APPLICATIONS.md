# Authored application fixtures and generated cases

`examples/apps/` contains eight small authored integrations: checksum, word-hash, graph path scoring, a bytecode-style loop, sort/search scoring, CRC-style bit processing, a matrix kernel, and a multi-function CLI score. Each is compiled through the normal driver, interpreted through the independent IR interpreter, and emitted as an ELF object by `tests/run_apps.sh`.

`tools/run_defined_cases.py` generates bounded, defined integer programs from a fixed seed. It reports separate attempts, accepted programs, interpreter runs, interpreter mismatches, reference attempts, reference runs, and reference mismatches. The current acceptance invocation runs 1,200 Cinder interpreter cases and 100 host-reference executions. These workloads are meaningful smoke and regression evidence, not a substitute for the requested 20,000-program differential campaign or 4,000,000-execution fuzz campaign.

Generated cases avoid signed overflow, invalid shifts, division by zero, out-of-bounds access, unsequenced effects, raw-address observations, and unspecified padding. Reference status is inconclusive if the host compiler cannot build or execute a case; it is never silently counted as a successful comparison.

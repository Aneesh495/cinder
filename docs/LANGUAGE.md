# Cinder `c17-core` language and target contract

## Target

Cinder emits little-endian ELF64 relocatable objects for Linux x86-64. The data model is LP64: `char` is 8 bits, `short` is 16 bits, `int` is 32 bits, `long` and pointers are 64 bits, and `long long` is 64 bits. `char` is signed in the initial profile. Scalar alignment follows the documented System V profile. The compiler does not infer target widths from the host build's `sizeof` values.

The default object mode is non-PIC, non-PIE. `-fPIC` is rejected until a complete PIC relocation and ABI test set is implemented. The supported execution environment is Linux x86-64. macOS arm64 is a supported build host for frontend tools and cross-target object generation, not a claim of native AArch64 code generation.

## Supported profile boundary

The current implementation is intentionally staged. Required semantic families are tracked in `docs/FEATURES.json`; a family is only marked complete after source, semantic, native, and negative evidence exists. The initial end-to-end slice includes preprocessing macros, integer literals and expressions, scalar locals, pointers in the type model, function declarations/definitions, calls, returns, conditionals, loops, constant globals, inline struct/union layout, string-backed global arrays, separate object emission, and direct x86-64 integer code generation.

Scalar conversions, float32/double operations and scalar ABI boundaries, pruned
SSA promotion, cyclic phi edge transfers, scoped typedefs/enums/tags, nested
declarators, fixed array layout, and canonical IR round trips are implemented.
See `CONVERSIONS.md`, `DECLARATIONS.md`, and `IR.md` for tested boundaries.
Addressable object memory, aggregate expression lowering/ABI, complete variadic
state, bitfields, debug information, and advanced C11/C17 constructs remain open.
Long double, complex, atomics, thread-local execution, variable-length arrays,
GNU inline assembly/vector extensions, and C++ input are excluded and diagnosed.

## Undefined and unspecified behavior

Tests only compare defined programs or record an explicit ineligible classification. Signed overflow, invalid shifts, division by zero, unsequenced side effects, invalid lifetimes, out-of-bounds access, unspecified padding, raw pointer-address observations, and unconstrained NaN payloads are not equality oracles. Cinder's optimization passes must preserve the declared definedness preconditions.

## Diagnostics

Fatal lexical, preprocessing, semantic, unsupported-profile, verifier, and output errors produce a nonzero exit status and do not publish a new successful artifact. Diagnostics preserve a primary source range. Macro/include provenance is retained by the token model and is expanded as the implementation gains source-manager coverage.

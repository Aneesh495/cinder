# Numeric literals

Integer parsing uses checked target-width accumulation. Decimal, octal, and
hexadecimal syntax have distinct valid digits. Unsigned and long/long-long
suffixes are recognized once each, in their permitted order and case. The
selected type follows the target LP64 rank and range rules. Full-width unsigned
values retain their bit pattern; out-of-range decimal signed literals fail.

Decimal and hexadecimal floating constants require complete significands and
exponents. Hex floats require a binary exponent. The declared formats are
IEEE binary32 and binary64. Host conversion is accepted only when the host
arithmetic formats match that contract. Float32 literals use direct `strtof`
conversion, which avoids a binary64 intermediate rounding. Long double remains
outside the current profile. Literal type metadata reaches semantic analysis
and typed IR; complete arithmetic and memory conversion rules remain separate
work.

The source cases check target literal sizes, suffix ranks, full-width unsigned
values, hex digits containing E, signed exponents, leading decimal points,
hex floats, and a decimal value just above a float32 halfway boundary. Negative
cases check malformed suffixes/digits/exponents and range overflow. Invalid
literals produce a diagnostic and preserve any existing output artifact.

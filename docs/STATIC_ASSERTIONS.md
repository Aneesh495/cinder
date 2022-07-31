# Static assertion declarations

`_Static_assert(condition, "message");` is accepted as a file declaration,
a block item, or a struct/union member declaration. It is not a statement
and cannot form an unbraced selection or iteration body. The declaration
adds no object, member, symbol, or runtime effect.

The condition must be an integer constant expression whose value is nonzero.
It follows the same typed target-width evaluator as enums and array bounds.
Its retained expression receives full semantic checking with the original
bindings and function context, including invalid unevaluated initializers.
Parser-time and fully typed constant values must agree.

Failure diagnostics contain the decoded message. Adjacent literals concatenate;
non-printable bytes are escaped so an embedded NUL cannot hide later message
text. The ordinary literal decoder supplies escape and encoding checks.
A failed assertion prevents artifact publication through the normal driver.

`make test-static-assert` compares authored declarations with GCC and Clang
at O0/O2 and checks original object bytes/relocations against assembly. The
constraint runner also requires specific assertion messages and preservation
of prior output. The source/IR suites check elimination of runtime effects.

The declaration contract follows section 6.7.10 of
[WG14 N1539](https://www.open-std.org/jtc1/SC22/wg14/www/docs/n1539.pdf).
The C42 registry retains complete source-bound feature acceptance as open.

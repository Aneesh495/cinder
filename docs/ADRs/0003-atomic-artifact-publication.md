# Atomic artifact publication

The compiler builds output in an exclusive sibling file created by `mkstemp`.
Object and assembly writers check stream errors and close the complete file
before renaming it over the requested destination. A semantic, encoder, I/O, or
link failure removes the owned sibling and preserves the prior destination.

Link inputs live in separate owned directories created by `mkdtemp`. The Linux
driver invokes `cc -no-pie` with object paths and explicit output arguments. It
waits through `EINTR` and publishes only after successful linker termination.
The system linker never receives C source. Successful rename is atomic on the
destination filesystem; this does not promise durability across power loss.

`tests/run_output.py` exercises object, assembly, preprocessing, inaccessible
destinations, and failed external-symbol linking. Abrupt process termination
can still leave an owned staging file; interruption cleanup remains open.

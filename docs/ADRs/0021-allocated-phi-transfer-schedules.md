# Allocated phi transfer schedules

Status: accepted

Physical parallel-copy scheduling previously ran inside the final encoder.
The selected function owned phi operands, but the target edge schedule did
not exist until byte emission. This hid a conditional-edge gap: a phi target
with only one predecessor was left unsplit and received no transfer.

Every conditional phi edge is now split, including equal-successor branches.
After allocation, each jump predecessor owns its scheduled value transfers,
register banks, input count and cycle-temporary count. The resolver uses actual
physical register/spill identities. GPR cycles use R11, SSE cycles use XMM1,
and the emitter only performs the resulting moves.

A separate checker models symbolic source tokens in physical storage. It
executes the proposed schedule, verifies each destination receives its original
simultaneous phi input, and checks endpoints, banks, limits and temporary
accounting. It does not call the copy resolver. Correct alternative schedules
are allowed; unsafe ordering and stale physical plans fail verification.
The allocator checker and final encoder both enforce this boundary before
output is changed.

The forced-cycle API fixture now explicitly regenerates its frame and copy
plans after replacing allocation locations. Tests cover integer, binary32 and
binary64 three-way cycles; both conditional outcomes; two separate phi targets;
and a branch whose two arms reach the same single-predecessor phi. Nine
mutations cover structural faults, wrong live inputs, omitted transfers and
destroying a source before saving its cycle temporary. Restored objects must
execute like independent C references. Baseline probes check byte identity on
the earlier cyclic profile.

This increment leaves constant, bulk-memory, argument/call and variadic target
pseudo expansion open. It does not claim full machine-representation acceptance.

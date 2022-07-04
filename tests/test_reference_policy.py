#!/usr/bin/env python3
"""A known compiler discrepancy cannot excuse a new mismatch or changed input."""
import pathlib
import tempfile
from reference_policy import adjudicate

source = pathlib.Path('tests/initializers/local_copy_then_leaf_override.c')
assert not adjudicate(source, 18, 16, dict(family='gcc'))['agreement']
assert adjudicate(source, 18, 18, dict(family='clang'))['agreement']
assert not adjudicate(source, 18, 18, dict(family='clang'))['eligible']
identity_source = pathlib.Path('tests/aggregates/comma_snapshot.c')
assert not adjudicate(identity_source, 14, 18, dict(family='clang'))['eligible']
assert not adjudicate(identity_source, 14, 14, dict(family='clang'))['eligible']
probes = [(source, 18, 17, dict(family='gcc')), (source, 18, 16, dict(family='clang')), (source, 19, 16, dict(family='gcc'))]
with tempfile.TemporaryDirectory() as directory:
    changed = pathlib.Path(directory) / source.name
    changed.write_bytes(source.read_bytes() + b'\n')
    probes.append((changed, 18, 16, dict(family='gcc')))
    probes.append((changed, 18, 18, dict(family='clang')))
    for probe in probes:
        try:
            adjudicate(*probe)
        except AssertionError:
            continue
        raise AssertionError(('accepted an unadjudicated discrepancy', probe))
print('reference policy: source, family, expected result and unknown outcome mutations rejected')

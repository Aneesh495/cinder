"""Preserve source-bound, standards-adjudicated reference disagreements."""
import hashlib
import json
import pathlib
import subprocess


def identify(command):
    version = subprocess.run([command, '--version'], capture_output=True, check=True).stdout.decode()
    macros = subprocess.run([command, '-dM', '-E', '-x', 'c', '-'], input=b'', capture_output=True, check=True).stdout
    family = 'clang' if b'#define __clang__ ' in macros else 'gcc' if b'#define __GNUC__ ' in macros else 'unknown'
    return dict(command=command, family=family, version=version)


def adjudicate(source, expected, observed, reference):
    if observed == expected:
        return dict(agreement=True)
    registry = json.loads(pathlib.Path(__file__).with_name('reference_deviations.json').read_text())
    key = pathlib.Path(source).name
    entry = registry.get(key)
    digest = hashlib.sha256(pathlib.Path(source).read_bytes()).hexdigest()
    assert entry and digest == entry['source_sha256'], (source, reference, expected, observed, 'unadjudicated reference disagreement')
    assert expected == entry['standard_exit'] and reference['family'] == entry['family'] and observed == entry['reference_exit'], (source, reference, expected, observed, 'unexpected reference outcome')
    return dict(agreement=False, ruling=entry['ruling'], reference_exit=observed, standard_exit=expected)

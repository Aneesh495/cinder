#!/usr/bin/env python3
"""Exercise exclusions and preserve string/character lexical boundaries."""
import importlib.util
import pathlib
import tempfile

spec = importlib.util.spec_from_file_location('source_census', 'tools/source_census.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
source = r'''#define OTHER(x) \
    fabricated_macro_body(x)
/* full comment
   second comment line */
int function(void) {
    const char *url = "https://example.test/a/*b*/";
    const char *quoted = "\\\"//still a string"; /* real comment */
    char quote = '\''; // trailing comment
    return url[0] + quoted[0] + quote;
}
'''
with tempfile.TemporaryDirectory(prefix='cinder-census-') as directory:
    path = pathlib.Path(directory) / 'example.c'
    path.write_text(source)
    assert module.count_file(path) == 5
    stripped = module.strip_comments(source)
    assert 'https://example.test/a/*b*/' in stripped and '//still a string' in stripped
    assert 'real comment' not in stripped and 'second comment line' not in stripped
    header = pathlib.Path(directory) / 'example.h'
    header.write_text('void cinder_example(int value);\nstruct State {\n    int value;\n};\n')
    assert module.count_file(header) == 2
print('source census: lexical boundaries and exclusions passed')

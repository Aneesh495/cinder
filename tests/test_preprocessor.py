#!/usr/bin/env python3
"""Differential token observations for individually authored PP source cases."""
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

TOKEN = re.compile(r'(?:u8|[LuU])?"(?:\\.|[^"\\])*"|(?:[LuU])?\'(?:\\.|[^\'\\])*\'|[A-Za-z_]\w*|(?:\d|\.\d)(?:[\w.]|[eEpP][+-])*|%:%:|>>=|<<=|\.\.\.|##|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||\*=|/=|%=|\+=|-=|&=|\^=|\|=|[^\s]')


def main():
    compiler = str(pathlib.Path(sys.argv[1]).resolve())
    fixtures = pathlib.Path(__file__).resolve().parent / 'preprocessor'
    references = [os.environ.get('CINDER_PP_REFERENCE', 'cc')]
    observations = []
    with tempfile.TemporaryDirectory(prefix='cinder-pp-') as directory:
        for path in sorted(fixtures.glob('*.c')):
            result = subprocess.run([compiler, '-E', '-I', str(fixtures), str(path)], capture_output=True, text=True, timeout=10)
            negative = path.name.startswith('invalid_')
            record = {'case': path.name, 'cinder_exit': result.returncode}
            if negative:
                assert result.returncode != 0 and 'error:' in result.stderr, (path, result)
                record['diagnostic'] = result.stderr
            else:
                assert result.returncode == 0, (path, result.stderr)
                actual = TOKEN.findall(result.stdout)
                for reference in references:
                    expected = subprocess.run([reference, '-std=c17', '-E', '-P', '-I', str(fixtures), str(path)], capture_output=True, text=True, timeout=10)
                    assert expected.returncode == 0, (path, expected.stderr)
                    reference_output = expected.stdout
                    if path.name == 'pragma_operator.c':
                        assert '#pragma STDC FP_CONTRACT OFF' in reference_output, reference_output
                        reference_output = reference_output.replace('#pragma STDC FP_CONTRACT OFF', '')
                        record['reference_pragma'] = 'STDC FP_CONTRACT OFF'
                    assert actual == TOKEN.findall(reference_output), (path, result.stdout, expected.stdout)
                record['tokens'] = actual
            observations.append(record)
        included = fixtures / 'provenance.h'
        source = pathlib.Path(directory) / 'provenance.c'
        source.write_text('#include "provenance.h"\nint main(void) { return BAD_VALUE; }\n')
        result = subprocess.run([compiler, '-fsyntax-only', '-I', str(fixtures), str(source)], capture_output=True, text=True, timeout=10)
        assert result.returncode != 0
        assert f'{source}:2:' in result.stderr, result.stderr
        assert f'{included.resolve()}:2:' in result.stderr and 'expanded from macro' in result.stderr, result.stderr
        source.write_text('#include "provenance.h"\nint main(void) { return INCLUDED_UNDECLARED; }\n')
        result = subprocess.run([compiler, '--dump-tokens', '-I', str(fixtures), str(source)], capture_output=True, text=True, timeout=10)
        assert f'{included.resolve()}:3:' in result.stdout, result.stdout
    evidence = pathlib.Path('.agent-local/preprocessor')
    evidence.mkdir(parents=True, exist_ok=True)
    (evidence / 'observations.json').write_text(json.dumps(observations, indent=2) + '\n')
    print(f'preprocessor: {len(observations)} authored token/diagnostic cases and source provenance passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

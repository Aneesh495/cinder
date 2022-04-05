#!/usr/bin/env python3
"""Check publication failures against existing complete artifacts."""
import pathlib
import subprocess
import sys
import tempfile


def main():
    compiler = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="cinder-publication-") as directory:
        root = pathlib.Path(directory)
        valid = root / "valid.c"
        valid.write_text("int main(void) { return 7; }\n")
        invalid = root / "invalid.c"
        invalid.write_text("int main(void) { return undeclared; }\n")
        unresolved = root / "unresolved.c"
        unresolved.write_text("int missing(void); int main(void) { return missing(); }\n")
        for mode in ("-c", "-S"):
            destination = root / "existing artifact"
            destination.write_bytes(b"complete prior artifact\n")
            result = subprocess.run([str(compiler), mode, str(invalid), "-o", str(destination)], capture_output=True)
            assert result.returncode != 0, result
            assert destination.read_bytes() == b"complete prior artifact\n"
            result = subprocess.run([str(compiler), mode, str(valid), "-o", str(root / "absent" / "output")], capture_output=True)
            assert result.returncode != 0, result
            result = subprocess.run([str(compiler), mode, str(valid), "-o", str(destination)], capture_output=True)
            assert result.returncode == 0, result.stderr
            assert destination.read_bytes() != b"complete prior artifact\n"
        destination = root / "preprocessed"
        result = subprocess.run([str(compiler), "-E", str(valid), "-o", str(destination)], capture_output=True)
        assert result.returncode == 0 and result.stdout == b"", result
        assert b"return 7" in destination.read_bytes()
        destination.write_bytes(b"complete prior executable\n")
        result = subprocess.run([str(compiler), str(unresolved), "-o", str(destination)], capture_output=True)
        assert result.returncode != 0
        assert destination.read_bytes() == b"complete prior executable\n"
        assert not list(root.glob("*.cinder-*")), list(root.iterdir())
    print("output publication: complete replacements and preserved failures passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

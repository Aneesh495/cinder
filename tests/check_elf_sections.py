#!/usr/bin/env python3
from __future__ import annotations
import struct
import sys

path = sys.argv[1]
data = open(path, "rb").read()
if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
    raise SystemExit("not an ELF64 little-endian object")
header = struct.unpack_from("<16sHHIQQQIHHHHHH", data, 0)
_, e_type, e_machine, _, _, _, shoff, _, _, _, _, shentsize, shnum, shstrndx = header
if e_type != 1 or e_machine != 62:
    raise SystemExit("unexpected ELF type or machine")
sections = []
for index in range(shnum):
    off = shoff + index * shentsize
    sections.append(struct.unpack_from("<IIQQQQIIQQ", data, off))
string_section = sections[shstrndx]
string_bytes = data[string_section[4]:string_section[4] + string_section[5]]
names = set()
for section in sections:
    start = section[0]
    end = string_bytes.find(b"\0", start)
    names.add(string_bytes[start:end].decode())
required = {".text", ".data", ".rodata", ".bss", ".rela.text", ".symtab", ".strtab", ".shstrtab", ".note.GNU-stack"}
missing = required - names
if missing:
    raise SystemExit("missing sections: " + ", ".join(sorted(missing)))
print("ELF64 sections verified:", ", ".join(sorted(required)))

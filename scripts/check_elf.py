#!/usr/bin/env python3
"""Verifica propiedades significativas del binario, no solo que exista."""
import struct,subprocess,sys
from pathlib import Path
b=Path(sys.argv[1]).read_bytes()
assert b[:4]==b'\x7fELF' and b[4]==2 and b[5]==1, 'Se requiere ELF64 little endian'
assert struct.unpack_from('<H',b,18)[0]==62, 'Se requiere x86-64'
assert struct.unpack_from('<Q',b,24)[0]>=0xffffffff80000000, 'Entrada higher-half invalida'
symbols=subprocess.check_output(['nm','--undefined-only',sys.argv[1]],text=True)
assert not symbols.strip(), 'Hay simbolos sin resolver: '+symbols
phoff=struct.unpack_from('<Q',b,32)[0]
phsize,phnum=struct.unpack_from('<HH',b,54)
for i in range(phnum):
    t,flags=struct.unpack_from('<II',b,phoff+i*phsize)
    assert t!=3, 'No debe existir interprete dinamico'
    if t==1: assert flags&3!=3, 'Segmento simultaneamente escribible y ejecutable'
for magic in (0xf6b8f4b39de7d1ae,0xadc0e0531bb10d03):
    assert struct.pack('<Q',magic) in b, 'Marcador Limine eliminado'
print('OK: ELF64 x86-64, entrada higher-half, sin simbolos pendientes, sin RWX y con marcadores Limine.')

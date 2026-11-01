#!/usr/bin/env python3
"""Create exclusively a NEW regular image below project build/. Never overwrites."""
import os,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def create(path):
    path=Path(path)
    if not path.is_absolute():path=ROOT/path
    build=ROOT/'build';build.mkdir(exist_ok=True)
    if path.parent.resolve()!=build.resolve() or path.is_symlink():
        raise ValueError('Solo archivos nuevos directamente en build/')
    mbr=bytearray(512);mbr[446+4]=0x7f
    struct.pack_into('<II',mbr,446+8,2048,14336);mbr[510:512]=b'\x55\xaa'
    marker=bytearray(512);marker[:12]=b'TCOS-DATA-V1'
    struct.pack_into('<III',marker,16,16384,2048,2317)
    with path.open('xb') as f:
        f.truncate(8*1024*1024);f.seek(0);f.write(mbr);f.write(marker);f.flush();os.fsync(f.fileno())
    return path
if __name__=='__main__':
    import argparse
    p=argparse.ArgumentParser();p.add_argument('image',nargs='?',default='build/data.img');a=p.parse_args()
    print(create(a.image))

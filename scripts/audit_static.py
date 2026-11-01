#!/usr/bin/env python3
"""Read-only syntax, build artifacts and ISO coherence checks."""
import ast,hashlib,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    count=0
    for directory in ('scripts','artifacts'):
        for path in (ROOT/directory).glob('*.py'):
            ast.parse(path.read_text(),filename=str(path));count+=1
    print(f'PASSED: sintaxis estatica de {count} scripts Python (sin ejecutarlos)')
    required=list((ROOT/'build').glob('*.o'))+[ROOT/p for p in ['build/kernel.elf','build/iso_root/boot/kernel.elf','build/toolchain-os.iso','build/test-fs-host']]
    assert required
    for path in required:
        assert path.is_file() and path.stat().st_size>0,str(path)+' vacio'
        if path.suffix in ('.o','.elf') or path.name=='test-fs-host':
            assert path.read_bytes()[:4]==b'\x7fELF',str(path)+' no ELF'
    kernel=(ROOT/'build/kernel.elf').read_bytes()
    assert kernel[:6]==b'\x7fELF\x02\x01'
    phoff=struct.unpack_from('<Q',kernel,32)[0];phsize,phnum=struct.unpack_from('<HH',kernel,54)
    for i in range(phnum):
        typ,flags,off,va,pa,size,mem,align=struct.unpack_from('<IIQQQQQQ',kernel,phoff+i*phsize)
        assert off+size<=len(kernel) and mem>=size
    iso=(ROOT/'build/toolchain-os.iso').read_bytes()
    pvd=iso[16*2048:17*2048];assert pvd[1:6]==b'CD001'
    def entries(record):
        start=struct.unpack_from('<I',record,2)[0]*2048;size=struct.unpack_from('<I',record,10)[0]
        data=iso[start:start+size];pos=0;out={}
        while pos<len(data):
            n=data[pos]
            if not n:pos=(pos//2048+1)*2048;continue
            r=data[pos:pos+n];assert len(r)>=34
            name=r[33:33+r[32]].decode('ascii','replace').split(';')[0].lower()
            out[name]=r;pos+=n
        return out
    record=entries(entries(pvd[156:190])['boot'])['kernel.elf']
    offset=struct.unpack_from('<I',record,2)[0]*2048;size=struct.unpack_from('<I',record,10)[0]
    assert iso[offset:offset+size]==kernel==(ROOT/'build/iso_root/boot/kernel.elf').read_bytes()
    print('PASSED: objetos/ELF/ISO no vacios; kernel identico en build, staging e ISO')
    print('KERNEL_BYTES',len(kernel),'ISO_BYTES',len(iso))
    for name in ['build/kernel.elf','build/toolchain-os.iso','build/data.img']:
        path=ROOT/name;print('SHA256',hashlib.sha256(path.read_bytes()).hexdigest(),name)
if __name__=='__main__':main()

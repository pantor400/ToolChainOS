#!/usr/bin/env python3
import hashlib,shutil,struct,uuid
from pathlib import Path
from create_test_disk import create
from qemu_test import Guest
tag=uuid.uuid4().hex[:10]
image=create('build/test-'+tag+'.img')
def drive(p,readonly=False):
    return ['-drive',f'file={p},format=raw,if=ide,index=0'+(',readonly=on' if readonly else '')]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
try:
    initial=digest(image)
    # First boot exercises ONLY reads; verify byte-for-byte unchanged image.
    with Guest('ata-first-read',drive(image),machine='pc') as g:
        g.send('disk info',['PRIMARY MASTER 0x1F0','SECTORS=16384','AUTHORIZED'])
        g.send('disk mbr',['0x55AA','START=2048','SIZE=14336'])
        g.send('disk read 0',['SECTOR LBA=0'])
        g.send('disk read 16384',['LBA FUERA DE RANGO'])
    assert digest(image)==initial
    with Guest('persist-boot1',drive(image),machine='pc') as g:
        g.send('disk test',['DISK TEST PASSED'])
        g.send('mkdir keep',['PERSISTENCIA OK'])
        g.send('crear keep/a original',['PERSISTENCIA OK'])
        g.send('ren keep/a renamed',['PERSISTENCIA OK'])
        g.send('copy keep/renamed keep/copied',['PERSISTENCIA OK'])
        g.send('rm keep/copied',['PERSISTENCIA OK'])
        g.send('edit keep/renamed',marker='EDIT> ')
        g.send(':clear',marker='EDIT> ')
        g.send('linea uno',marker='EDIT> ')
        g.send('linea dos',marker='EDIT> ')
        g.send(':wq',['PERSISTENCIA OK'])
    with Guest('persist-boot2',drive(image),machine='pc') as g:
        assert b'FS RECUPERADO GENERATION=' in g.transcript
        g.send('open keep/renamed',['linea uno','linea dos'])
        g.send('open keep/a',['ARCHIVO NO ENCONTRADO'])
        g.send('open keep/copied',['ARCHIVO NO ENCONTRADO'])
        g.send('rm keep/renamed',['PERSISTENCIA OK'])
        g.send('rmdir keep',['PERSISTENCIA OK'])
    with Guest('persist-boot3',drive(image),machine='pc') as g:
        g.send('open keep/renamed',['ARCHIVO NO ENCONTRADO'])
        g.send('tree /',['nota','script'])
        g.send('fsstatus',['MODE=PERSISTENT','DIRTY=NO'])
    # Corruption isolated to fresh regular copies inside build.
    corrupt=Path('build/corrupt-'+tag+'.img');shutil.copyfile(image,corrupt)
    with corrupt.open('r+b') as f:
        headers=[]
        for slot in (0,1):
            f.seek((2049+134*slot)*512);h=f.read(512)
            headers.append(struct.unpack_from('<Q',h,16)[0])
        slot=max(range(2),key=headers.__getitem__);f.seek((2049+134*slot)*512);f.write(b'X'*512)
    with Guest('persist-recovery',drive(corrupt),machine='pc') as g:
        assert b'FS RECUPERADO GENERATION=' in g.transcript
        g.send('open nota',['Bienvenido'])
    with corrupt.open('r+b') as f:
        for slot in (0,1):f.seek((2049+134*slot)*512);f.write(b'X'*512)
    with Guest('persist-corrupt',drive(corrupt),machine='pc') as g:
        assert b'CORRUPT' in g.transcript
        g.send('fsstatus',['MODE=RAM'])
        g.send('disk test',['SIN ZONA AUTORIZADA'])
    corrupt.unlink()
    # Readonly drive: changing FS must report failure and remain responsive.
    config=Path('build/blkdebug-'+tag+'.conf')
    config.write_text('[inject-error]\nevent = "write_aio"\niotype = "write"\nerrno = "5"\nimmediately = "on"\nonce = "off"\n')
    faulty=['-drive',f'file=blkdebug:{config}:{image},format=raw,if=ide,index=0,werror=report']
    with Guest('persist-write-error',faulty,machine='pc') as g:
        g.send('disk info', ['WRITE=AUTHORIZED'])
        g.send('crear readonly ram',['ERROR PERSISTENCIA'])
        g.send('suma 2 3',['= 5'])
    config.unlink()
    print('PASSED: ATA lectura inicial sin escritura, limites, WRITE/READ/RESTORE, persistencia en 3 arranques, recuperacion/corrupcion y fallo I/O inyectado')
finally:
    image.unlink(missing_ok=True)

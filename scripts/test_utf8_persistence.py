#!/usr/bin/env python3
"""UTF-8 persistence across real restarts, only on a new disposable image."""
import uuid
from pathlib import Path
from create_test_disk import create
from qemu_test import Guest
image=create('build/utf8-test-'+uuid.uuid4().hex[:10]+'.img')
drive=['-drive',f'file={image},format=raw,if=ide,index=0']
sample='Perú: año, programación, pingüino. ¿Qué? ¡Sí! ÁÉÍÓÚ Ñ Ü.'
try:
    with Guest('utf8-persist-boot1',drive,machine='pc') as g:
        g.send('mkdir Perú',['PERSISTENCIA OK'])
        g.send('crear Perú/niño '+sample,['PERSISTENCIA OK'])
        g.send('edit Perú/niño',marker='EDIT> ')
        g.send(':clear',marker='EDIT> ')
        g.send(sample,marker='EDIT> ')
        g.send('Segunda línea: áéíóú ñ ü',marker='EDIT> ')
        g.send(':wq',['PERSISTENCIA OK'])
        g.send('ren Perú/niño año',['PERSISTENCIA OK'])
    with Guest('utf8-persist-boot2',drive,machine='pc') as g:
        g.send('open Perú/año',[sample,'Segunda línea: áéíóú ñ ü'])
        g.buffer.decode('utf-8')
        g.send('copy Perú/año Perú/copia',['PERSISTENCIA OK'])
        g.send('open Perú/copia',[sample])
        g.send('rm Perú/año',['PERSISTENCIA OK'])
    with Guest('utf8-persist-boot3',drive,machine='pc') as g:
        g.send('open Perú/copia',[sample,'Segunda línea: áéíóú ñ ü'])
        g.send('open Perú/año',['ARCHIVO NO ENCONTRADO'])
    print('PASSED: nombres/contenido UTF-8, editor/REN/COPY/RM y persistencia en 3 arranques')
finally:image.unlink(missing_ok=True)

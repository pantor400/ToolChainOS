#!/usr/bin/env python3
"""Real framebuffer capture through local QMP; no synthetic screenshot."""
import json,socket,sys,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from qemu_test import Guest
path=Path('build/capture-'+uuid.uuid4().hex[:8]+'.sock')
try:
    with Guest('capture-demo',['-qmp',f'unix:{path},server=on,wait=off']) as g:
        g.send('limpiar')
        g.send('info',['PAGINACION: ACTIVA'])
        g.send('memstats',['PAGE=4096'])
        g.send('uptime',['HZ=100'])
        g.send('suma 25 -8',['= 17'])
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(5);client.connect(str(path))
            stream=client.makefile('rwb',buffering=0)
            json.loads(stream.readline())
            def call(name,args=None):
                request={'execute':name}
                if args is not None:request['arguments']=args
                stream.write((json.dumps(request)+'\n').encode())
                while True:
                    response=json.loads(stream.readline())
                    if 'error' in response:raise RuntimeError(response)
                    if 'return' in response:return response
            call('qmp_capabilities')
            call('screendump',{'filename':str(Path('artifacts/arranque-actual.png').resolve()),'format':'png'})
    print('PASSED: captura REAL de framebuffer artifacts/arranque-actual.png')
finally:path.unlink(missing_ok=True)

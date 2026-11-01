#!/usr/bin/env python3
"""Verify V3 ASCII banner, complete HELP and capture the actual framebuffer."""
import json,socket,uuid
from pathlib import Path
from qemu_test import Guest
sock=Path('build/presentation-'+uuid.uuid4().hex[:8]+'.sock')
with Guest('v31-presentacion',['-qmp',f'unix:{sock},server=on,wait=off']) as g:
    banner=g.transcript.decode('utf-8')
    for text in ['TOOLCHAIN OS V3.1 E56 - SISTEMA OPERATIVO X86-64','C + NASM + LD + MAKE + LIMINE + QEMU',
                 'PARA CONSULTAR A LA IA: IA <PREGUNTA>','KERNEL_READY','OS> ']:
        assert text in banner,text
    assert 'PARCIAL' not in banner and '?' not in banner
    def capture(name):
        with socket.socket(socket.AF_UNIX) as client:
            client.settimeout(5);client.connect(str(sock));stream=client.makefile('rwb',buffering=0)
            json.loads(stream.readline())
            for command in [{'execute':'qmp_capabilities'},
              {'execute':'screendump','arguments':{'filename':str(Path('artifacts/'+name).resolve()),'format':'png'}}]:
                stream.write((json.dumps(command)+'\n').encode())
                while True:
                    result=json.loads(stream.readline())
                    if 'error' in result:raise RuntimeError(result)
                    if 'return' in result:break
    capture('inicio-e56.png')
    text=g.send('help')
    required=['TECLADO [ES|US|1|2]','HELP','INFO','MEM','UPTIME','LIMPIAR','HALT','ECO','TEXTO','SUMA','COLOR','FONDO',
       'LS / DIR','PWD','CD','MKDIR','CREAR','OPEN','EDIT','RM','RMDIR','REN','COPY','TREE',
       ':show',':clear',':line N texto',':del N',':wq',':q','EDIT>','MEMSTATS','PMMTEST',
       'TASKS START','EXTEST DE/PF','DISK INFO','DISK READ <LBA>','DISK MBR','DISK TEST','FSSTATUS',
       'IA <PREGUNTA>','IA STATUS','IA CANCEL','RUN CPP','RUN_CPP','CREAR_CARPETA','CREAR_ARCHIVO',
       'TOUCH','CANCELAR','PARA USAR LA INTELIGENCIA ARTIFICIAL, ESCRIBE IA SEGUIDO DE TU PREGUNTA.',
       'IA Que es un sistema operativo?','SERVIDOR OLLAMA Y PUENTE COM2 ACTIVOS']
    for command in required:assert command in text,command
    capture('help-e56.png')
    g.send('suma 25 -8',['= 17'])
sock.unlink(missing_ok=True)
print(f'PASSED: bienvenida V3.1 ASCII, HELP con {len(required)} textos/sintaxis, marcadores y SUMA; dos capturas reales')

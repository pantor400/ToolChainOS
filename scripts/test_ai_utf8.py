#!/usr/bin/env python3
"""Real Ollama generation with Spanish UTF-8, not a mock."""
import json,os,subprocess,sys,time,uuid
from pathlib import Path
from ai_bridge import Ollama
from qemu_test import Guest
from qmp_test import QMP
model=os.getenv('OLLAMA_MODEL','qwen2.5:0.5b')
try:Ollama(os.getenv('OLLAMA_URL','http://127.0.0.1:11434'),model).ready()
except Exception as exc:print('BLOCKED: '+str(exc));raise SystemExit(78)
tag=uuid.uuid4().hex[:8];sock=Path('build/ai-utf8-'+tag+'.sock');qmp=Path('build/ai-utf8-qmp-'+tag+'.sock')
bridge=None;monitor=None
prompt='Repite esta frase en español, conservando tildes y signos: ¡Hola, Perú! El año trae programación y pingüinos. ¿Qué tal?'
with Path('artifacts/ai-utf8-bridge.log').open('w') as log:
    try:
        with Guest('ai-utf8-real',['-chardev',f'socket,id=ai,path={sock},server=on,wait=off',
                                  '-serial','chardev:ai','-qmp',f'unix:{qmp},server=on,wait=off']) as g:
            monitor=QMP(qmp)
            bridge=subprocess.Popen([sys.executable,'scripts/ai_bridge.py','--socket',str(sock),'--model',model],stdout=log,stderr=log)
            time.sleep(.2);g.send('ia status');g.until('IA END',10)
            assert b'OLLAMA READY MODEL=' in g.buffer
            g.send('ia '+prompt);g.until('IA END',125)
            text=g.buffer.decode('utf-8')
            assert 'ERROR IA:' not in text and 'IA RESPONSE:' in text
            body=text.split('IA RESPONSE:',1)[1].split('IA END',1)[0].replace('\r','').strip()
            assert body and any(c in body for c in 'áéíóúÁÉÍÓÚñÑüÜ¿¡'),repr(body)
            assert '\ufffd' not in body,repr(body)
            Path('artifacts/ai-utf8-result.json').write_text(json.dumps({
                'type':'REAL_OLLAMA_INFERENCE','model':model,'prompt':prompt,'response':body,
                'strict_utf8_decode':True,'spanish_characters_present':True},ensure_ascii=False,indent=2)+'\n')
            monitor.capture('ia-utf8-framebuffer.png')
            g.send('suma 3 4',['= 7'])
            monitor.close();monitor=None
        print('PASSED: IA REAL UTF-8, pregunta española y respuesta con tildes/eñe/signos, IA END y shell')
    finally:
        if monitor:monitor.close()
        if bridge:
            bridge.terminate()
            try:bridge.wait(timeout=3)
            except subprocess.TimeoutExpired:bridge.kill();bridge.wait()
        sock.unlink(missing_ok=True);qmp.unlink(missing_ok=True)

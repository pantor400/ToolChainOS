#!/usr/bin/env python3
"""Real inference only; exit 78 means BLOCKED, never substitutes mock."""
import os,subprocess,sys,time,uuid
from pathlib import Path
from ai_bridge import Ollama
from qemu_test import Guest
model=os.getenv('OLLAMA_MODEL','')
try:
    backend=Ollama(os.getenv('OLLAMA_URL','http://127.0.0.1:11434'),model)
    backend.ready()
except Exception as exc:
    print('BLOCKED: INFERENCIA REAL OLLAMA: '+str(exc));raise SystemExit(78)
sock=Path('build/ai-real-'+uuid.uuid4().hex[:8]+'.sock')
bridge=None
with Path('artifacts/ai-bridge-real.log').open('w') as log:
    try:
        with Guest('ai-real',['-chardev',f'socket,id=ai,path={sock},server=on,wait=off','-serial','chardev:ai']) as g:
            bridge=subprocess.Popen([sys.executable,'scripts/ai_bridge.py','--socket',str(sock)],stdout=log,stderr=log)
            time.sleep(.2)
            g.send('ia status');g.until('IA END',10);assert b'OLLAMA READY' in g.buffer
            g.send('ia Responde brevemente: que es un kernel de sistema operativo?')
            text=g.until('IA END',125)
            assert 'ERROR IA:' not in text and 'IA RESPONSE:' in text
            assert len(text.split('IA RESPONSE:',1)[1].split('IA END')[0].strip())>0
            g.send('suma 3 4',['= 7'])
        print('PASSED: INFERENCIA REAL ToolchainOS COM2 Linux Ollama modelo='+model)
    finally:
        if bridge is not None:
            bridge.terminate()
            try:bridge.wait(timeout=3)
            except subprocess.TimeoutExpired:bridge.kill();bridge.wait()
        sock.unlink(missing_ok=True)

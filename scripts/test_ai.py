#!/usr/bin/env python3
"""TRANSPORT MOCK ONLY: this test never claims real model inference."""
import json,os,subprocess,sys,threading,time,uuid
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
from pathlib import Path
from ai_bridge import Decoder,frame,Ollama
from qemu_test import Guest
class Handler(BaseHTTPRequestHandler):
    mode='ok'
    def log_message(self,*args):pass
    def do_GET(self):
        self.send_response(200);self.end_headers()
        self.wfile.write(json.dumps({'models':[{'name':'transport-mock'}]}).encode())
    def do_POST(self):
        body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        assert body['stream'] is False and body['model']=='transport-mock'
        if self.mode=='delay':time.sleep(.6)
        if self.mode=='http-error':
            self.send_response(503);self.end_headers();return
        self.send_response(200);self.end_headers()
        if self.mode=='bad-json':self.wfile.write(b'not json');return
        text='TRANSPORT MOCK ONLY: año Perú programación áéíóú ÁÉÍÓÚ ñÑ üÜ ¿Qué? ¡Sí!'
        if self.mode=='long':text='MOCK '+('á'*2000)
        self.wfile.write(json.dumps({'response':text,'done':True}).encode())
def protocol_tests():
    a=frame(1,42,'hola');b=frame(2,43)
    d=Decoder();out=[]
    for byte in b'noise'+a+b:out.extend(d.feed(bytes([byte])))
    assert out==[(1,42,b'hola'),(2,43,b'')]
    bad=bytearray(a);bad[-1]^=1;assert Decoder().feed(bytes(bad)+a)==[(1,42,b'hola')]
    assert Decoder().feed(b'TC\x01\x01\0\0\0\0\xff\xff\0\0'+b)==[(2,43,b'')]
    for url in ['http://0.0.0.0:11434','https://127.0.0.1','http://example.com']:
        try:Ollama(url,'x')
        except ValueError:pass
        else:raise AssertionError(url)
    print('PASSED: framing byte a byte, concatenacion, checksum, longitud y loopback')
protocol_tests()
server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
url=f'http://127.0.0.1:{server.server_port}'
tag=uuid.uuid4().hex[:8];sock=Path('build/ai-test-'+tag+'.sock')
bridge=None
log=Path('artifacts/ai-bridge-mock.log').open('w')
try:
    try:Ollama(url,'missing').ready()
    except ValueError:pass
    else:raise AssertionError('model check')
    with Guest('ai-mock',['-chardev',f'socket,id=ai,path={sock},server=on,wait=off','-serial','chardev:ai']) as g:
        bridge=subprocess.Popen([sys.executable,'scripts/ai_bridge.py','--socket',str(sock),'--url',url,'--model','transport-mock'],stdout=log,stderr=log)
        time.sleep(.15)
        g.send('ia status',['IA REQUEST'])
        g.until('IA END');assert b'OLLAMA READY MODEL=transport-mock' in g.buffer
        g.send('ia prueba transporte',['IA REQUEST'])
        g.until('IA END');assert 'año Perú programación áéíóú ÁÉÍÓÚ ñÑ üÜ ¿Qué? ¡Sí!'.encode() in g.buffer
        g.buffer.decode('utf-8')
        Handler.mode='long';g.send('ia larga',['IA REQUEST'])
        g.until('IA END');assert b'[TRUNCATED]' in g.buffer
        assert 'á'.encode() in g.buffer;g.buffer.decode('utf-8')
        Handler.mode='bad-json';g.send('ia malformed',['IA REQUEST'])
        g.until('IA END');assert b'ERROR IA:' in g.buffer
        Handler.mode='http-error';g.send('ia http-error',['IA REQUEST'])
        g.until('IA END');assert b'HTTPError' in g.buffer
        Handler.mode='delay';g.send('ia cancelar',['IA REQUEST'])
        g.send('ia cancel',['IA CANCELADA']);g.send('suma 7 8',['= 15'])
        time.sleep(.8)
        Handler.mode='ok';g.send('ia status',['IA REQUEST']);g.until('IA END')
        bridge.terminate();bridge.wait(timeout=3);bridge=None
        g.send('ia status',['IA REQUEST']);g.until('IA END',5)
        assert b'TIMEOUT' in g.buffer
        g.send('suma 8 9',['= 17'])
    print('PASSED: COM1/COM2, mock HTTP, status, respuesta, truncamiento UTF8, JSON/HTTP error, cancelacion y desconexion')
finally:
    if bridge is not None:
        bridge.terminate()
        try:bridge.wait(timeout=3)
        except subprocess.TimeoutExpired:bridge.kill();bridge.wait()
    log.close();sock.unlink(missing_ok=True);server.shutdown();server.server_close();thread.join(timeout=2)

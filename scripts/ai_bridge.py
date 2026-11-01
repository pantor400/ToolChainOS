#!/usr/bin/env python3
"""Local COM2 bridge. No third-party dependencies; bounded TC/1 frames."""
import argparse,asyncio,json,os,struct,threading,urllib.request,urllib.parse
MAX=1024
def frame(kind,request,body=b''):
    if isinstance(body,str):body=body.encode('utf-8')
    if len(body)>MAX:
        body=body[:MAX-20].decode('utf-8','ignore').encode()+b' [TRUNCATED]'
    header=bytearray(struct.pack('<2sBBIHH',b'TC',1,kind,request,len(body),0))
    struct.pack_into('<H',header,10,(sum(header[:10])+sum(body))&65535)
    return bytes(header)+body
class Decoder:
    def __init__(self):self.buf=bytearray()
    def feed(self,data):
        self.buf.extend(data);frames=[]
        while len(self.buf)>=2:
            if self.buf[:2]!=b'TC':del self.buf[0];continue
            if len(self.buf)<12:break
            _,version,kind,req,n,checksum=struct.unpack_from('<2sBBIHH',self.buf)
            if version!=1 or n>MAX:del self.buf[0];continue
            if len(self.buf)<12+n:break
            body=bytes(self.buf[12:12+n])
            if (sum(self.buf[:10])+sum(body))&65535!=checksum:del self.buf[0];continue
            del self.buf[:12+n];frames.append((kind,req,body))
        return frames
class Ollama:
    def __init__(self,url,model,timeout=100):
        p=urllib.parse.urlsplit(url)
        if p.scheme!='http' or p.hostname not in ('127.0.0.1','localhost','::1') or p.username or p.password or p.query or p.fragment:
            raise ValueError('OLLAMA_URL debe usar HTTP en loopback')
        self.slots=threading.BoundedSemaphore(2)
        self.url=url.rstrip('/');self.model=model;self.timeout=timeout
        self.opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
    def http(self,path,body=None):
        data=None if body is None else json.dumps(body).encode()
        req=urllib.request.Request(self.url+path,data=data,headers={'Content-Type':'application/json'})
        if not self.slots.acquire(blocking=False):raise ValueError('HTTP OCUPADO: INFERENCIA ANTERIOR AUN ACTIVA')
        try:
            with self.opener.open(req,timeout=1.5 if path=='/api/tags' else self.timeout) as response:
                raw=response.read(1024*1024+1)
        finally:self.slots.release()
        if len(raw)>1024*1024:raise ValueError('RESPUESTA HTTP EXCEDE 1MiB')
        return json.loads(raw)
    def ready(self):
        if not self.model:raise ValueError('OLLAMA_MODEL NO CONFIGURADO')
        models=self.http('/api/tags')
        names=[m.get('name') for m in models.get('models',[]) if isinstance(m,dict)]
        if self.model not in names:raise ValueError('MODELO NO INSTALADO: '+self.model)
        return 'OLLAMA READY MODEL='+self.model
    def generate(self,prompt):
        self.ready()
        result=self.http('/api/generate',{'model':self.model,'prompt':prompt,'stream':False,'options':{'num_predict':192}})
        if not isinstance(result.get('response'),str) or result.get('done') is not True:
            raise ValueError('JSON DE GENERACION INVALIDO/INCOMPLETO')
        return result['response']
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs):raise ValueError('REDIRECCION HTTP NO PERMITIDA')
async def bridge(socket_path,backend):
    reader,writer=await asyncio.open_unix_connection(socket_path)
    print('COM2 CONNECTED; API LOCAL; MODEL='+backend.model,flush=True)
    decoder=Decoder();jobs={};lock=asyncio.Lock()
    async def reply(kind,req,body):
        async with lock:
            packet=frame(kind,req,body)
            # Pace below UART FIFO throughput; kernel polls COM2 without IRQ.
            for pos in range(0,len(packet),8):
                writer.write(packet[pos:pos+8]);await writer.drain();await asyncio.sleep(.003)
    async def job(kind,req,body):
        try:
            print(f'REQUEST id={req} type={kind} bytes={len(body)}',flush=True)
            if kind==2:result=await asyncio.to_thread(backend.ready)
            else:result=await asyncio.to_thread(backend.generate,body.decode('utf-8',errors='strict'))
            await reply(0x82 if kind==2 else 0x81,req,result)
            print(f'RESPONSE id={req} bytes={len(result.encode())}',flush=True)
        except asyncio.CancelledError:
            print(f'CANCEL id={req}',flush=True);raise
        except Exception as exc:
            # No prompt text or secrets in logs.
            await reply(0xff,req,type(exc).__name__+': '+str(exc)[:240])
            print(f'ERROR id={req} type={type(exc).__name__}',flush=True)
        finally:jobs.pop(req,None)
    try:
        while data:=await reader.read(256):
            for kind,req,body in decoder.feed(data):
                if kind==3:
                    if req in jobs:jobs[req].cancel()
                elif kind in (1,2):
                    if jobs:
                        await reply(0xff,req,'PUENTE OCUPADO')
                    else:jobs[req]=asyncio.create_task(job(kind,req,body))
                else:await reply(0xff,req,'TIPO NO SOPORTADO')
    finally:
        for j in list(jobs.values()):j.cancel()
        if jobs:await asyncio.gather(*list(jobs.values()),return_exceptions=True)
        writer.close();await writer.wait_closed()
def main():
    p=argparse.ArgumentParser();p.add_argument('--socket',default='build/ai.sock')
    p.add_argument('--url',default=os.getenv('OLLAMA_URL','http://127.0.0.1:11434'))
    p.add_argument('--model',default=os.getenv('OLLAMA_MODEL',''));a=p.parse_args()
    try:asyncio.run(bridge(a.socket,Ollama(a.url,a.model)))
    except (OSError,ValueError) as exc:raise SystemExit(str(exc))
if __name__=='__main__':main()

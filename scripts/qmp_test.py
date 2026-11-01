"""Local QMP helper for PS/2 injection and real framebuffer captures."""
import json,socket,time
from pathlib import Path
class QMP:
    def __init__(self,path):
        self.socket=socket.socket(socket.AF_UNIX);self.socket.settimeout(5)
        end=time.monotonic()+5
        while True:
            try:self.socket.connect(str(path));break
            except (FileNotFoundError,ConnectionRefusedError):
                if time.monotonic()>end:raise
                time.sleep(.02)
        self.stream=self.socket.makefile('rwb',buffering=0);json.loads(self.stream.readline())
        self.call('qmp_capabilities')
    def call(self,name,args=None):
        request={'execute':name}
        if args is not None:request['arguments']=args
        self.stream.write((json.dumps(request)+'\n').encode())
        while True:
            result=json.loads(self.stream.readline())
            if 'error' in result:raise RuntimeError(result)
            if 'return' in result:return result['return']
    def key(self,*keys):
        self.call('send-key',{'keys':[{'type':'qcode','data':k} for k in keys],'hold-time':20})
        time.sleep(.055)
    def ascii(self,text):
        for c in text:self.key('spc' if c==' ' else c)
    def capture(self,name):
        self.call('screendump',{'filename':str(Path('artifacts',name).resolve()),'format':'png'})
    def close(self):self.stream.close();self.socket.close()

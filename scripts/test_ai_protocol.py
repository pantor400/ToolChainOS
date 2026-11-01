#!/usr/bin/env python3
"""Host-only protocol/API contract tests: no QEMU, sockets or Ollama service."""
import io,json,struct,unittest
from unittest.mock import Mock
from ai_bridge import Decoder,frame,Ollama,NoRedirect
class Response(io.BytesIO):
    def __enter__(self):return self
    def __exit__(self,*args):self.close()
def backend(*responses):
    obj=Ollama('http://127.0.0.1:11434','local-test-model')
    obj.opener=Mock()
    obj.opener.open.side_effect=[Response(r if isinstance(r,bytes) else json.dumps(r).encode()) for r in responses]
    return obj
class ProtocolTests(unittest.TestCase):
    def test_fragmentation(self):
        out=[];decoder=Decoder()
        for b in frame(1,42,'hola'):out+=decoder.feed(bytes([b]))
        self.assertEqual(out,[(1,42,b'hola')])
    def test_multiple_and_noise(self):
        self.assertEqual(Decoder().feed(b'noise'+frame(1,1,'a')+frame(2,2)),[(1,1,b'a'),(2,2,b'')])
    def test_checksum_resync(self):
        bad=bytearray(frame(1,1,'a'));bad[-1]^=1
        self.assertEqual(Decoder().feed(bad+frame(2,2)),[(2,2,b'')])
    def test_invalid_version_and_length(self):
        for version,length in [(2,0),(1,65535)]:
            packet=struct.pack('<2sBBIHH',b'TC',version,1,1,length,0)
            self.assertEqual(Decoder().feed(packet+frame(2,3)),[(2,3,b'')])
    def test_utf8_truncation(self):
        packet=frame(0x81,1,'á'*2000)
        response=Decoder().feed(packet)[0][2]
        self.assertLessEqual(len(response),1024)
        self.assertTrue(response.decode('utf-8').endswith('[TRUNCATED]'))
    def test_url_restrictions(self):
        for url in ['http://0.0.0.0:11434','https://127.0.0.1','http://example.com','http://user:secret@localhost','http://localhost/?secret=x']:
            with self.subTest(url=url),self.assertRaises(ValueError):Ollama(url,'x')
    def test_redirect_rejected(self):
        with self.assertRaises(ValueError):NoRedirect().redirect_request(None,None,None,None,None,None)
    def test_api_contract_mock_only(self):
        obj=backend({'models':[{'name':'local-test-model'}]},{'response':'MOCK CONTRACT ONLY','done':True})
        self.assertEqual(obj.generate('test'),'MOCK CONTRACT ONLY')
        calls=obj.opener.open.call_args_list
        request=calls[1].args[0];body=json.loads(request.data)
        self.assertEqual(request.full_url,'http://127.0.0.1:11434/api/generate')
        self.assertEqual(body['model'],'local-test-model')
        self.assertIs(body['stream'],False)
        self.assertEqual(body['prompt'],'test')
        self.assertEqual(calls[0].kwargs['timeout'],1.5)
    def test_model_missing(self):
        with self.assertRaises(ValueError):backend({'models':[]}).ready()
    def test_json_error(self):
        with self.assertRaises(json.JSONDecodeError):backend(b'invalid').http('/api/tags')
    def test_invalid_generation(self):
        obj=backend({'models':[{'name':'local-test-model'}]},{'response':'incomplete','done':False})
        with self.assertRaises(ValueError):obj.generate('test')
    def test_size_limit(self):
        with self.assertRaises(ValueError):backend(b'x'*(1024*1024+1)).http('/api/tags')
    def test_concurrency_bound(self):
        obj=backend()
        self.assertTrue(obj.slots.acquire(False));self.assertTrue(obj.slots.acquire(False))
        try:
            with self.assertRaises(ValueError):obj.http('/api/tags')
            obj.opener.open.assert_not_called()
        finally:obj.slots.release();obj.slots.release()
if __name__=='__main__':unittest.main(verbosity=2)

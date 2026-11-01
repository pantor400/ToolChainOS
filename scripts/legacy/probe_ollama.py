#!/usr/bin/env python3
import json,os,shutil,urllib.request
from pathlib import Path
result={'executable':shutil.which('ollama'),'configured_model':os.getenv('OLLAMA_MODEL'),'status':'BLOCKED'}
try:
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open('http://127.0.0.1:11434/api/tags',timeout=3) as r:data=json.load(r)
    result['models']=[m['name'] for m in data.get('models',[])]
    result['status']='AVAILABLE' if result['models'] else 'BLOCKED'
except Exception as exc:result['reason']=str(exc)
Path('artifacts/ollama-probe.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))

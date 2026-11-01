#!/usr/bin/env python3
import os,signal,subprocess,time,json,hashlib,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
EV=ROOT/'artifacts/etapas56-20261029'
QA=ROOT/'build/qa-etapas56'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for d in ('src','include','scripts','vendor'):
 shutil.copytree(ROOT/d,QA/d,dirs_exist_ok=True,ignore=shutil.ignore_patterns('*:Zone.Identifier','__pycache__'))
for f in ('Makefile','linker.ld','limine.conf'):shutil.copyfile(ROOT/f,QA/f)
steps=[
 ('build',['make','iso','check']),
 ('memory-host',['make','test-memory-host']),
 ('scheduler-host',['make','test-scheduler-host']),
 ('memory-scheduler-qemu',['python3','-B','scripts/test_memory_scheduler.py']),
 ('utf8-host',['make','test-utf8-host']),
 ('fs-host',['sh','-c','gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -Iinclude -Ivendor/limine scripts/test_fs_host.c src/fs.c -o build/test-fs-host && build/test-fs-host']),
 ('ai-protocol-host',['python3','-B','scripts/test_ai_protocol.py']),
 ('legacy',['python3','-B','scripts/smoke_test.py']),
 ('files-editor',['python3','-B','scripts/test_files_editor.py']),
 ('core',['python3','-B','scripts/test_core.py']),
 ('utf8-keyboard',['python3','-B','scripts/test_utf8_keyboard.py']),
 ('persistence',['python3','-B','scripts/test_persistence.py']),
 ('utf8-persistence',['python3','-B','scripts/test_utf8_persistence.py']),
 ('uefi',['python3','-B','scripts/test_uefi.py']),
 ('ai-transport-mock',['python3','-B','scripts/test_ai.py']),
 ('ai-real',['python3','-B','scripts/test_ai_real.py']),
 ('ai-utf8-real',['python3','-B','scripts/test_ai_utf8.py']),
 ('launcher',['python3','-B','scripts/test_launcher.py']),
 ('presentation',['python3','-B','scripts/test_presentation.py'])]
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--from-step');args=parser.parse_args()
index=next((i for i,(name,_) in enumerate(steps) if name==args.from_step),0)
results=json.loads((EV/'results.json').read_text())['results'][:index] if index else []
for name,cmd in steps[index:]:
 existing=[p.name for p in Path('/proc').iterdir() if p.name.isdigit() and (p/'comm').exists() and (p/'comm').read_text().startswith('qemu-system')]
 if existing:raise RuntimeError('QEMU already active: '+repr(existing))
 start=time.monotonic();log=EV/(name+'-final.log')
 with log.open('w') as out:
  proc=subprocess.Popen(cmd,cwd=QA,stdout=out,stderr=subprocess.STDOUT,start_new_session=True,env={**os.environ,'OLLAMA_MODEL':'qwen2.5:0.5b'})
  try:code=proc.wait(timeout=240)
  except subprocess.TimeoutExpired:
   os.killpg(proc.pid,signal.SIGTERM)
   try:proc.wait(timeout=3)
   except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
   code=124
 status={0:'PASSED',77:'SKIPPED',78:'BLOCKED'}.get(code,'FAILED')
 results.append({'name':name,'status':status,'code':code,'seconds':round(time.monotonic()-start,2),'log':log.name})
 report={'results':results,'counts':{s:sum(r['status']==s for r in results) for s in ['PASSED','FAILED','BLOCKED','SKIPPED']}}
 protected=json.loads((EV/'protegidos.json').read_text())
 report['protected_unchanged']=all(sha(ROOT/p)==h for p,h in protected.items())
 (EV/'results.json').write_text(json.dumps(report,indent=2))
 print(name,status,flush=True)
 if code not in (0,77,78):break
shutil.copytree(QA/'artifacts',EV/'evidencias',dirs_exist_ok=True)
print(json.dumps(report['counts']))

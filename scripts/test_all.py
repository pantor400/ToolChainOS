#!/usr/bin/env python3
"""Reproducible results; missing optional components never count as passed."""
import datetime,json,os,shutil,subprocess,time
from pathlib import Path
# Misma variable que el Makefile: SAN= desactiva los sanitizers del anfitrion.
san=['-fsanitize='+os.environ['SAN']] if os.environ.get('SAN','address,undefined').strip() else []
steps=[
 ('build',[['make','iso']],['gcc','nasm','ld','xorriso']),
 ('elf',[['make','check']],['python3']),
 ('memory-host',[['make','test-memory-host']],['gcc']),
 ('scheduler-host',[['make','test-scheduler-host']],['gcc','nasm']),
 ('memory-scheduler-qemu',[['python3','scripts/test_memory_scheduler.py']],['qemu-system-x86_64']),
 ('fs-host',[['gcc','-std=c11','-Wall','-Wextra','-Werror',*san,'-Iinclude','-Ivendor/limine','scripts/test_fs_host.c','src/fs.c','-o','build/test-fs-host'],['build/test-fs-host']],['gcc']),
 ('utf8-host',[['make','test-utf8-host']],['gcc']),
 ('utf8-keyboard',[['python3','scripts/test_utf8_keyboard.py']],['qemu-system-x86_64']),
 ('legacy',[['python3','scripts/smoke_test.py']],['qemu-system-x86_64']),
 ('files-editor',[['python3','scripts/test_files_editor.py']],['qemu-system-x86_64']),
 ('core',[['python3','scripts/test_core.py']],['qemu-system-x86_64']),
 ('gui',[['python3','scripts/test_gui.py']],['qemu-system-x86_64']),
 ('persistence',[['python3','scripts/test_persistence.py']],['qemu-system-x86_64']),
 ('utf8-persistence',[['python3','scripts/test_utf8_persistence.py']],['qemu-system-x86_64']),
 ('ai-transport-mock',[['python3','scripts/test_ai.py']],['qemu-system-x86_64']),
 ('ai-agent',[['python3','scripts/test_agent.py']],['qemu-system-x86_64']),
 ('uefi',[['python3','scripts/test_uefi.py']],['qemu-system-x86_64']),
 ('ai-utf8-real',[['python3','scripts/test_ai_utf8.py']],['qemu-system-x86_64']),
 ('ai-real',[['python3','scripts/test_ai_real.py']],['qemu-system-x86_64'])]
Path('artifacts').mkdir(exist_ok=True);results=[]
for name,commands,deps in steps:
    missing=[d for d in deps if not shutil.which(d)]
    start=time.monotonic();code=0;log=Path('artifacts/suite-'+name+'.log')
    if missing:
        status='BLOCKED';log.write_text('Missing: '+', '.join(missing))
    else:
        with log.open('w') as out:
            for command in commands:
                out.write('$ '+repr(command)+'\n');out.flush()
                try:code=subprocess.run(command,stdout=out,stderr=subprocess.STDOUT,timeout=240).returncode
                except subprocess.TimeoutExpired:out.write('TIMEOUT\n');code=124
                if code:break
        status={0:'PASSED',77:'SKIPPED',78:'BLOCKED'}.get(code,'FAILED')
    results.append({'name':name,'status':status,'seconds':round(time.monotonic()-start,2),'log':str(log)})
    print(status+': '+name,flush=True)
counts={s:sum(r['status']==s for r in results) for s in ('PASSED','FAILED','SKIPPED','BLOCKED')}
report={'time':datetime.datetime.now(datetime.timezone.utc).isoformat(),'counts':counts,'results':results}
Path('artifacts/results.json').write_text(json.dumps(report,indent=2))
print(json.dumps(counts))
if counts['BLOCKED']:print('INCOMPLETE: componentes bloqueados detallados en artifacts/results.json')
raise SystemExit(1 if counts['FAILED'] else 0)

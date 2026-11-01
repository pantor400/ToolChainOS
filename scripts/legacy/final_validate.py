#!/usr/bin/env python3
"""Sequential final acceptance. Stops on first failure; preserves existing images."""
import argparse,datetime,hashlib,json,os,signal,subprocess,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def qemu_processes():
    out=[]
    for path in Path('/proc').iterdir():
        if not path.name.isdigit():continue
        try:
            name=(path/'comm').read_text().strip()
            if name.startswith(('qemu-system','qemu-kvm')):out.append(int(path.name))
        except (OSError,ProcessLookupError):pass
    return out
def atomic(path,data):
    tmp=path.with_name(path.name+'.pending')
    with tmp.open('w') as f:f.write(json.dumps(data,indent=2));f.flush();os.fsync(f.fileno())
    tmp.replace(path)
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--evidence-dir',required=True);args=parser.parse_args()
    os.chdir(ROOT);evidence=Path(args.evidence_dir).resolve()
    if evidence.parent!=ROOT/'artifacts' or not (evidence/'discos-previos.json').exists():raise SystemExit('Directorio de evidencia no autorizado')
    before=json.loads((evidence/'discos-previos.json').read_text())
    assert not qemu_processes(),'QEMU anterior activo'
    assert (evidence/'first-boot.log').read_text().startswith('PASSED:')
    results=[{'name':'first-boot','status':'PASSED','log':'first-boot.log'}]
    commands=[
      ('build',[['make','iso']]),('elf',[['make','check']]),
      ('fs-host',[['gcc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-Iinclude','-Ivendor/limine','scripts/test_fs_host.c','src/fs.c','-o','build/test-fs-host'],['build/test-fs-host']]),
      ('ai-protocol-host',[['python3','-B','scripts/test_ai_protocol.py']]),
      ('legacy',[['python3','-B','scripts/smoke_test.py']]),
      ('files-editor',[['python3','-B','scripts/test_files_editor.py']]),
      ('core',[['python3','-B','scripts/test_core.py']]),
      ('uefi',[['python3','-B','scripts/test_uefi.py']]),
      ('persistence',[['python3','-B','scripts/test_persistence.py']]),
      ('ai-transport-mock',[['python3','-B','scripts/test_ai.py']]),
      ('ollama-availability',[['python3','-B','scripts/probe_ollama.py']]),
      ('ai-real',[['python3','-B','scripts/test_ai_real.py']]),
      ('capture',[['python3','-B','scripts/capture_demo.py']]),
      ('launcher',[['python3','-B','scripts/test_launcher.py']]),
      ('static-artifacts',[['python3','-B','scripts/audit_static.py']])]
    def record():
        after={name:digest(ROOT/name) for name in before}
        unchanged=after==before
        report={'time_UTC':datetime.datetime.now(datetime.timezone.utc).isoformat(),'results':results,
          'counts':{s:sum(r['status']==s for r in results) for s in ('PASSED','FAILED','SKIPPED','BLOCKED')},
          'existing_images_unchanged':unchanged,'images_before':before,'images_after':after,'qemu_processes_after':qemu_processes()}
        atomic(evidence/'results.json',report)
        if not unchanged:raise RuntimeError('STOP: imagen preexistente alterada')
    for name,cmds in commands:
        assert not qemu_processes(),'STOP: QEMU anterior no termino'
        start=time.monotonic();code=0;env=dict(os.environ)
        if name=='ai-real' and not env.get('OLLAMA_MODEL'):
            probe=json.loads(Path('artifacts/ollama-probe.json').read_text())
            models=probe.get('models',[])
            if len(models)==1:env['OLLAMA_MODEL']=models[0]
        with (evidence/(name+'.log')).open('w') as out:
            for cmd in cmds:
                out.write('$ '+repr(cmd)+'\n');out.flush()
                p=subprocess.Popen(cmd,stdout=out,stderr=subprocess.STDOUT,env=env,start_new_session=True)
                try:code=p.wait(timeout=200)
                except subprocess.TimeoutExpired:
                    os.killpg(p.pid,signal.SIGTERM)
                    try:p.wait(timeout=3)
                    except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
                    out.write('TIMEOUT: proceso y grupo terminados; no reintento automatico\n');code=124
                if code:break
        status={0:'PASSED',77:'SKIPPED',78:'BLOCKED'}.get(code,'FAILED')
        results.append({'name':name,'status':status,'exit_code':code,'seconds':round(time.monotonic()-start,2),'log':name+'.log'})
        record();print(status+': '+name,flush=True)
        if status=='FAILED' or qemu_processes():raise SystemExit('STOP: no se inician mas pruebas')
    record()
    print('VALIDACION TERMINADA: '+str(evidence/'results.json'),flush=True)
if __name__=='__main__':main()

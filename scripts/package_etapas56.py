#!/usr/bin/env python3
"""Curated independent E56 delivery. Existing images and releases are read-only."""
from pathlib import Path
from datetime import datetime
from zoneinfo import ZoneInfo
import hashlib,json,shutil,zipfile
ROOT=Path(__file__).resolve().parents[1]
EV=ROOT/'artifacts/etapas56-20261029'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    results=json.loads((EV/'results.json').read_text())
    assert results['counts']=={'PASSED':19,'FAILED':0,'BLOCKED':0,'SKIPPED':0}
    protected=json.loads((EV/'protegidos.json').read_text())
    assert all(sha(ROOT/p)==h for p,h in protected.items())
    stamp=datetime.now(ZoneInfo('America/Lima')).strftime('%Y%m%d-%H%M%S')
    out=ROOT/'entrega'/('ToolchainOS-V3.1-E56-'+stamp);out.mkdir(exist_ok=False)
    def copy(p,rel=None):
        target=out/(rel if rel else p.relative_to(ROOT));target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
    for d in ('src','include','vendor/limine','scripts'):
        for p in sorted((ROOT/d).glob('*')):
            if p.is_file() and ':Zone.Identifier' not in p.name:copy(p)
    for p in sorted((ROOT/'docs').rglob('*.md')):copy(p)
    copy(ROOT/'docs/informes/INFORME_ACADEMICO_ETAPAS56.docx')
    for p in sorted((ROOT/'docs/etapas56-diagramas').glob('*.png')):copy(p)
    for f in ['Makefile','linker.ld','limine.conf','README.md','LEEME_ENTREGA.md','CHANGELOG.md','LICENSE','INICIAR_DEMO_LINUX.sh','INICIAR_DEMO_WINDOWS.bat','build/kernel.elf','build/toolchain-os.iso']:
        copy(ROOT/f)
    for p in sorted(EV.rglob('*')):
        if p.is_file() and 'docx-qa' not in p.parts and p.name!='delivery.json':copy(p)
    # A newly created blank image only; existing user data is NEVER packaged.
    import sys
    sys.path.insert(0,str(ROOT/'scripts'))
    from create_test_disk import create
    new=create('build/entrega-e56-vacia-'+stamp+'.img')
    copy(new,Path('build/data.img'))
    (out/'LEEME_PRIMERO.txt').write_text(
      'ToolchainOS V3.1 E56\nInforme: docs/informes/INFORME_ACADEMICO_ETAPAS56.docx\nGuia: docs/guias/GUIA_DEMOSTRACION_ETAPAS56.md\nISO: build/toolchain-os.iso\nResultados: artifacts/etapas56-20261029/results.json\nProcesos kernel sin aislamiento. Round Robin cooperativo, no preventivo.\nUbuntu/WSL: python3 scripts/run_vm.py --headless\nEl disco incluido es NUEVO y VACIO.\n',encoding='utf-8')
    manifest={str(p.relative_to(out)):sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
    (out/'MANIFEST_SHA256.json').write_text(json.dumps(manifest,indent=2))
    (out/'SHA256SUMS').write_text(''.join(h+'  '+n+'\n' for n,h in manifest.items()))
    archive=out.parent/(out.name+'.zip')
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for p in sorted(out.rglob('*')):
            if p.is_file():z.write(p,arcname=out.name+'/'+str(p.relative_to(out)))
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        for path,h in manifest.items():assert hashlib.sha256(z.read(out.name+'/'+path)).hexdigest()==h
    assert sha(out/'build/toolchain-os.iso')==sha(ROOT/'build/qa-etapas56/build/toolchain-os.iso')
    assert all(sha(ROOT/p)==h for p,h in protected.items())
    record={'directory':str(out),'zip':str(archive),'zip_sha256':sha(archive),'manifest_files':len(manifest),'protected_unchanged':True,'iso_matches_tested':True}
    archive.with_name(archive.name+'.sha256').write_text(record['zip_sha256']+'  '+archive.name+'\n')
    (EV/'delivery.json').write_text(json.dumps(record,indent=2))
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()

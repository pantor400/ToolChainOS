#!/usr/bin/env python3
"""Create a curated new delivery directory and ZIP; never copy existing disks."""
import argparse,datetime,hashlib,json,shutil,sys,zipfile
from pathlib import Path
from zoneinfo import ZoneInfo
from create_test_disk import create
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--evidence-dir',required=True);a=p.parse_args()
    evidence=(ROOT/a.evidence_dir).resolve()
    if evidence.parent!=ROOT/'artifacts':raise SystemExit('Evidence must be below artifacts/')
    results=json.loads((evidence/'results.json').read_text())
    assert results['counts']['FAILED']==0 and results['existing_images_unchanged']
    for name,value in results['images_before'].items():assert sha(ROOT/name)==value
    stamp=datetime.datetime.now(ZoneInfo('America/Lima')).strftime('%Y%m%d-%H%M%S')
    release=ROOT/'entrega'/('ToolchainOS-V31-'+stamp);release.mkdir(parents=True,exist_ok=False)
    def copy(source,target=None):
        source=Path(source)
        assert source.is_file() and not source.is_symlink(),str(source)
        dest=release/(target if target else source.relative_to(ROOT))
        dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
    for folder in ['src','include','vendor/limine']:
        for path in sorted((ROOT/folder).glob('*')):
            if path.is_file() and ':Zone.Identifier' not in path.name:copy(path)
    scripts=['ai_bridge.py','audit_static.py','capture_demo.py','check_elf.py','create_test_disk.py',
        'debug.gdb','probe_ollama.py','qemu_test.py','run_vm.py','setup-ubuntu.sh','smoke_test.py',
        'test_ai.py','test_ai_protocol.py','test_ai_real.py','test_all.py','test_boot.py',
        'test_core.py','test_files_editor.py','test_fs_host.c','test_launcher.py',
        'test_persistence.py','test_uefi.py','test_presentation.py','qmp_test.py',
        'test_utf8_host.c','test_utf8_keyboard.py','test_utf8_persistence.py','test_ai_utf8.py','package_release.py']
    for name in scripts:copy(ROOT/'scripts'/name)
    docs=['docs/diseno/ARQUITECTURA.md','docs/diseno/DECISIONES_TECNICAS.md','docs/validacion/DIAGNOSTICO_INICIAL.md',
        'docs/guias/ENTREGA_DOCENTE.md','docs/diseno/FASES_IMPLEMENTADAS.md','docs/guias/GUIA_DEMOSTRACION.md',
        'docs/informes/INFORME_FINAL.md','docs/validacion/PRUEBAS_Y_RESULTADOS.md','docs/guias/TECLADO_UTF8.md']
    for name in docs:copy(ROOT/'docs'/name)
    for name in ['Makefile','README.md','LICENSE','CHANGELOG.md','linker.ld','limine.conf',
                 'INICIAR_DEMO_LINUX.sh','INICIAR_DEMO_WINDOWS.bat',
                 'build/kernel.elf','build/toolchain-os.iso']:
        copy(ROOT/name)
    for path in sorted(evidence.glob('*')):
        if path.is_file() and path.suffix in ['.log','.json'] and path.name not in ['package.log','delivery.json']:
            copy(path,Path('artifacts/final-validation')/path.name)
    for path in sorted((evidence/'evidencias').glob('*')):
        if path.is_file():copy(path,Path('artifacts/final-validation/evidencias')/path.name)
    fresh=create('build/entrega-vacia-'+stamp+'.img')
    with fresh.open('rb') as f:
        f.seek(1024);assert not any(f.read()),'New delivery disk must have no user content'
    copy(fresh,Path('build/data.img'))
    copy(ROOT/'LEEME_ENTREGA.md')
    manifest={str(p.relative_to(release)):sha(p) for p in sorted(release.rglob('*')) if p.is_file()}
    (release/'MANIFEST_SHA256.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (release/'SHA256SUMS').write_text('\n'.join(v+'  '+k for k,v in manifest.items())+'\n')
    zip_path=release.with_suffix('.zip')
    with zipfile.ZipFile(zip_path,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for path in sorted(release.rglob('*')):
            if path.is_file():archive.write(path,arcname=str(Path(release.name)/path.relative_to(release)))
    with zipfile.ZipFile(zip_path) as archive:assert archive.testzip() is None
    for name,value in results['images_before'].items():assert sha(ROOT/name)==value
    record={'directory':str(release.relative_to(ROOT)),'zip':str(zip_path.relative_to(ROOT)),
      'zip_sha256':sha(zip_path),'files':len(manifest)+2,
      'new_blank_disk_source':str(fresh.relative_to(ROOT)),'existing_disks_unchanged':True,
      'omitted':['backups','recovery archives','preexisting user disks','.idea','.vscode','Zone.Identifier','temporary and intermediate files']}
    (evidence/'delivery.json').write_text(json.dumps(record,indent=2)+'\n')
    zip_path.with_name(zip_path.name+'.sha256').write_text(record['zip_sha256']+'  '+zip_path.name+'\n')
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()

#!/usr/bin/env python3
"""Report only the controlled host/static recovery, never reclassify VM history."""
import ast,datetime,hashlib,json,os,re,tarfile,tempfile
from pathlib import Path
from zoneinfo import ZoneInfo
ROOT=Path(__file__).resolve().parents[1]
REC=ROOT/'artifacts/recuperacion-20261026T050714Z'
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def atomic(path,text):
    path=Path(path)
    fd,tmp=tempfile.mkstemp(prefix=path.name+'.',suffix='.tmp',dir=path.parent)
    with os.fdopen(fd,'w',encoding='utf-8') as f:
        f.write(text);f.flush();os.fsync(f.fileno())
    os.replace(tmp,path)
def main():
    previous_manifest=json.loads((REC/'manifest-previo.json').read_text())
    for name,entry in previous_manifest.items():
        if name.startswith(('src/','include/')) or name=='artifacts/results.json':
            assert digest(ROOT/name)==entry['sha256'],'FUENTE O HISTORIAL CAMBIADO: '+name
    before=json.loads((REC/'discos-previos.json').read_text())
    disks={name:digest(ROOT/name) for name in before}
    assert disks==before,'DISCOS MODIFICADOS: no emitir informe exitoso'
    stages=[
      ('build','build.log'),('elf-check','elf-check.log'),('inspect','inspect.log'),
      ('fs-host-asan-ubsan','fs-host.log'),('ai-protocol-host','ai-protocol-host.log'),
      ('hardening-check','hardening-check.log'),('static-artifacts','static-audit.log'),
      ('shell-syntax','shell-syntax.log')]
    results=[]
    for name,log in stages:
        p=REC/log;assert p.exists() and p.stat().st_size>0,str(p)
        text=p.read_text()
        assert not re.search(r'FAILED|Traceback|SyntaxError|AddressSanitizer:|runtime error:',text),str(p)
        results.append({'name':name,'status':'PASSED','log':str(p.relative_to(ROOT))})
    text=(REC/'ai-protocol-host.log').read_text()
    assert re.search(r'Ran 13 tests',text) and text.rstrip().endswith('OK')
    assert (ROOT/'build/kernel.elf').stat().st_size>0 and (ROOT/'build/toolchain-os.iso').stat().st_size>0
    now=datetime.datetime.now(ZoneInfo('America/Lima')).isoformat()
    report={'time_America_Lima':now,'scope':'recovery-host-static-only',
      'counts':{'PASSED':len(results),'FAILED':0},'results':results,
      'vm_tests':'NOT_RUN_PENDING_USER_AUTHORIZATION','ollama':'NOT_RUN',
      'kernel_sources_unchanged':True,'historical_results_unchanged':True,
      'disk_sha256_before':before,'disk_sha256_after':disks,
      'previous_results':'artifacts/results.json (historical, predates final changes)'}
    atomic(ROOT/'artifacts/results-recovery.json',json.dumps(report,indent=2)+'\n')
    # Preserve original VM summaries. Empty logs get explicit provenance notes, never invented stdout.
    atomic(ROOT/'artifacts/test-all.log',
      'NOTA DE RECUPERACION 2026-10-26: este archivo estaba vacio.\n'
      'No se recupero su salida original y NO se ejecuto test-all en esta recuperacion.\n'
      'Resumen historico conservado: artifacts/results.json (9 PASSED, 0 FAILED, 1 BLOCKED).\n'
      'Validacion actual limitada a anfitrion/estatica: artifacts/results-recovery.json.\n')
    independent=(ROOT/'artifacts/persistence.log').read_text()
    atomic(ROOT/'artifacts/suite-persistence.log',
      'RECUPERACION DOCUMENTAL; no es stdout original de la suite.\n'
      'El archivo estaba vacio y se preservo asi en estado-previo.tar.gz.\n'
      'Evidencia ANTERIOR copiada de artifacts/persistence.log; no hubo nueva VM:\n'+independent+
      'Transcripciones anteriores: persist-boot1/2/3.serial.txt y persist-write-error.serial.txt.\n')
    hashes=[]
    for name in ['build/kernel.elf','build/toolchain-os.iso']:
        hashes.append(digest(ROOT/name)+'  '+name)
    atomic(ROOT/'artifacts/SHA256SUMS.actual','\n'.join(hashes)+'\n')
    sizes={name:(ROOT/name).stat().st_size for name in ['build/kernel.elf','build/ai_serial.o','build/pmm.o','build/test-fs-host','build/toolchain-os.iso']}
    lines=['# Recuperación controlada — 2026-10-26','',
      'Instante local: '+now+'. Solo proyecto ToolchainOS. Sin QEMU, otras VMs,',
      'Ollama, sudo, instalaciones, make clean ni eliminación de datos.','',
      '## Causa y alcance',
      'La causa exacta del fallo de Windows/almacenamiento no puede demostrarse con',
      'los registros disponibles del proyecto. Archivos de salida quedaron en cero',
      'mientras la ISO y su kernel en staging contenían datos completos. Es compatible',
      'con truncamientos/escrituras no persistidas por la interrupción; no demuestra',
      'un defecto en las fuentes C/NASM ni permite atribuirlo a un componente concreto.',
      'Las redirecciones de logs y la apertura de salidas truncan antes de escribir.',
      'Se evita atribuir causalidad definitiva sin registros del sistema anfitrión.','',
      '## Preservación',
      'Estado anterior: '+str(REC.relative_to(ROOT))+'/estado-previo.tar.gz.',
      'Manifest de archivos y hashes: manifest-previo.json; discos: discos-previos.json.',
      'Las carpetas ToolchainOS_RESPALDO_ORIGINAL y ToolchainOS_RECUPERACION no se tocaron.',
      'build/data.img y la otra imagen preexistente se conservaron byte por byte.','',
      '## Artefactos regenerados',
      '| Archivo | Bytes actuales | Procedimiento |','|---|---:|---|']
    for name,size in sizes.items():
        lines.append('| '+name+' | '+str(size)+' | '+('GCC host + ASan/UBSan' if name.endswith('test-fs-host') else 'make -B iso')+' |')
    lines+=['','Se forzó la reconstrucción de los objetos sin limpiar build/. Se ejecutaron',
      'make check y make inspect. El kernel de build, staging y el embebido en la ISO',
      'son idénticos; la revisión estática no equivale a una prueba de arranque.','',
      '## Scripts y registros',
      'artifacts/final_hardening.py estaba vacío. Se reconstruyó como verificador',
      'de solo lectura de los cambios ya presentes; no es una recuperación forense',
      'idéntica ni reaplica modificaciones a fuentes.',
      'artifacts/final_report.py contenía secuencias literales \\n que producían',
      'SyntaxError. Se reemplazó por entrada al generador de recuperación actual.',
      'Los logs vacíos test-all.log y suite-persistence.log no se pueden recuperar',
      'fielmente: ahora contienen notas de procedencia, sin inventar ejecuciones.',
      'La evidencia histórica independiente de persistencia y sus transcripciones',
      'se conservó. results.json sigue siendo el resumen anterior, sin alterarlo.','',
      '## Verificaciones realizadas',
      '| Verificación | Estado | Registro |','|---|---|---|']
    for r in results:lines.append('| '+r['name']+' | PASSED | '+r['log']+' |')
    lines+=['','El protocolo IA tiene 13 pruebas unitarias en el anfitrión: enmarcado,',
      'fragmentación, paquetes múltiples, checksum, longitud/versión, UTF8, límites,',
      'restricción loopback y contrato JSON mediante objetos mock, sin sockets ni',
      'servicio/modelo Ollama. FS usa el test C existente con ASan/UBSan.',
      'No se modificaron las fuentes del kernel durante esta recuperación.','',
      '## Pruebas pendientes',
      'La última revisión compilada necesita autorización para revalidación en QEMU:',
      'BIOS/UEFI, regresión serial, archivos/editor, PMM, IDT/timer/tareas, ATA y',
      'persistencia con imágenes desechables, COM2/mock y captura de framebuffer.',
      'La inferencia real con Ollama sigue sin validación; no se inició ni consultó',
      'el servicio ahora. Su ausencia corresponde al registro histórico, no a un',
      'sondeo nuevo. No se reinició el desarrollo desde la fase 0.']
    atomic(ROOT/'docs/validacion/RECUPERACION_2026-10-26.md','\n'.join(lines)+'\n')
    summary=['# Pruebas y resultados','',
      '## Estado actual: recuperación del 26 de octubre de 2026',
      'La revisión actual recompila y verifica estáticamente; NO ha arrancado QEMU',
      'ni Ollama. '+str(len(results))+' grupos host/estáticos PASSED, 0 FAILED.',
      'Detalle: docs/validacion/RECUPERACION_2026-10-26.md y artifacts/results-recovery.json.',
      'El kernel/ISO actuales no tienen aún aceptación de ejecución tras el apagado.','',
      '## Evidencia anterior conservada',
      'artifacts/results.json registra 9 grupos PASSED, 0 FAILED, 0 SKIPPED y',
      '1 BLOCKED (Ollama real) antes de los últimos cambios. No se reclasifica',
      'esa evidencia como prueba de esta compilación.',
      'Registros: artifacts/suite-*.log, persistence.log, core.log, ai.log y',
      'transcripciones *.serial.txt. El registro agregado de persistencia se perdió;',
      'su archivo actual explica la procedencia de la evidencia independiente.',
      'La captura arranque-actual.png es anterior a la recuperación.','',
      '## Alcance por fase',
      'Fases 0–6: código implementado; ejecución histórica documentada.',
      'FS: además, prueba host con sanitizadores superada durante recuperación.',
      'Fase 7: protocolo y puente implementados; 13 pruebas host superadas,',
      'transporte COM2 histórico simulado superado, inferencia real sin verificar.','',
      '## Artefactos y límites',
      'build/kernel.elf y build/toolchain-os.iso regenerados, no vacíos, ELF/ISO',
      'coherentes; hashes actuales en artifacts/SHA256SUMS.actual.',
      'Las imágenes preexistentes no se modificaron; hashes antes/después en',
      'artifacts/results-recovery.json. Respaldo del estado previo dentro de artifacts/.',
      'PMM limitado a 4GiB; tareas cooperativas kernel sin aislamiento; ATA PIO',
      'legacy/LBA28; sin IST para garantizar manejo de doble fallo.',
      'VM distinta de WSL, hardware físico e inferencia real continúan pendientes.']
    atomic(ROOT/'docs/validacion/PRUEBAS_Y_RESULTADOS.md','\n'.join(summary)+'\n')
    changed=[]
    old={}
    with tarfile.open(ROOT/'artifacts/baseline/source-original.tar.gz') as archive:
        for item in archive:
            if item.isfile():old[item.name]=archive.extractfile(item).read()
    paths=[]
    for d in ['src','include','scripts','docs']:
        paths += [p for p in (ROOT/d).glob('*') if p.is_file() and ':Zone.Identifier' not in p.name]
    paths += [ROOT/n for n in ['Makefile','README.md','CHANGELOG.md','.gitignore','INICIAR_DEMO_LINUX.sh','INICIAR_DEMO_WINDOWS.bat']]
    for p in paths:
        name=str(p.relative_to(ROOT))
        if p.read_bytes()!=old.get(name):
            changed.append({'path':name,'change':'modified' if name in old else 'new-or-not-in-baseline-archive','sha256':digest(p)})
    atomic(ROOT/'artifacts/changed-files.json',json.dumps(changed,indent=2)+'\n')
    print('PASSED: informes y hashes actuales; discos preexistentes identicos.')
    print(json.dumps(sizes))
if __name__=='__main__':main()

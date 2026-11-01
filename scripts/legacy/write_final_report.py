#!/usr/bin/env python3
"""Final reports derive from actual validation records, not historical summaries."""
import argparse,datetime,hashlib,json
from pathlib import Path
from zoneinfo import ZoneInfo
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--evidence-dir',required=True);args=parser.parse_args()
    evidence=(ROOT/args.evidence_dir).resolve();assert evidence.parent==ROOT/'artifacts'
    results=json.loads((evidence/'results.json').read_text())
    version=json.loads((evidence/'version.json').read_text())
    assert results['counts']['FAILED']==0 and results['existing_images_unchanged'] and not results['qemu_processes_after']
    assert sha(ROOT/'build/kernel.elf')==version['kernel_sha256']
    assert sha(ROOT/'build/toolchain-os.iso')==version['iso_sha256']
    for name,value in version['inputs'].items():assert sha(ROOT/name)==value,name
    for name,value in results['images_before'].items():assert sha(ROOT/name)==value
    for r in results['results']:assert (evidence/r['log']).stat().st_size>0,r['log']
    probe=json.loads((ROOT/'artifacts/ollama-probe.json').read_text())
    rel=str(evidence.relative_to(ROOT));now=datetime.datetime.now(ZoneInfo('America/Lima')).isoformat()
    lines=['# Pruebas y resultados finales — 2026-10-26','',
      'Cierre local: '+now+'. Partida: estado recuperado, sin reiniciar el desarrollo.',
      'Evidencia actual: '+rel+'. En el paquete: artifacts/final-validation/.',
      'No se utilizan resultados históricos como aceptación de esta revisión.','',
      '## Resultado actual',
      '15 grupos PASSED, 0 FAILED, 0 SKIPPED, 1 BLOCKED.',
      'Son grupos de compilación, inspección, disponibilidad y pruebas funcionales,',
      'no un recuento de aserciones ni evidencia de IA real funcionando.','',
      '| Grupo | Estado | Registro |','|---|---|---|']
    assert results['counts']=={'PASSED':15,'FAILED':0,'SKIPPED':0,'BLOCKED':1}
    for r in results['results']:lines.append('| '+r['name']+' | '+r['status']+' | '+r['log']+' |')
    lines+=['','## Procedimiento y seguridad',
      'Se comprobó ausencia de VMs anteriores. Primero scripts/test_boot.py; después',
      'scripts/final_validate.py --evidence-dir '+rel+' y test_launcher.py aislado.',
      'TCG, VGA estándar, 256MiB, display none, nic none y ningún disco físico.',
      'Una VM a la vez, timeouts y cierre de hijos. Cero QEMU activos al terminar.',
      'Los comandos QEMU concretos están definidos en qemu_test.py y cada script.',
      'No se tocaron las carpetas de respaldo/recuperación, no hubo sudo, instalaciones,',
      'cambios del anfitrión, make clean ni limpiezas generales.','',
      '## Comprobaciones reales',
      '- Primer BIOS: KERNEL_READY, INFO y SUMA. Regresión serial original completa.',
      '- ELF64 higher-half, sin símbolos pendientes/RWX, solicitudes Limine presentes.',
      '- FS host ASan/UBSan: capacidad, reutilización, 1023 bytes y profundidad 50.',
      '- Archivos/editor: 40 comandos, límites y errores, guardar/cancelar, reemplazo,',
      '  vacío, overflow y script editado con retorno 42.',
      '- PMM: agotamiento/liberación x2, acceso HHDM y liberaciones inválidas rechazadas.',
      '- PIT creciente; DE/PF esperados en arranques separados con vector/error/RIP.',
      '- Tareas con pilas propias, alternancia, shell y recursos recuperados en tres ciclos.',
      '- UEFI/OVMF real, CODE solo lectura y VARS desechable: shell, PMM y timer.',
      '- ATA: primera VM solo lectura y hash inalterado, IDENTIFY/MBR/límites.',
      '- Persistencia: editar/renombrar/borrar y recuperar tras tres arranques de imagen',
      '  nueva. Corrupción solo en copias; fallo write inyectado sin bloquear la shell.',
      '- IA: 13 unitarias host y COM2→Unix→puente→HTTP mock, fragmentación/checksum,',
      '  longitud/UTF8, errores JSON/HTTP, cancelación y desconexión.',
      '- Captura REAL por QMP, inspeccionada visualmente; no imagen sintética.',
      '- Lanzador real con imagen nueva desechable y guardias de rutas.',
      '- Kernel idéntico en build, staging e ISO; fuentes corresponden a la compilación.','',
      '## Ollama: aceptación parcial',
      'Sondeo actual: '+json.dumps(probe,ensure_ascii=False),
      'La prueba real terminó BLOCKED antes de iniciar QEMU. No se instaló ni descargó nada.',
      'TRANSPORT MOCK ONLY demuestra transporte, no inferencia. Pendiente: servicio local',
      'y modelo pequeño autorizado, OLLAMA_MODEL exacto y make test-ai-real.','',
      '## Conservación, versión y límites',
      'Las dos imágenes preexistentes conservan hashes: results.json incluye antes/después.',
      'No aparecieron nuevos defectos del kernel. No se cambiaron sus fuentes.',
      'La fixture del lanzador se aisló en una imagen nueva para no abrir data.img.',
      'La recuperación anterior regeneró salidas y reparó el generador; sus informes',
      'permanecen como historia, separados de la aceptación actual.',
      'ISO final: build/toolchain-os.iso, '+str((ROOT/'build/toolchain-os.iso').stat().st_size)+' bytes.',
      'Kernel: build/kernel.elf, '+str((ROOT/'build/kernel.elf').stat().st_size)+' bytes.',
      'Kernel SHA256: '+version['kernel_sha256'],
      'ISO SHA256: '+version['iso_sha256'],
      'version.json registra hashes de fuentes, cabeceras, dependencias y configuración.',
      'La ISO regenerada en la recuperación y revalidada ahora se fija como final.',
      'PMM bajo 4GiB; tareas cooperativas kernel sin modo usuario/aislamiento/syscall;',
      'sin IST para garantizar doble fallo; ATA primario legacy/LBA28; FS educativo y ASCII.',
      'Sin prueba de hardware físico, otra VM Ubuntu ni interacción manual Windows/WSLg.',
      'EXTEST y blkdebug producen errores esperados del huésped, no fallos del anfitrión.']
    (ROOT/'docs/validacion/PRUEBAS_Y_RESULTADOS.md').write_text('\n'.join(lines)+'\n')
    phase=(ROOT/'docs/diseno/FASES_IMPLEMENTADAS.md').read_text()
    final=['# Informe final de ToolchainOS — 2026-10-26','',
      'V2.5 parcial: fases 0–6 operativas y verificadas. Fase 7 con transporte/puente',
      'verificados; inferencia real BLOQUEADA. No es cumplimiento completo de fase 7.','']
    final+=phase.splitlines()[2:]
    final+=['','## Cierre',
      '15 grupos superados, 0 fallidos, 0 omitidos, 1 bloqueado.',
      'Resultados y evidencias: docs/validacion/PRUEBAS_Y_RESULTADOS.md y '+rel+'.',
      'En el paquete docente se copian bajo artifacts/final-validation/.',
      'Kernel/ISO corresponden a las fuentes. Se conservan intactas las imágenes',
      'preexistentes por SHA256 y no quedan procesos QEMU.','',
      '## Cambios y errores',
      'No se demostraron defectos nuevos del kernel: no se reescribieron sus fuentes.',
      'Se añadieron validación secuencial, primer arranque, informes y empaquetado.',
      'La prueba del lanzador usa una imagen nueva desechable.',
      'La recuperación anterior y sus correcciones están documentadas por separado.','',
      '## Artefactos y ejecución',
      'ISO final: build/toolchain-os.iso; kernel: build/kernel.elf.',
      'Carpeta/ZIP independiente: entrega/ToolchainOS-20261026-*.',
      'La ruta exacta y el hash del ZIP están en delivery.json de la evidencia actual.',
      'Material docente: docs/guias/ENTREGA_DOCENTE.md. Guía: docs/guias/GUIA_DEMOSTRACION.md.',
      '~~~bash','make iso','make check','python3 scripts/run_vm.py --headless','~~~',
      'Demo persistente en una imagen nueva dedicada:',
      '~~~bash','python3 scripts/run_vm.py --ata --headless --disk build/exposicion.img','~~~',
      'Sin necesidad de KVM/WSLg. Ubuntu de VM usa los mismos comandos con TCG,',
      'pero una instalación distinta no fue comprobada.','',
      '## Pendiente prioritario',
      'Validar inferencia REAL con servicio y modelo local autorizados; no hay instalación',
      'ni descarga automática. No hace falta repetir las fases ya verificadas.']
    (ROOT/'docs/informes/INFORME_FINAL.md').write_text('\n'.join(final)+'\n')
    (ROOT/'artifacts/results-final.json').write_text(json.dumps(results,indent=2)+'\n')
    (ROOT/'artifacts/SHA256SUMS.actual').write_text(version['kernel_sha256']+'  build/kernel.elf\n'+version['iso_sha256']+'  build/toolchain-os.iso\n')
    print('PASSED: informe final basado en evidencia actual, con bloqueo real fase 7.')
if __name__=='__main__':main()

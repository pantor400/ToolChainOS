# Recuperación controlada — 2026-10-26

Instante local: 2026-10-26T00:10:54.547119-05:00. Solo proyecto ToolchainOS. Sin QEMU, otras VMs,
Ollama, sudo, instalaciones, make clean ni eliminación de datos.

## Causa y alcance
La causa exacta del fallo de Windows/almacenamiento no puede demostrarse con
los registros disponibles del proyecto. Archivos de salida quedaron en cero
mientras la ISO y su kernel en staging contenían datos completos. Es compatible
con truncamientos/escrituras no persistidas por la interrupción; no demuestra
un defecto en las fuentes C/NASM ni permite atribuirlo a un componente concreto.
Las redirecciones de logs y la apertura de salidas truncan antes de escribir.
Se evita atribuir causalidad definitiva sin registros del sistema anfitrión.

## Preservación
Estado anterior: artifacts/recuperacion-20261026T050714Z/estado-previo.tar.gz.
Manifest de archivos y hashes: manifest-previo.json; discos: discos-previos.json.
Las carpetas ToolchainOS_RESPALDO_ORIGINAL y ToolchainOS_RECUPERACION no se tocaron.
build/data.img y la otra imagen preexistente se conservaron byte por byte.

## Artefactos regenerados
| Archivo | Bytes actuales | Procedimiento |
|---|---:|---|
| build/kernel.elf | 284400 | make -B iso |
| build/ai_serial.o | 31056 | make -B iso |
| build/pmm.o | 25408 | make -B iso |
| build/test-fs-host | 165448 | GCC host + ASan/UBSan |
| build/toolchain-os.iso | 4171776 | make -B iso |

Se forzó la reconstrucción de los objetos sin limpiar build/. Se ejecutaron
make check y make inspect. El kernel de build, staging y el embebido en la ISO
son idénticos; la revisión estática no equivale a una prueba de arranque.

## Scripts y registros
artifacts/final_hardening.py estaba vacío. Se reconstruyó como verificador
de solo lectura de los cambios ya presentes; no es una recuperación forense
idéntica ni reaplica modificaciones a fuentes.
artifacts/final_report.py contenía secuencias literales \n que producían
SyntaxError. Se reemplazó por entrada al generador de recuperación actual.
Los logs vacíos test-all.log y suite-persistence.log no se pueden recuperar
fielmente: ahora contienen notas de procedencia, sin inventar ejecuciones.
La evidencia histórica independiente de persistencia y sus transcripciones
se conservó. results.json sigue siendo el resumen anterior, sin alterarlo.

## Verificaciones realizadas
| Verificación | Estado | Registro |
|---|---|---|
| build | PASSED | artifacts/recuperacion-20261026T050714Z/build.log |
| elf-check | PASSED | artifacts/recuperacion-20261026T050714Z/elf-check.log |
| inspect | PASSED | artifacts/recuperacion-20261026T050714Z/inspect.log |
| fs-host-asan-ubsan | PASSED | artifacts/recuperacion-20261026T050714Z/fs-host.log |
| ai-protocol-host | PASSED | artifacts/recuperacion-20261026T050714Z/ai-protocol-host.log |
| hardening-check | PASSED | artifacts/recuperacion-20261026T050714Z/hardening-check.log |
| static-artifacts | PASSED | artifacts/recuperacion-20261026T050714Z/static-audit.log |
| shell-syntax | PASSED | artifacts/recuperacion-20261026T050714Z/shell-syntax.log |

El protocolo IA tiene 13 pruebas unitarias en el anfitrión: enmarcado,
fragmentación, paquetes múltiples, checksum, longitud/versión, UTF8, límites,
restricción loopback y contrato JSON mediante objetos mock, sin sockets ni
servicio/modelo Ollama. FS usa el test C existente con ASan/UBSan.
No se modificaron las fuentes del kernel durante esta recuperación.

## Pruebas pendientes
La última revisión compilada necesita autorización para revalidación en QEMU:
BIOS/UEFI, regresión serial, archivos/editor, PMM, IDT/timer/tareas, ATA y
persistencia con imágenes desechables, COM2/mock y captura de framebuffer.
La inferencia real con Ollama sigue sin validación; no se inició ni consultó
el servicio ahora. Su ausencia corresponde al registro histórico, no a un
sondeo nuevo. No se reinició el desarrollo desde la fase 0.

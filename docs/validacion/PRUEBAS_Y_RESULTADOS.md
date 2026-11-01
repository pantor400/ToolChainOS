# Pruebas y resultados — ToolchainOS V3.1

Fecha local: 2026-10-26T09:21:58.845541-05:00. Evidencia NUEVA: artifacts/v3.1-20261026T133146Z.
En el paquete: artifacts/final-validation/ y evidencias/. Las pruebas corrieron
en una copia QA aislada; no se sobrescribieron los registros históricos.

## Resultado final: 18 grupos PASSED, 0 FAILED, 0 BLOCKED y 0 SKIPPED
| Grupo | Estado | Log |
|---|---|---|
| build | PASSED | build-final.log |
| elf | PASSED | elf-final.log |
| utf8-host | PASSED | utf8-host.log |
| utf8-keyboard | PASSED | utf8-keyboard.log |
| presentation | PASSED | presentation.log |
| fs-host | PASSED | fs-host.log |
| ai-protocol-host | PASSED | ai-protocol-host.log |
| legacy | PASSED | legacy.log |
| files-editor | PASSED | files-editor.log |
| core | PASSED | core.log |
| uefi | PASSED | uefi.log |
| persistence | PASSED | persistence.log |
| utf8-persistence | PASSED | utf8-persistence.log |
| ai-transport-mock | PASSED | ai-transport-mock.log |
| ai-real | PASSED | ai-real.log |
| ai-utf8-real | PASSED | ai-utf8-real.log |
| launcher | PASSED | launcher.log |
| static-artifacts | PASSED | static-artifacts.log |

El recuento es de grupos, no de aserciones. La prueba host recorre 1 112 064
escalares Unicode válidos y comprueba roundtrip y fragmentación con ASan/UBSan.
Incluye inválidos/overlong/surrogates/fuera de rango y mapas/modificadores/glifos/FS.

## Comprobaciones específicas
- Selección ES y US mediante teclas PS/2 inyectadas por QMP y Enter, no solo por UART.
- Default ES sin responder y sin bloqueo; TECLADO consulta/cambia ES/US.
- PS/2: año, Perú, á/Ñ/ü/¿/¡/?/!, Shift, teclas muertas, AltGr+Q y Backspace.
- Serial: año, Perú, programación, ¿Qué es un sistema operativo?, todo el conjunto
  español solicitado y símbolos de 3/4 bytes. Decodificación estricta de las capturas.
- Backspace: ñ/ú/emoji completos y prefijo UTF-8 incompleto; casos malformados
  descartan la línea sin ejecutar. Desborde en bytes no deja caracteres cortados.
- Editor: edición, Backspace multibyte, guardar/abrir, cancelar y 1023 bytes.
- Nombres y contenido UTF-8 persistentes tras tres arranques; editor/REN/COPY/RM.
- Framebuffer: español, recoloreado y selección; capturas reales inspeccionadas.
- IA real abierta heredada y consulta de cobertura española mediante qwen2.5:0.5b.
  Resultado registrado: ¡Hola, Perú! El año trae programación y pingüinos. ¿Qué tal?
  Se pidió repetición para fijar el repertorio, pero la respuesta procede del modelo
  real por /api/generate. La consulta abierta también se ejecutó, no es un eco simulado.
- Mock COM2 aparte: caracteres completos y respuesta larga truncada sin cortar UTF-8.
- Toda la regresión: BIOS/UEFI, memoria, IDT/timer, tareas, archivos/editor, ATA y
  persistencia con imágenes desechables; shell tras IA y lanzador aislado.
- Kernel, ISO y copia QA idénticos; make check sin símbolos pendientes/RWX.

## Intentos y correcciones
build-primer-intento.log conserva errores de indentación de glyph.c tratados
como error por -Werror. Se separaron sentencias; build-final.log y elf-final.log
son correctos. No se desactivaron advertencias ni se ocultó el intento inicial.
Se ajustó la ubicación de tildes minúsculas para no mantener el punto de í.
La batería final se ejecutó después de esa corrección y pasó completa.

## Manual vs automatizado
Este cierre verifica teclado por PS/2 emulado y serial automáticamente; las capturas
se inspeccionaron visualmente. No se afirma prueba humana del teclado físico
ni interacción manual Windows/WSLg. La evidencia humana de V3.0 permanece histórica.
El test usa inyección QMP real en el dispositivo emulado, no una respuesta fija.
Host tests no sustituyen la evidencia de QEMU, que se conserva separadamente.

## Conservación y alcance
Respaldo previo independiente: respaldo-v3.0.tar.gz en esta carpeta de evidencia.
protegidos.json y results.json prueban hashes idénticos de imágenes preexistentes
y ZIP V3.0. No se tocaron carpetas de respaldo/recuperación ni configuraciones
del anfitrión. No hubo instalaciones, sudo, limpiezas generales ni cambios gráficos.
Las pruebas destructivas usaron exclusivamente discos nuevos y copias desechables.
ai_bridge.py y protocolo TC/1 no se modificaron; PMM/tareas/FS/ATA permanecen intactos.

UTF-8 de transporte/almacenamiento no implica Unicode gráfico completo.
La fuente cubre español precompuesto y algunos símbolos; los restantes son recuadros
solo en framebuffer. Serial conserva sus bytes. Backspace es por punto de código,
no por grafemas; no hay normalización, shaping ni garantía general de NFD/ZWJ.
Ancho CJK/emoji aproximado. FS mantiene límites en bytes y casefold ASCII.
Los controles de salida se filtran y los bytes malformados se señalan con U+FFFD.
No se recupera memoria Limine, no hay aislamiento/procesos de usuario ni C++ completo.

## Integridad
Kernel SHA256: 17ad43a34a9539fb16f5cd3b3c09ebc3fbd72b0c1afe4e47e841e0412f0e0f99
ISO SHA256: 25016b61255228b3e9e5e29b8c2cf94cf84215a1caf2f5c4415858a0804b6ba7
version.json enumera fuentes, cabeceras, Makefile, linker y dependencia Limine.
El ZIP nuevo se verifica por manifiesto/CRC y contiene el mismo kernel/ISO probados.

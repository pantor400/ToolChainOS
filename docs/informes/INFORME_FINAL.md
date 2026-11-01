# Informe final ToolchainOS V3.1

Se implementó selección inicial español latinoamericano (Perú)/US con default
seguro tras 3 segundos, TECLADO en ejecución y conservación de KERNEL_READY/OS>.
Entrada, editor/archivos y salida serial admiten UTF-8 válido; Backspace elimina
puntos de código completos. El framebuffer amplía la fuente para español.
No se confunde distribución, codificación y representación.

## Cambios
- Nuevos keyboard.c/keyboard_menu.c: set 1, ES/US, modificadores, teclas muertas y menú.
- Nuevo utf8.c: validación/codificación/decodificación y límites por punto de código.
- console.c/glyph.c: serial UTF-8, celdas Unicode y fuente española ampliada.
- kernel.c: arranque V3.1, TECLADO y línea UTF-8 acotada.
- ai_serial.c: presentar payload UTF-8 completo; TC/1 y puente sin cambios.
- editor.c: indica que la capacidad se mide en bytes UTF-8; formato FS intacto.
- Tests, Makefile, documentación y empaquetado V31 actualizados.

## Validación
18 grupos PASSED, 0 FAILED, 0 BLOCKED y 0 SKIPPED al finalizar.
18 grupos incluyen compilación/ELF, host y QEMU, no número total de aserciones.
La batería cubre las nueve pruebas solicitadas: ES, US, TECLADO, palabras
españolas, interrogación, editor/FS, Backspace, Ollama real y regresión BIOS/UEFI.
Capturas reales muestran menú, español, redibujado e IA. Son pruebas automatizadas;
no se atribuye una prueba humana de teclado físico. Detalle: ../validacion/PRUEBAS_Y_RESULTADOS.md.
Se corrigieron avisos de indentación iniciales y el acento/punto de í; se conservan logs.

## IA real
Ollama 0.40.11 + qwen2.5:0.5b local, con pregunta española y respuesta UTF-8 válida:
«¡Hola, Perú! El año trae programación y pingüinos. ¿Qué tal?».
La consulta abierta anterior también pasó. IA END y shell posteriores comprobados.
No se alteró el protocolo ni el puente para simular una respuesta.

## Preservación y entrega
Respaldo V3.0 previo: artifacts/v3.1-20261026T133146Z/respaldo-v3.0.tar.gz.
Todas las imágenes y ZIP preexistentes conservan sus hashes; no hubo cambios del anfitrión.
ISO nueva: build/toolchain-os.iso. ZIP nuevo: entrega/ToolchainOS-V31-*.zip.
Ruta exacta/hash en delivery.json de la evidencia actual. La entrega V3.0 sigue intacta.
Evidencia actual: artifacts/v3.1-20261026T133146Z; dentro del ZIP: artifacts/final-validation/.
El disco del ZIP se crea nuevo y vacío, sin contenido de imágenes preexistentes.

## Limitaciones explícitas
No es Unicode completo: fuente latina limitada, sin shaping/normalización/grafemas
complejos, NFD/ZWJ sin garantía general y ancho CJK/emoji aproximado.
Serial conserva Unicode válido, pero requiere terminal UTF-8 y fuente del anfitrión.
TECLADO remapea PS/2 del huésped; no modifica la distribución física del anfitrión.
Límites FS/shell en bytes; casefold ASCII heredado. Las demás limitaciones del
kernel educativo (PMM 4GiB, cooperación sin aislamiento, ATA primario) permanecen.
No se probó hardware físico ni otra instalación Ubuntu.
Instrucciones y demostración: README.md, ../guias/GUIA_DEMOSTRACION.md y ../guias/TECLADO_UTF8.md.

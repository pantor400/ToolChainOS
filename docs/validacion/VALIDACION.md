> Documento histórico de la versión inicial. Para el estado actual consulte\n> ../diseno/ARQUITECTURA.md, ../diseno/FASES_IMPLEMENTADAS.md y PRUEBAS_Y_RESULTADOS.md.\n\n# Validación de Toolchain OS V2

Fecha: 19 de octubre de 2026. GCC 13, NASM 2.16.01, GNU ld, xorriso 1.5.6 y QEMU 8.2.2.

- C compilado con advertencias como errores; ELF64 x86-64 sin símbolos pendientes, sin segmentos RWX y con solicitudes Limine conservadas.
- Misma ISO arrancada mediante BIOS y UEFI en QEMU/TCG con 256 MiB de RAM.
- Suma directa y guiada, operandos negativos, resultados negativos y límites +/-1000000000.
- Rechazo de decimales, valores fuera del rango, texto inválido y argumentos extra; cancelación de entradas guiadas.
- Repetición de texto conservando mayúsculas, minúsculas y espacios interiores; retroceso y entrada CRLF.
- Entrada PS/2 con Shift, consola serie, INFO y MEM.
- Cambio de todos los colores y fondos por comandos; captura de framebuffer verificada por valores RGB para texto rojo y fondo claro.
- GDB conectado en loopback:1234 y breakpoint alcanzado en kmain.

scripts/smoke_test.py repite las pruebas de consola por UART. Los registros y capturas adjuntos proceden de la V2. El cambio gráfico se ve en QEMU; la terminal serie mantiene sus colores propios.

No se ejecutó aquí el BAT en Windows ni la interfaz de VS Code. Se probaron la ISO, el código, el enlace, el arranque y GDB en Linux. Se utilizó GCC del anfitrión configurado para freestanding; la opción CROSS existe pero no se probó con un cross-compiler separado. El núcleo no implementa Unicode, SMP, procesos ni sistema de archivos.

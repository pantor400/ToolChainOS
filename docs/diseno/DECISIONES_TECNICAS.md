# Decisiones técnicas
## Conservación
Se mantiene Limine y la paginación del cargador. No se recuperan regiones del
bootloader; solo LIMINE_MEMMAP_USABLE. Metadatos del PMM ocupan páginas usables
que quedan excluidas de la API de asignación. Las páginas de kernel, framebuffer,
ACPI y reservas no se asignan. Tope físico deliberado: 4 GiB.
MAP_NONMANAGED_BYTES suma regiones del mapa no administradas (incluye MMIO y
reservas), no representa RAM instalada.
El taller PDF no fue suministrado; no se afirma haberlo contrastado.

## Interrupciones y tareas
Gates con el selector CS vigente de Limine, stubs con/sin error, GPR preservados,
pila alineada a 16 antes de llamar C; compilación -mgeneral-regs-only y sin red zone.
PIC remapeado a 32/40, solo IRQ0 sin máscara; PIT divisor 11932 (~100 Hz).
Los manejadores no asignan ni imprimen en el IRQ de timer. Excepciones fatales
reportan vector, error y RIP; PF incluye CR2. No hay IST ni recuperación de fallos
de pila/doble fallo garantizada. Las pruebas provocan DE y PF en VMs separadas.
Dos tareas de demostración con pilas 4KiB, registros callee-saved y yield; al
terminar se recuperan páginas. No son procesos de usuario ni tareas preventivas.

## Disco
q35 se conserva; pc/i440fx dispone de IDE compatible según la
[documentación QEMU](https://www.qemu.org/docs/master/system/qemu-manpage.html).
La presencia se comprueba por clase PCI IDE y modo primario compatible.
Perfil probado: -machine pc -accel tcg -drive file=IMAGEN,format=raw,if=ide,index=0.
Solo maestro primario, sectores 512 y LBA28. Primeras operaciones son lecturas.
Escrituras bloqueadas salvo imagen EXACTA de 8 MiB/16384 sectores, firma de
proyecto en LBA1 y partición MBR tipo 0x7f, inicio 2048 y tamaño 14336.
Nunca se escribe LBA0, LBA1 ni la ISO.
LBA2048: scratch de DISK TEST con lectura previa/restauración.
LBA2049 y LBA2183: snapshots, cada uno cabecera de 512 + payload de 133 sectores.
Límite exclusivo de escritura: 2317.
Cabecera: magic TCOSSNAP[8], versión LE32@8=1, bytes LE32@12=68096,
generación LE64@16, FNV1a32 payload@24, reservado@28, FNV1a32 cabecera[0:32]@32.
Cada uno de 64 registros: tipo u8@0, padre u8@1, reservado u16@2,
tamaño LE32@4, nombre[32]@8, contenido[1024]@40. No se serializa padding C.
Se invalida el destino alternativo, se escribe/flush payload y se confirma la
cabecera al final. Se conserva el último snapshot válido y se comprueban checksum,
tipos, terminadores, padres, ciclos, duplicados y rutas antes de importar.
Sin disco/formato autorizado: RAM. Ambos snapshots corruptos: RAM y escritura
deshabilitada para preservar evidencia. Los fallos dejan datos nuevos en RAM y
reportan ERROR PERSISTENCIA; FSSTATUS DIRTY=YES. No se promete atomicidad frente
a cualquier fallo físico o caché que incumpla flush.
La inyección QEMU usa iotype=write explícito: el valor por defecto afecta todos
los tipos de E/S. [Referencia blkdebug](https://www.qemu.org/docs/master/devel/testing/blkdebug.html).

## IA
COM1=0x3F8 consola; COM2=0x2F8 IA, segundo -serial en QEMU.
Socket Unix en build con permisos del proceso umask077. Sin puertos TCP externos.
TC/1: magic TC, versión u8=1, tipo u8, id LE32, longitud LE16, suma LE16,
payload UTF-8 <=1024 bytes. Suma de bytes de cabecera [0:10] y payload módulo 65536.
Tipos: 1 pregunta, 2 estado, 3 cancelar; 0x81 respuesta, 0x82 estado, 0xff error.
Una solicitud activa, resync, fragmentación, IDs tardíos descartados y timeout:
2s estado, 120s consulta. IA CANCEL no afecta a la shell; descarta resultados.
La inferencia HTTP ya iniciada puede continuar hasta su timeout tras cancelar.
El puente limita JSON a 1MiB y respuesta a 1024 bytes con [TRUNCATED].
Desde V3.1, UART conserva UTF-8 válido y el framebuffer usa glifos españoles.
Glifos no implementados: recuadro gráfico; no se altera su salida serial.
No se promete Unicode gráfico completo; véase ../guias/TECLADO_UTF8.md.
No se registra el texto de preguntas en los logs del puente.
Se verifica /api/tags antes de POST /api/generate con stream=false.
[Generación Ollama](https://docs.ollama.com/api/generate),
[modelos instalados](https://docs.ollama.com/api/tags).
URLs solo loopback, sin proxies del entorno ni redirecciones HTTP.
No se instala Ollama ni se descargan modelos. El servidor mock existe solo en
test_ai.py y su texto indica TRANSPORT MOCK ONLY.

## Validación V3.0 — 26/10/2026
Ollama 0.40.11 instalado manualmente por el usuario, modelo qwen2.5:0.5b,
CPU. Inferencia real COM2→puente Python→API local verificada automatizadamente.
No se instaló/actualizó nada ni se modificó el protocolo en este cierre.
Véanse ../informes/INFORME_FINAL.md y ../validacion/PRUEBAS_Y_RESULTADOS.md para las evidencias actuales.

## V3.1 — entrada y salida de texto
Selección ES/US acotada a 3 segundos; TECLADO solo cambia PS/2 del huésped.
Serial ya llega codificado por el anfitrión. Validación UTF-8 estricta, Backspace
por punto de código y fuente didáctica ampliada; sin normalización/grafemas complejos.
Protocolo COM2 y puente intactos. Pruebas actuales: ../validacion/PRUEBAS_Y_RESULTADOS.md.

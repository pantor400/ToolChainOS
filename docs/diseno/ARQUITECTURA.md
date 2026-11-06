# Arquitectura actual
```
BIOS o UEFI → Limine → kmain (BSP x86-64)
  ├─ PS/2 ES-LATAM/US → UTF-8 validado → shell/editor
  ├─ COM1 UTF-8 + framebuffer RGB con fuente española limitada
  ├─ memmap USABLE + HHDM → PMM de páginas 4096 bytes
  ├─ IDT + stubs NASM → excepciones fatales / PIC → PIT 100 Hz
  ├─ shell loop → scheduler cooperativo → dos pilas propias PMM
  ├─ FS RAM → snapshots LE con checksum → ATA PIO → imagen IDE pc
  └─ COM2 → socket Unix local → ai_bridge.py → HTTP loopback Ollama
```
El perfil original q35 se conserva. ATA usa pc/i440fx, maestro primario 0x1F0,
control 0x3F6, LBA28, PCI IDE en modo compatible. Sin AHCI ni DMA.
El kernel no usa libc Linux; el puente Python es un proceso Linux deliberado.
Se arranca un único BSP; -smp 2 conserva el perfil, sin ejecución paralela de AP.
RUN CPP continúa siendo el intérprete original, no un compilador completo.

## Módulos
- kernel.c: arranque, TECLADO, comandos y entrada UTF-8 acotada.
- keyboard.c/keyboard_menu.c: set 1 ES/US, teclas muertas, menú con timeout.
- utf8.c: validación/decodificación/codificación y límites de punto de código.
- glyph.c/console.c: Unicode en UART y glifos españoles en framebuffer.
- fs.c: 64 nodos, rutas relativas/absolutas, invariantes y operaciones.
- editor.c: búfer transaccional de 1023 bytes, guardar/cancelar.
- pmm.c: bitmaps en memoria USABLE, acceso HHDM, tope físico 4 GiB.
- interrupts.c/interrupt_stubs.asm: IDT, excepciones, PIC y PIT.
- tasks.c/context.asm: cambios de pila reales, READY/RUNNING/WAITING/DEAD.
- ata.c: IDENTIFY, transferencias de 256 palabras, plazos y errores.
- storage.c: autorización de imagen, validación, snapshots alternados.
- ai_serial.c: UART COM2, enmarcado TC/1, solicitudes asíncronas.
- scripts/ai_bridge.py: protocolo y API local real; ninguna respuesta fija.

Las tareas son cooperativas en modo kernel y comparten memoria. No existen
aislamiento, modo usuario, syscall ni planificación preventiva.

## Validación V3.0 — 26/10/2026
Ollama 0.40.11 instalado manualmente por el usuario, modelo qwen2.5:0.5b,
CPU. Inferencia real COM2→puente Python→API local verificada automatizadamente.
No se instaló/actualizó nada ni se modificó el protocolo en este cierre.
Véanse ../informes/INFORME_FINAL.md y ../validacion/PRUEBAS_Y_RESULTADOS.md para las evidencias actuales.

## V3.1
Se conserva TC/1 y el puente sin cambios. La salida IA ahora conserva UTF-8 válido
en lugar del filtro ASCII por byte. No se cambian PMM, tareas, FS ni ATA.
Detalles de distribución/codificación/fuente y límites: ../guias/TECLADO_UTF8.md.


## Extensión web

Navegador gráfico ELF integrado en Apps y GUI WEB. Iniciar con
python3 scripts/run_vm.py --web; IA simultánea con --ai --web.
HTTP/HTTPS y TLS verificado se ejecutan en Ubuntu por COM3; RTL8139
conserva ICMP/DNS y no implementa TCP/TLS nativos. HTML estático y enlaces,
sin CSS completo, JavaScript, login o multimedia. Apps en ring 0.
Consultar docs/diseno/NAVEGADOR_WEB.md y docs/guias/USO_NAVEGADOR.md
para límites y comandos; la validación de integración se registra en
docs/guias/VALIDACION_TOOLCHAINOS_WEB.md. Los resultados generados son locales.

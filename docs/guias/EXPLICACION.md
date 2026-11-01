> Documento histórico de la versión inicial. Para el estado actual consulte\n> ../diseno/ARQUITECTURA.md, ../diseno/FASES_IMPLEMENTADAS.md y ../validacion/PRUEBAS_Y_RESULTADOS.md.\n\n# Del código al arranque

## Qué hace cada archivo

- `src/kernel.c`: solicitudes Limine, punto de entrada `kmain`, comandos e información de memoria. Las estructuras request son un acuerdo entre bootloader y núcleo; Limine rellena las respuestas antes de entrar al núcleo.
- `src/cpu.asm`: instrucciones NASM y funciones que C puede llamar. `asm_sumar` recibe RDI y RSI, devuelve RAX y respeta System V AMD64. Las lecturas de CR0/CR3/CR4 son privilegiadas y se ejecutan dentro del núcleo.
- `src/console.c`: configura COM1, dibuja letras en framebuffer y lee caracteres por sondeo. La dirección 0x3F8 es un puerto E/S, no RAM.
- `src/memory.c`: implementa memcpy, memset, memmove y memcmp; no utiliza la biblioteca estándar del anfitrión.
- `include/font.h`: matriz de bits propia de 5x7 por carácter, dibujada con escala 2.
- `linker.ld`: define segmentos de carga, permisos y direcciones virtuales. Separa texto RX de datos RW. KEEP conserva las solicitudes que Limine busca en el ELF.
- `limine.conf`: le dice al bootloader qué núcleo cargar y qué resolución solicitar.
- `Makefile`: dependencias, compilación, enlace, empaquetado, inspección y ejecución.
- `.vscode`: tareas de construcción y configuración de GDB para VS Code.
- `vendor/limine`: dependencia externa fijada y su licencia; no es código escrito por el alumno.

## Banderas que aparecen en la exposición

- `-ffreestanding`: compila suponiendo un entorno independiente. No equivale por sí sola a eliminar bibliotecas del enlace.
- `-nostdlib`: evita el enlace estándar automático; nuestro enlace usa ld directamente y el archivo libgcc explícito.
- `-mno-red-zone`: evita usar los 128 bytes bajo RSP como almacenamiento temporal; útil en núcleos donde las interrupciones pueden alterar esa zona.
- `-mcmodel=kernel`: modelo de código apropiado para el rango virtual alto elegido por este diseño x86-64.
- `-mgeneral-regs-only`: evita generar instrucciones que necesiten registros SIMD/FPU antes de configurarlos.
- `-fno-pie -fno-pic`: el núcleo tiene el modelo de direcciones fijo del linker; no es una aplicación PIE de Linux.
- `-MMD -MP`: genera dependencias de cabeceras y reglas auxiliares para Make.
- `--gc-sections`: descarta secciones sin uso; por eso KEEP es significativo.
- `-g`: conserva información de depuración para GDB.

## 0x100000, 4 KiB y higher-half

0x100000 = 1048576 bytes = 1 MiB. Es una dirección clásica de carga física en algunos diseños. No indica el tamaño del núcleo. Aquí el linker comienza en 0xffffffff80000000, dirección VIRTUAL alta. Limine elige la ubicación física y la comunica mediante su respuesta; INFO presenta ambas.

0x1000 = 4096 bytes = 4 KiB. ALIGN(0x1000) coloca el inicio de las secciones en límites de 4 KiB. No crea tablas de páginas: la paginación inicial la prepara el bootloader. CR0 bit 31 permite comprobar que está activa. CR3 es un registro de control asociado a las tablas de páginas, no una cantidad de RAM.

## Arranque y 64 bits

Firmware BIOS/UEFI -> Limine -> kmain -> consola. El núcleo recibe un entorno x86-64 preparado por el protocolo Limine. GRUB es otra opción válida; el caso clásico Multiboot de 32 bits exige preparar una transición para un núcleo de 64 bits, pero no debe afirmarse que GRUB es incapaz de cualquier entrada de 64 bits.

Framebuffer no equivale a interfaz gráfica completa. El núcleo dibuja píxeles; la fuente y las letras las construye este proyecto. El mapa de memoria solo se consulta; no tenemos todavía un asignador. La cifra USABLE no es igual a toda la RAM configurada en QEMU.

## Secuencia sugerida para demostrarlo

1. Abre kernel.c: muestra la llamada `asm_sumar(a,b)`.
2. Abre cpu.asm: muestra RDI + RSI y retorno en RAX.
3. Ejecuta `make all` y enseña los objetos `.o`.
4. Compara `nm build/kernel.o` con `nm build/cpu.o`: referencia y definición.
5. Abre linker.ld: explica KEEP, ALIGN y permisos.
6. Ejecuta `make inspect` y abre los informes.
7. Ejecuta `make iso` y `make run`.
8. En el núcleo usa SUMA, TEXTO, COLOR, INFO y MEM. Distingue base física y virtual.
9. Cierra QEMU. Muestra `git diff` después de cambiar un mensaje de kernel.c.
10. Reconstruye y ejecuta: Make debe actualizar kernel.o y el núcleo/ISO, sin recompilar todos los objetos.

## Git bisect (cuando ya exista un historial)

`git bisect start`, `git bisect bad`, `git bisect good COMMIT_QUE_FUNCIONABA`.
En cada versión reconstruye y prueba. Marca `git bisect good` o `git bisect bad` según el resultado. Al terminar, `git bisect reset`. Sustituye el marcador por un hash verdadero. No inventes un fallo ni un commit para la exposición.

## Precisión al exponer

Make no es universalmente superior a CMake, ni NASM universalmente superior a GAS. Son elecciones del proyecto. Git localiza cambios y conserva historia; no detecta corrupción de memoria por sí mismo. `-no-reboot` no detiene automáticamente la CPU en GDB ante un fallo. `-s -S` son abreviaturas habituales; nuestro Makefile usa una dirección GDB explícita de loopback. El soporte SMP de un bootloader no implica que este núcleo ya administre varias CPU.

## Fuentes oficiales

- Limine y licencia: https://github.com/limine-bootloader/limine
- Protocolo: https://github.com/limine-bootloader/limine-protocol
- GCC: https://gcc.gnu.org/onlinedocs/gcc/
- GNU ld: https://sourceware.org/binutils/docs/ld/
- GNU Make: https://www.gnu.org/software/make/manual/
- NASM: https://www.nasm.us/docs.php
- QEMU: https://www.qemu.org/docs/master/system/
- Git: https://git-scm.com/docs

## Funciones interactivas de V2

Consulta GUIA_CODIGO.md para seguir el parser, estados de entrada, suma variable, color y eco del texto.

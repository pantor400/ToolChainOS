# Registro de cambios
- V2.1: FS acotado, validación estricta y operaciones RM/RMDIR/REN/COPY/TREE.
- V2.2: editor transaccional multilinea; PMM 4KiB/HHDM y pruebas de agotamiento.
- V2.3: IDT x86-64, excepciones, PIC/PIT, UPTIME y tareas cooperativas con pilas.
- V2.4: perfil IDE pc, ATA LBA28, snapshots persistentes y reinicios comprobados.
- V2.5 parcial: COM2 asíncrono, protocolo y puente real para Ollama; transporte
  probado con mock. Inferencia real bloqueada por ausencia de servicio/modelo.
No se crearon commits: la copia suministrada no tenía repositorio .git.
La lista exacta de archivos comparados con el respaldo inicial está en
artifacts/changed-files.json.

## Recuperación controlada — 2026-10-26
Regenerados objetos, ELF, ISO y ejecutable de prueba host sin limpiar build/.
Discos preexistentes conservados por hash; fuentes del kernel sin cambios.
Verificaciones host/estáticas actuales separadas de pruebas históricas QEMU.
Auxiliar hardening reconstruido como comprobador; generador de informes reparado.
Ninguna VM ni servicio Ollama se ejecutó en esta recuperación.

## Validación final — 2026-10-26
Fases 0–6 revalidadas secuencialmente en QEMU TCG BIOS/UEFI. 15 grupos PASSED,
0 FAILED y 1 BLOCKED (inferencia Ollama real sin servicio/modelo). Sin cambios
nuevos en el kernel. Fixture del lanzador aislada; entrega docente curada con
fuentes/ISO/evidencia e imagen nueva vacía, sin copiar discos preexistentes.

## V3.0 — 2026-10-26
Bienvenida ASCII definitiva y HELP completo por categorías, conservando KERNEL_READY/OS>.
IA real revalidada con Ollama 0.40.11 y qwen2.5:0.5b en CPU: fase 7 cerrada.
14 grupos finales PASSED; intento fallido de fixture estática conservado y corregido.
Nuevo paquete V3; sin cambios de protocolos/subsistemas ni pérdida de datos.

## V3.1 — 2026-10-26
Respaldo V3.0 independiente antes de editar. Menú ES-LATAM/US con default
tras 3 segundos, TECLADO, mapas PS/2 y teclas muertas. UTF-8 estricto de
entrada/salida; Backspace por punto de código y glifos españoles en framebuffer.
IA conserva texto válido sin cambiar TC/1 ni ai_bridge.py. 18 grupos actuales
superados, incluyendo IA real con tildes y persistencia UTF-8. Límites gráficos
y de grafemas documentados; entrega nueva sin tocar ZIP/discos anteriores.

## 2026-10-29 Avance académico E56 sobre V3.1
PMM consolidado, tramos contiguos y reservas con validación completa; heap por arenas
con kmalloc/kcalloc/kfree, 16 bytes de alineación, división/fusión y límite de 512 KiB.
PCB/TCB generales, pilas 16 KiB, FIFO Round Robin cooperativo, dormir/despertar por PIT
y recolección desde shell. PS, THREADS, SCHED, PROC TEST, HEAPSTATS, HEAPTEST, MEMMAP.
Se conserva TASKS con la infraestructura común. Protocolo COM2 y formato ATA sin cambios.
19 grupos finales aprobados; registros de intentos y bloqueos iniciales conservados.

## 2026-11-01 Limpieza de código y de árbol
Eliminadas fs_get_cwd(), keyboard_get_layout(), tasks_test_poll() y process_command()
por no tener consumidores; str_copy_kernel() y split_path_name() sustituidas por
copystr() y fs_split(). RUN CPP, RUN_CPP y la entrada guiada comparten run_cpp_path().
Borrados scripts/legacy/ (seis guiones archivados), la configuración local de CLion y
los intermedios de build/iso_root. Comportamiento del SO y comandos sin cambios;
recompilado, verificado con check_elf y con las pruebas host PMM, heap, UTF-8 y scheduler.

## 2026-11-02 Agente de bajo nivel y correcciones P1
La IA deja de ser solo conversacional: `AGENTE <objetivo>` abre una sesión TC/1 por COM2
(tipo 4) donde el puente propone órdenes (0x84), el kernel las valida con lista blanca,
las ejecuta en el shell y devuelve la salida real capturada (tipo 5); `0x85` o el límite
de 8 pasos cierran la sesión. Solo lectura por defecto; `AGENTE ESCRITURA ON` habilita
CREAR/MKDIR/RM/REN/COPY. Nuevos `include/agent.h`, `src/agent.c`, `scripts/ai_agent.py`
y `make test-agent`. `console_capture_*` añade la captura acotada de consola.
TC/1 mantiene versión 1: los tipos nuevos conviven con 1/2/3, 0x81/0x82/0xff.
Correcciones de la auditoría aplicadas: cota en collect_functions(), literales numéricos
en unsigned con saturación, -INT64_MIN sin desbordamiento, last_return_val reiniciado por
invocación, borrado de dos celdas en console_erase_last(), cola de teclado de 16 bytes y
editor que compara comandos sin distinguir mayúsculas. Ver docs/auditoria/REVISION_FUNCIONES.md §8
para las fichas no aplicadas y su motivo. `test_all.py` incorpora el paso `ai-agent` y el
`Makefile` expone `SAN` para ejecutar las pruebas de host en distribuciones sin libasan/libubsan.

## 2026-11-02 HELP en dos columnas
Reorganizado el HELP: secciones por categoría con sangría, órdenes en la columna 4 y
descripciones alineadas en la 28, todo dentro de las 42 filas de la consola. Se conservan
los mismos comandos y textos que verifica `scripts/test_presentation.py`.

## 2026-11-03 V3.2 escritorio claro, red, ratón y apps ELF
Interfaz gráfica de tema claro con doble buffer en el PMM, tema centralizado y
recorte por superficie; perfiles 1920x1080 y 1280x800 seleccionables con
`make iso RES=...`. Tipografías horneadas en el anfitrión (6 tamaños, 4bpp) e
iconos procedurales escalados por enteros. Ratón PS/2 por IRQ12 con
resincronización y sondeo de respaldo; el touchpad se declara como
compatibilidad PS/2 relativa. Driver PCI RTL8139 con MAC, ARP, RX 8 KiB, TX
2 KiB e IP fija 10.0.2.15/24. Cargador ELF64 para PIE estáticos que localiza
`.rela.dyn` por cabeceras de sección y aplica `R_X86_64_RELATIVE`; las apps usan
`host_api_t` (ABI 2) y `elf_abort()` las devuelve al kernel si fallan. Nuevos
comandos `GUI`, `GUI DEMO`, `GUI RUN <n>`, `RED`, `RATON` y `ELF`; prueba
`make test-gui` sin capturas (marcadores serie + QMP). El driver de teclado
entrega ESC (scancode 0x01) como 27, que antes se descartaba.

## 2026-11-03 Perfil grafico compacto por defecto
`RES` pasa a 1280x800 para que la ventana del escritorio ocupe menos; 1920x1080
sigue disponible con `make run RES=1920x1080`. `limine.conf` versionado declara
1280x800x32. `make run`, `debug`, `trace` y `uefi` abren QEMU con
`-display gtk,zoom-to-fit=on` (`DISPLAY_QEMU`) para ajustar la ventana a la
pantalla del anfitrion sin cambiar el modo del huesped. `make test-gui` validado
en ambos perfiles.

## 2026-11-03 Fluidez del escritorio, ventanas ampliadas y apps de editor/terminal
Eliminado el parpadeo y el retardo del puntero: el escritorio repinta solo el
rectangulo marcado como sucio (`invalidate`, `redraw_region`, `flush_region`), el
fondo es un gradiente precalculado en vez de mezcla por pixel, las sombras usan
relleno solido y el bucle duerme con `sti; hlt` cuando no hay eventos. El cursor
se pinta en el dispositivo y se restaura desde el back buffer. Las apps solo
llaman a `present()` cuando llega un evento. Medido con `test_gui.py`: la sesion
grafica completa (apertura, movimientos, clic y salida) tarda ~0.5 s.
QEMU deja de estar mal configurado: nuevo `scripts/qemu_config.py` compartido por
Makefile, `qemu_test.py`, `smoke_test.py`, `test_gui.py` y `run_vm.py`, con KVM
automatico (`-accel kvm -cpu host`, TCG `-cpu max` como respaldo), 32 MiB de
memoria de video, `-rtc base=utc,clock=host` y disco con `cache=writeback`.
La ventana Sistema muestra CPU y nucleos por CPUID, RAM libre, heap, procesos e
hilos, uptime, resolucion, red, raton, teclado, despachos y archivos. La ventana
Archivos navega con flechas, abre un visor, crea archivos y carpetas, renombra y
borra. `host_api_t` sube a ABI 3 (FS completo, `exec` y teclas extendidas).
`hola.elf` se retira: `apps/editor.c` y `apps/terminal.c` la sustituyen y
`limine.conf` publica editor, terminal y pintar. `test_gui.py` comprueba los tres
modulos, la latencia de la sesion y `help` ejecutado desde la terminal; `test_all.py`
incorpora el paso `gui`. Validado con `test_gui`, `test_core`, `test_files_editor`,
`test_presentation`, `test_utf8_keyboard` y `smoke_test`.

## 2026-11-03 Ventana HD y conectividad IPv4
El perfil por defecto pasa de 1280x800 a 1920x1080 para que la ventana de QEMU
llene una pantalla Full HD; se anade `FULLSCREEN=1`/`--fullscreen` (y
`QEMU_FULLSCREEN=1`) que pasa `-full-screen`, y el perfil compacto sigue en
`make iso RES=1280x800`. `run_vm.py` imprime el comando efectivo.
El RTL8139 gana IPv4 con checksum RFC 1071, eco ICMP con RTT medido por el PIT y
DNS por UDP contra 10.0.2.3, con compresion de nombres al leer la respuesta.
Nuevos comandos `RED PING <ip>`, `RED DNS <nombre>` y `RED INTERNET`; la ventana
Red muestra puerta, DNS, ultimo ping y ultimo nombre resuelto.
Se corrige la transmision: el chip rota entre cuatro descriptores y escribir
siempre `TSD0` dejaba la cola parada; ahora se usan cuatro buffers de 2 KiB con
`TSAD0-3`/`TSD0-3` rotados. El estado de enlace se lee de `BMSR` (0x64) en lugar
de `MSR`. Verificado en QEMU: `RED ARP` responde, `RED PING 10.0.2.2` devuelve
RTT y `RED DNS example.com`/`RED INTERNET` resuelven con Internet del anfitrion.
`test_gui.py` comprueba ARP, el eco a la puerta y el envio de la consulta DNS.

## 2026-11-03 Reloj en la barra y retirada de la app de dibujo
La barra superior muestra la hora local en formato `HH:MM`, leida del CMOS por
`src/rtc.c` (`rtc_now`/`rtc_text`: BCD o binario, 12/24 h y espera al refresco).
QEMU pasa a `-rtc base=localtime,clock=host` en el `Makefile` y en
`scripts/qemu_config.py`, asi que la hora coincide con el anfitrion. La barra solo
se repinta cuando cambia el minuto y el nuevo comando `HORA`/`TIME` imprime
`HH:MM:SS DD/MM/AAAA`. Se retira `apps/pintar.c` y su modulo de `limine.conf`;
`test_gui.py` valida ahora el reloj por COM1 y solo exige `editor.elf` y
`terminal.elf`.

## 2026-11-03 Resolución 1280x720 y ventana de QEMU ajustada al modo
`RES` pasa a 1280x720 (`limine.conf` versionado declara 1280x720x32) y
`DISPLAY_QEMU` a `gtk,zoom-to-fit=off`: `make run`, `debug`, `trace` y `uefi`
abren la ventana del emulador con el tamaño exacto del modo del huésped, sin
escalarla a la pantalla del anfitrión. Para llenar la pantalla siguen
disponibles `DISPLAY_QEMU=gtk,zoom-to-fit=on` y `FULLSCREEN=1`, y los perfiles
1920x1080 y 1280x800 con `make iso RES=...`. `run_vm.py` usa el mismo valor por
defecto. El escritorio usa el juego compacto de tipografías (escala 1) y el
doble buffer baja a 3,52 MB.

## 2026-11-03 Cierre de ventanas, desplazamiento y editor con selector
El cierre de ventanas dejaba restos: el botón de la barra de título marcaba el
slot como libre sin invalidar su rectángulo, así que el escritorio conservaba el
último fotograma. Ahora hay una única `close_window()` que daña el área (más el
dock), la usa también `toggle_window`, `gui_app_close` y `handle_press`, y
`gui_app_close` repinta y vuelca en el acto para que `GUI RUN` no deje restos al
volver a la consola. `repaint_pending()` informa por COM1
`GUI: VENTANA CERRADA <titulo> LIMPIA|CUBIERTA|CON RESTOS` para validarlo sin
capturas.

La ventana Archivos estrena barra de desplazamiento vertical (lista y visor),
botones Nuevo/Carpeta/Renombrar/Borrar/Editar y la tecla `E` para abrir el
archivo marcado en el editor. La barra es compartida con las apps mediante el
mismo criterio de pulgar proporcional. `host_api_t` sube a ABI 4 con
`launch_file`: la ruta viaja del escritorio a la app y se consume una sola vez.
El kernel añade `GUI EDIT <archivo>` y `elf_find()`.

`apps/editor.c` se reescribe: arranca con plantilla en blanco sin nombre, barra
con Nuevo/Abrir/Guardar, selector interno del sistema de archivos (navegación,
`..`, scrollbar y campo de nombre al guardar), `TAB` abre el selector y alterna
Abrir/Guardar, límite de 1023 bytes del FS y marcadores `editor.elf: ABIERTO`,
`GUARDADO` y `SIN ARCHIVO` por COM1. `apps/terminal.c` añade barra vertical con
pulgar arrastrable, `Inicio`/`Fin` para ir a los extremos y vuelta al final al
ejecutar. El teclado PS/2 entrega `TAB` (byte 9); la shell lo ignora.
`test_gui.py` comprueba plantilla nueva, ambos modos del selector, guardado en la
ruta elegida, lectura del archivo desde la terminal, `GUI EDIT` y la limpieza
del área al cerrar el editor.

La verificación con capturas (`scripts/verify_editor_capture.py`, imágenes
`artifacts/verificacion-*.png`) destapó dos fallos que las pruebas por serie no
podían ver. `raise_window()` comparaba `window->z == z_top` y una ventana nueva
llega con ambos a 0, así que `redraw_region()` —que recorre z desde 1— nunca la
pintaba: con `GUI RUN` o `GUI EDIT` la app corría y respondía, pero su ventana
no aparecía hasta abrir otra desde el dock. Y el marco sumaba margen solo al
alto, de modo que las últimas columnas de la superficie de la app quedaban
recortadas (la barra del editor caía justo ahí); ahora el ancho reserva
`margin * 2`. El script abre terminal y editor por QMP, captura con
`screendump`, comprueba las barras y el selector, y abre/cierra la ventana de
aplicaciones para dejar constancia de que el escritorio vuelve limpio.

## 2026-11-03 Terminal sin recortes, archivos internos y fondo con imagen
La terminal cortaba el texto porque partía de 82 columnas fijas aunque el área
útil dependiera del ancho de la ventana. Ahora mide cada carácter con
`text_width`, ajusta por palabras y retrocede al último espacio; la línea de
entrada recorta por delante lo que no cabe y el historial crece a 300 líneas con
su ancho guardado por fila.

El sistema de archivos incorpora archivos internos del SO en `/etc` (motd,
os-release, hostname, fstab), `/boot/limine.conf`, `/proc` (version, cpuinfo) y
`/wallpapers/wallpaper.jpg`. Se siembran en cada arranque e importación de disco
con una tabla paralela `system_nodes[]` (el snapshot de 1064 B por nodo no
cambia) y se restauran sus contenidos, de modo que `RM`, `REN`, `COPY` y las
escrituras fallan con `ARCHIVO DEL SISTEMA: SOLO LECTURA`. `fs_list_dir` los
marca `[SYS]`; la ventana Archivos los pinta en ámbar con `RO` y deshabilita
Borrar/Renombrar; el editor los abre con `[solo lectura]` y bloquea las teclas de
edición. `host_api_t` sube a ABI 5 con `is_system`.

Nuevo pipeline de imágenes en el anfitrión: `scripts/build_images.py` convierte
`images/*.jpg|png` a RGB565, recorta `wallpaper` al aspecto de `RES` y escribe un
blob validado que `objcopy` enlaza en `.rodata.images`; `src/images.c` lo
recorre y `gui.c` escala el fondo con tablas por columna y fila, con el degradado
como respaldo si no hay imágenes. La ventana **Imágenes** del dock lista las
imágenes empaquetadas con tamaño y vista previa, y abrir un `.jpg`/`.png` desde
Archivos lleva al visor. Objetivos nuevos `make images` e `make images-check`;
requiere Pillow. `test_gui.py` comprueba el sembrado de archivos internos, el
fondo por imagen, el visor desde el dock y el editor en solo lectura sobre
`/etc/motd`; `README.md` documenta las rutas, el pipeline y los límites.

## 2026-11-03 Recorte por caracteres y ventana activa
La salida de una orden en la terminal se truncaba siempre en el mismo punto:
`api->exec()` copiaba en un buffer de 640 B y `HELP` (unos 2.6 KiB) quedaba
cortado a media palabra. Sube a 4096 B y el eco `OS> orden` arranca en línea
nueva cuando la salida anterior no terminó con `\n`, de modo que dos salidas
seguidas ya no se pegan. Sobre el dibujo, `draw_clipped()` mide la cadena antes
de pintarla y, si no cabe, la recorta por puntos de código UTF-8 completos: se
aplica a la ruta, a cada renglón y al pie de ayuda, que así nunca invaden la
barra de desplazamiento ni el borde del recuadro.

Con varias ventanas abiertas cada tecla se entregaba a `files_key()` y a
`images_key()` a la vez. El escritorio guarda ahora `focus_slot` y solo la
ventana activa recibe el teclado (`active_key()`); el foco lo toma la ventana al
abrirse, al pulsarla o al subirla desde el dock, y al cerrarla hereda la de más
arriba. La activa se distingue por el título y el marco en azul de acento y
emite `GUI: VENTANA ACTIVA <titulo>` (o `GUI: SIN VENTANA ACTIVA`) por COM1;
`gui_pump()` descarta las teclas si la app no tiene el foco. `test_gui.py`
comprueba el cambio de foco desde el dock y que las flechas ya no mueven la
imagen de la ventana inactiva; `README.md` describe el reparto del teclado.

El marco era `gui_frame()`, un rectángulo de un píxel que dejaba un pico en cada
esquina del panel redondeado. `round_frame()` recorre el mismo trazado que
`gui_round()` y pinta por filas el tramo que la curva gana respecto a la
anterior, de modo que el contorno (azul en la activa) sigue las esquinas y queda
continuo; la sombra se redondea también para no asomar cuadrada por detrás.

## 2026-11-04 CI con Pillow y aviso de dependencia
El workflow falló en `Compilación y pruebas de host` al generar el blob de
imágenes: el runner no trae Pillow y `build_images.py` terminaba con el traceback
del import. `ci.yml` instala ahora `python3-pil` junto al resto de dependencias y
lo verifica en el paso de versiones; `build_images.py` comprueba la dependencia al
inicio de `build()` y sale con un mensaje claro en lugar de una traza. Los pasos
del workflow (ISO, check_elf, PMM/heap, UTF-8, planificador, py_compile y
protocolo TC/1) se repitieron en local: todos pasan. Las acciones suben a
`actions/checkout@v7` y `actions/upload-artifact@v7` para salir de Node 20, que
el runner ya solo mantiene por compatibilidad forzada a Node 24.

## 2026-11-04 Invitado Linux de compatibilidad para LibreOffice
El kernel sigue sin modo usuario, paginación ni syscalls, así que no ejecuta
binarios de terceros. Para abrir el ecosistema Linux se usa el protocolo `linux`
que ya implementa Limine 8.7: la ISO incorpora un kernel Alpine 6.18.60-virt y su
initramfs, y `limine.conf` gana la entrada `Modo compatibilidad: Linux +
LibreOffice`. `make guest` aprovisiona el invitado con podman sin privilegios
(Alpine 3.24 + LibreOffice + Xorg + i3) y lo empaqueta con `mkfs.ext4 -d` en
`vendor/guest/rootfs.img`; `make iso-guest` genera una variante con
`default_entry: 2` para arrancarlo sin teclas. `scripts/guest/toolchain-init` es
el PID 1 del invitado: monta `proc/sys/devtmpfs`, anuncia hitos por `ttyS0`
(`GUEST: init start`, `X up`, `session ready`) y lanza Xorg con `modesetting`
sobre el framebuffer del firmware más LibreOffice Writer maximizado.
`make test-guest` verifica la ISO y el arranque completo con captura QMP. La ISO
principal no cambia de comportamiento: sin invitado aprovisionado se genera igual
y `timeout: 0` sigue arrancando ToolchainOS al instante. Los binarios de terceros
(vmlinuz, initramfs, licencias y origen) viven en `vendor/guest/`; la imagen de
2.5 GiB no se versiona y `update_manifest.py` la ignora. Validado en QEMU UEFI con
KVM: LibreOffice Writer a 1280x720 con teclado y ratón PS/2.

## 2026-11-04 Personalidad Linux: BusyBox ejecutado por el kernel propio
El kernel gana la capacidad de atender un binario Linux real sin ceder el arranque.
`src/linux_abi.c` y `src/linux_enter.asm` programan `EFER.SCE`, `STAR`, `LSTAR` y
`FMASK` para que la instruccion `SYSCALL` entre en un manejador propio; habilitan
SSE en `CR0`/`CR4` (el kernel se compila con `-mgeneral-regs-only` y musl usa
`xmm`), arman la pila con `argc`/`argv`/`auxv` y despachan las llamadas que usan
los applets: identidad, `brk`/`mmap`/`munmap`, tiempo, descriptores sobre el
sistema de archivos en RAM (`getdents64`, `sendfile`, `ioctl` de terminal),
senales vacias y `exit_group`. `elf.c` expone `elf_load_image`/`elf_free_image`
para reutilizar el cargador PIE sin pasar por la GUI. La orden `LINUX <applet>`
(E56) ejecuta el BusyBox de Alpine aprovisionado por `scripts/linux_bin.py` con
podman; el binario viaja como modulo Limine en `build/toolchain-os-busybox.iso`,
una variante que no altera la ISO entregada. `make test-linux-bin` verifica ISO,
programacion de SYSCALL y los applets `echo`, `uname -m`, `ls /` y
`cat /etc/hostname` con codigo 0. Los tres fallos que costo depurar quedan
documentados por su sintoma: `call` en lugar de `jmp` al `_start` (argc leia la
direccion de retorno), `#UD` por CR4.OSFXSR y perdida de `rdx`/`r8`/`r9`/`r10` al
restaurar registros (syscall de Linux los conserva y el asm de musl solo marca
`rcx`/`r11`). BusyBox (GPL-2.0) se ejecuta como programa separado: el kernel
sigue siendo MIT y su origen, version y licencia viven en `vendor/linux-bin/`.

## 2026-11-04 Gnumeric y Abiword en el invitado, y ordenes delegadas en BusyBox
El invitado de la seccion 12 gana dos aplicaciones de oficina. Ted no existe en
los repositorios de Alpine (`apk` no lo conoce), asi que se usa Abiword 3.0 como
procesador de texto y Gnumeric 1.12 como hoja de calculo: sobre un invitado que
ya trae LibreOffice cuestan +28,8 MiB y 6 paquetes, porque comparten GTK3, Pango
y Cairo. La aplicacion se elige con `toolchain.app=<writer|gnumeric|abiword>` en
la linea de comandos del kernel: `scripts/guest/toolchain-init` la lee de
`/proc/cmdline` y anuncia `GUEST: app <nombre>` y `GUEST: <nombre> running`.
`make run-app GUEST_APP=gnumeric` y `make test-app GUEST_APP=abiword` construyen
`build/toolchain-os-app-<app>.iso` (copia de la del invitado con el parametro
anadido) y la prueban con captura QMP; sin `GUEST_APP` todo sigue igual y abre
Writer. `guest_linux.py` detecta que la lista de paquetes crecio y ejecuta `apk`
sobre el root existente en vez de reinstalar el gigabyte de LibreOffice.
En `test_linux_guest.py` se corrigio un fallo latente del sondeo: el offset
saltaba al final del log, de modo que dos hitos llegados entre dos sondeos
hacian que el intermedio se diese por perdido.
La orden `LINUX` deja de ser el unico camino al binario: con el en la ISO, `LS`,
`TREE` y `OPEN` delegan en los applets reales (`ls`, `find`, `cat`) mediante
`linux_applet()` y sin imprimir el preambulo de estado, y se anaden `GREP`, `WC`,
`SORT`, `HEAD`, `TAIL`, `FIND`, `STAT` y `OD`, que el kernel no implementaba. La
ISO entregada no lleva binario, asi que alli se conserva el codigo propio y las
pruebas de archivos, nucleo y editor no cambian. Verificado en general:
`make check`, `smoke_test`, `test_core`, `test_gui`, `test_presentation`,
`test_linux_bin` (que ahora comprueba la delegacion sin ruido) y `test-app` con
Gnumeric y Abiword en QEMU UEFI con KVM a 1280x720.

## 2026-11-04 Retirada del invitado Linux: todo dentro de ToolchainOS
Se descarta la via externa: Limine ya no arranca un kernel Linux ajeno y desaparece
la entrada `Linux + LibreOffice`. Borrados `scripts/guest/`,
`scripts/guest_linux.py`, `scripts/test_linux_guest.py` y todo `vendor/guest/`
(vmlinuz, initramfs, rootfs de 2,5 GiB, licencias y origen), junto con los objetivos
`guest`, `guest-check`, `run-guest`, `test-guest`, `run-app`, `test-app` y
`iso-guest`. En `scripts/qemu_config.py` se retiran `ISO_GUEST`, `OVMF_CODE`,
`OVMF_VARS`, `uefi()` y `guest_disk()`; `run_vm.py` pierde `--linux` y
`--guest-iso`; `update_manifest.py` deja de excluir `rootfs.img`. La ISO que se
construye ya no copia `boot/linux/` ni concatena la entrada del invitado.
LibreOffice, Gnumeric y Abiword quedan fuera del alcance del proyecto: son binarios
dinamicos con `ld-musl` y GTK3, y este kernel no tiene modo usuario, cargador
dinamico, hilos ni senales. Lo unico que ejecuta software de terceros es la
personalidad Linux (seccion 12), con BusyBox estatico en ring 0. README, INDICE y
LEEME se renumeran a 12 secciones; las entradas anteriores de este registro se
conservan como historia de lo que se probo y se retiro.

## 2026-11-05 Hipervisor VT-x, fase 1: VMXON y VMXOFF
El kernel detecta VT-x (`CPUID.1:ECX[5]`), lee `IA32_FEATURE_CONTROL` (0x3A) y la
revision del VMCS en `IA32_VMX_BASIC` (0x480), reserva una pagina de 4 KiB del PMM,
ajusta CR0/CR4 a los bits fijos del modelo, activa `CR4.VMXE` y ejecuta `VMXON`;
`VMXOFF` y la liberacion de la region deshacen todo. Nuevos `include/vmx.h`,
`src/vmx.c` y `src/vmx_inst.asm`; `cpuid` en `src/cpuinfo.c` pasa a ser publico como
`cpu_cpuid`. El comando `VMX` entra y sale, `VMX STATUS` solo informa, y `HELP`
publica la linea nueva. `make test-vmx` (con KVM anidado) comprueba el informe,
`VMXON=OK` y `VMXOFF=OK`; con TCG sale `SKIPPED` (77) y `VMX` responde `SOPORTE=NO`.
Dos fallos reales quedaron documentados en la seccion 13 del README: el operando de
`VMXON` es una direccion de memoria que contiene la direccion fisica de la region
(pasar la region hace que el puntero sea su propio contenido y devuelve
VMfailInvalid), y VT-x rechaza `VMXON` con #GP si CR0/CR4 no respetan
`IA32_VMX_CR0/CR4_FIXED0/1`. Suites existentes sin cambios: `check`, `smoke`,
`test-core`, `test-files`, `test-presentation` (54 textos), `test-gui` y
`test-linux-bin` siguen en verde; el hipervisor solo se activa con `VMX`.

## 2026-11-05 Hipervisor nativo en el escritorio y apagado total (APAGAR)
El hipervisor deja de ser una orden de consola: el dock estrena un boton
**Hipervisor** (icono de chip) con ventana propia que muestra soporte VT-x,
estado, revision del VMCS, region VMXON y el puerto `PM1a_CNT`, y ofrece
**Entrar VMX**, **Salir VMX** y **Apagar** (teclas `E`, `X` y `A`). `GUI
HIPERVISOR` abre el escritorio con esa ventana ya activa y `GUI_WINDOW_COUNT`
sube a 7. Todo lo ejecuta el kernel en ring 0: no hay QEMU ni hipervisor
anfitrion de por medio.
Nuevo `include/power.h` + `src/power.c` con `power_off()`: sale de VMX si
estaba activo, cierra el escritorio, sincroniza el sistema de archivos y envia
ACPI S5 en `PM1a_CNT` (0x604 de QEMU y 0xB004 de Bochs) con `SLP_TYP=5` y, para
QEMU, `SLP_TYP=0`; si el firmware no responde, detiene la CPU avisando por COM1.
La orden `APAGAR` queda reservada a la shell y el agente no puede lanzarla.
`VMX OFF` sale de VMX sin entrar. `make test-apagar` verifica ambos caminos
(consola y ventana) esperando el evento QMP `SHUTDOWN` con KVM y con TCG.
El parseo ACPI se descarto: la FADT vive en la memoria reservada baja, que el
HHDM de Limine no mapea, y leerla provocaba `#PF` (CR2 `0xFFFF80000000040E` en
el EBDA y `0xFFFF8000000F5320` en la RSDT); el motivo y los puertos usados
quedan en la seccion 14 del README.

## 2026-11-05 Retirada de VMX y hoja de calculo nativa en el escritorio
El hipervisor se retira por completo: se borran `include/vmx.h`, `src/vmx.c`,
`src/vmx_inst.asm`, `src/vmx.asm` y `scripts/test_vmx.py`; `src/gui.c` pierde
`WIN_HYPER`, `SLOT_HYPER`, `gui_open_hypervisor`, `draw_hyper_content` y el sexto
boton del dock vuelve a ser el ultimo, con `GUI_WINDOW_COUNT` de nuevo en 6;
`src/kernel.c` deja de despachar `VMX` y publica `GUI HOJA` en lugar de
`GUI HIPERVISOR`; `src/power.c` ya no sale de VMX antes del S5 y conserva el
apagado por `PM1a_CNT` (0x604 y 0xB004). `HELP` y `test_presentation.py` bajan a
55 textos sin las lineas de VMX.
La ofimatica pedida se resuelve dentro del propio sistema: nueva app
`apps/hoja.c` (ABI 5, 800x520) con rejilla de 8x16, edicion por teclado y raton,
formulas `=` con `+ - * /`, parentesis, referencias A1..H16 y funciones SUMA,
PROMEDIO, MIN, MAX y CONTAR sobre celda o rango, en aritmetica entera porque las
apps no enlazan libgcc. Guardar y Abrir trabajan sobre `hoja.csv` del directorio
actual, legible desde la terminal, y la app emite marcadores `hoja.elf:` por COM1
para validarla sin capturas. `limine.conf` publica `hoja.elf` y `test_gui.py`
comprueba apertura, formula, edicion y guardado (con mapa US para escribir `=`,
restaurado despues a ES). Verificado: `make iso`, `check_elf`, `test_gui`,
`test_presentation`, `test_apagar` (evento QMP `SHUTDOWN` con KVM), `test_core`,
`smoke_test` y `test_files_editor`. Nuevo `scripts/verify_hoja_capture.py`, que
abre `GUI HOJA` por QMP, escribe tres importes y una formula de rango y deja la
captura `artifacts/e56-hoja-calculo.png` como evidencia visual.

## 2026-11-04 USB: controladora, memorias, arranque desde pendrive y Secure Boot
Nuevo subsistema USB por sondeo, sin IRQ: `src/usb.c` localiza la controladora
xHCI por PCI (clase 0x0C, subclase 0x03, progif 0x30), mapea su BAR con
`src/mmio.c` y monta anillos de comandos y de eventos. El hueco PCI no aparece
en el mapa de Limine, asi que `mmio_map()` recorre la paginacion de 4 niveles
desde CR3, afina paginas de 1 GiB y 2 MiB a 4 KiB y las marca PWT|PCD.
La enumeracion usa Enable Slot, Address Device, descriptores y
SET_CONFIGURATION; el almacenamiento masivo BOT/SCSI usa CBW/CSW de 31/13 bytes
con paginas DMA separadas por etapa (INQUIRY, TEST UNIT READY, READ CAPACITY(10),
MODE SENSE, READ(10) y WRITE(10)). `src/usb_fat.c` interpreta MBR/GPT y monta
FAT12/16/32 de solo lectura (raiz, cadenas de clusteres, etiqueta) y detecta sin
descifrar LUKS, BitLocker, exFAT y NTFS. Comandos nuevos en la shell: USB INFO,
PUERTOS, LISTAR, VER, IMPORTAR, LEER, ESCRIBIR y REINICIAR; el arranque publica
controladora, medio, particiones, sistema montado y avisos de cifrado.
La ISO hibrida se copia a un pendrive con `scripts/escribir_usb.py`, que enumera
con lsblk, rechaza discos del sistema o montados y exige la frase exacta
ESCRIBIR <dispositivo>. `scripts/secure_boot.py` genera claves propias con
openssl, firma BOOTX64.EFI con sbsign y `make secureboot-iso` reconstruye la ISO
firmada; `scripts/test_usb.py` cubre FAT32, escritura y persistencia de un
sector, LUKS, solo lectura y ausencia de dispositivos, y
`scripts/test_secureboot.py` demuestra con OVMF que la ISO sin Secure Boot
arranca y que con Secure Boot el cargador sin firmar se rechaza. Verificado:
`make iso`, `make test-usb` y `make test-secureboot`.

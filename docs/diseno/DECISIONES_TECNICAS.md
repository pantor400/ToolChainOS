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

## V3.2 — escritorio, red y apps ELF
Interfaz grafica de tema claro con doble buffer RGB propio en 0x00RRGGBB, no en
MMIO. Se asigna con el PMM: 1280x720 son 3,5 MB, 1280x800 son 4,1 MB y
1920x1080 son 8,3 MB, fuera
del tope del heap. La conversion al framebuffer respeta los shifts de mascara y
tiene ruta rapida para 16/8/0. El cursor se pinta directo sobre el dispositivo
tras el volcado: sin guardar fondo no hay estelas ni lecturas de MMIO.
Sin SSE/FPU (-mgeneral-regs-only) no se rasteriza TTF en caliente: las
tipografias se hornean en el anfitrion con Pillow en 6 tamanos 4bpp
(scripts/build_fonts.py) y el kernel elige juego por resolucion. Los iconos son
procedurales sobre lienzo 24x24 escalado por enteros; no hay PNG ni decoder.
La resolucion la fija Limine y se selecciona en build con RES=1280x720 (perfil
por defecto), RES=1920x1080 (HD) o RES=1280x800 (compacto); no hay cambio en
caliente del modo de video.
Raton por i8042 + IRQ12 (0xA8, config byte, 0xF6, 0xF3 0x64, 0xE8 0x03, 0xF4),
paquetes de 3 bytes resincronizados por el bit 3 y respaldo por sondeo. Sin
driver Synaptics/Elan: el touchpad se anuncia como compatibilidad PS/2 relativa.
Red: enumeracion PCI 0xCF8/0xCFC buscando 10EC:8139, solo BAR de E/S, bus
master, RX 8 KiB+16 con WRAP y modo promiscuo, cuatro descriptores TX rotados
(TSAD0-3/TSD0-3) e IP fija 10.0.2.15/24 con puerta 10.0.2.2 y DNS 10.0.2.3.
Sobre Ethernet y ARP hay IPv4 (checksum RFC 1071), eco ICMP y consultas DNS por
UDP: RED PING, RED DNS e RED INTERNET. Sin TCP/IP, DHCP, IRQ del dispositivo ni
IPv6.
Apps: PIE estatico sin libc; el cargador localiza .rela.dyn por cabeceras de
seccion (sh_info==0) porque el enlace estatico no emite .dynamic, y solo aplica
R_X86_64_RELATIVE. Trampolin NASM con pila propia y elf_abort desde el manejador
de excepciones: una app que falla vuelve al kernel. No hay modo usuario; la
frontera es el contrato host_api_t. Detalle: ../diseno/GUI_RED_APPS.md.
ESC se entrega como 27 desde el teclado PS/2; la shell lo ignora y la GUI lo usa
para cerrar la ventana o la sesion.

## 2026-11-03 — fluidez, ABI 3 y QEMU
El parpadeo tenia tres causas acumuladas: volcado completo del framebuffer en cada
fotograma, mezcla por pixel del fondo y del sombreado, y repintado del escritorio
por cada paquete del raton. Se sustituyen por un rectangulo sucio acumulado
(`invalidate`/`redraw_region`/`flush_region`), un gradiente precalculado de 256
filas y relleno solido para las sombras. El cursor se pinta en el dispositivo y
se restaura desde el back buffer; cuando no hay eventos ni dano el bucle ejecuta
`sti; hlt`. Las apps solo presentan al recibir un evento.
El fondo cacheado se prefiere a redibujar el degradado: 256 filas de 32 bits se
copian por bloques y el coste por fotograma depende solo de la zona sucia.
`host_api_t` sube a 3 porque anade campos al final de la tabla; la app comprueba
`abi_version` antes de usarlos y una version distinta se rechaza sin ejecutar.
El FS se expone con las mismas funciones `fs_*` del kernel (sin copia) y `exec`
reutiliza el despachador del shell con captura acotada de consola, rechazando
GUI, ESCRITORIO y HALT para que una app no anide otra sesion grafica.
QEMU se centraliza en `scripts/qemu_config.py`: KVM con CPU `host` cuando
`/dev/kvm` es accesible y TCG con `cpu max` como respaldo, 32 MiB de memoria de
video, reloj del anfitrion y cache de disco. Se prefiere KVM porque el coste de
TCG (mas aun sin `mtcg`) hacia perceptible el retardo del puntero; las pruebas
verifican el mismo comportamiento en ambos modos.

## 2026-11-03 — ventana HD y red IPv4
El huesped arrancaba a 1280x800 y en una pantalla Full HD la ventana quedaba
pequena. Sube el perfil por defecto a 1920x1080 (sigue disponible
`RES=1280x800`) y se anade `FULLSCREEN=1`, `--fullscreen` y `QEMU_FULLSCREEN=1`
para pasar `-full-screen`; no se toca `zoom-to-fit`, que evita que la ventana
recorte el modo del huesped en pantallas menores.
La red solo tenia Ethernet + ARP, asi que "tener red" no significaba tener
Internet. Se anade IPv4 minimo (cabecera de 20 B y checksum RFC 1071), eco ICMP
con RTT del PIT y DNS por UDP al 10.0.2.3 con compresion de nombres, expuesto
como `RED PING`, `RED DNS` y `RED INTERNET`.
El fallo que impedia transmitir era doble: se escribia siempre `TSD0` (el chip
rota entre cuatro descriptores y la cola se paraba) y el enlace se leia de `MSR`
en lugar de `BMSR`. Se usan cuatro buffers de 2 KiB con `TSAD0-3`/`TSD0-3`
rotados y `inw(0x64) & 4` para el enlace.
Sin TCP/IP no hay HTTP: comprobar Internet se limita a ping y DNS, que es lo
maximo verificable sin implementar una pila completa y sockets. `test_gui.py`
comprueba ARP, el eco a 10.0.2.2 y el envio de la consulta DNS; la resolucion
real depende de que el anfitrion tenga Internet.

## 2026-11-03 — reparto 1280x720 y ventana a escala 1:1
El perfil por defecto baja a 1280x720 para que el escritorio quepa comodo en
cualquier monitor. `DISPLAY_QEMU` pasa a `gtk,zoom-to-fit=off`, de modo que la
ventana del emulador mide exactamente el modo del huesped (1280x720) en vez de
estirarse hasta llenar la pantalla del anfitrion; `DISPLAY_QEMU=gtk,zoom-to-fit=on`
y `FULLSCREEN=1` siguen disponibles para las demostraciones. `limine.conf`
versionado declara 1280x720x32 y `run_vm.py` usa el mismo valor por defecto. Los
perfiles 1920x1080 y 1280x800 se conservan con `RES`.

## 2026-11-03 — reloj en la barra y fin de la app de dibujo
La barra superior mostraba resolucion e IP, pero ningun reloj. Se anade
`src/rtc.c`, que lee el CMOS tratando el bit de actualizacion en curso, el
formato BCD/binario y el modo 12/24 h; la barra pinta `HH:MM` a la derecha y
solo se invalida cuando cambia el minuto, asi que el repintado por regiones
sigue intacto. QEMU pasa de `base=utc` a `base=localtime,clock=host` para que la
hora mostrada sea la del anfitrion y no UTC, y `HORA`/`TIME` imprime la marca
completa por consola para poder validarla por COM1.
Se retira `apps/pintar.c`: era una demo de trazo sin uso dentro del guion y su
modulo ocupaba `limine.conf`. Quedan editor y terminal, que son las apps que
usan el FS y `exec`; `test_gui.py` deja de lanzar `pintar.elf` y comprueba el
reloj en su lugar.

## 2026-11-03 — cierre de ventanas, barras de desplazamiento y selector del editor
El boton de cerrar de una ventana del sistema ponia `used = 0` sin invalidar su
rectangulo, asi que el escritorio conservaba el ultimo fotograma hasta que otra
accion lo repintaba. Se centraliza en `close_window()`: dana el area de la
ventana (con su sombra) y el dock, cancela el arrastre y deja el slot libre; la
usan `toggle_window`, `gui_app_close` y `handle_press`. Como `GUI RUN` termina
sin otro ciclo de eventos, `gui_app_close` repinta y vuelca en el acto. El
repintado por regiones se mantiene: no se anade ningun repintado completo.
`report_close_cleanup()` compara el pixel central del area cerrada con el
gradiente del escritorio y publica `LIMPIA`, `CUBIERTA` (otra ventana encima) o
`CON RESTOS`, de modo que la prueba no depende de capturas.

Las barras de desplazamiento son las mismas en la ventana Archivos y en las dos
apps: pista con marco, pulgar proporcional (minimo 18 px) y `scrollbar_first()`
para traducir un clic o arrastre a la primera fila visible. Solo se dibujan si
`total > visibles`. El raton PS/2 no tiene rueda, asi que el arrastre del pulgar
y `Inicio`/`Fin` son la unica forma de recorrer el texto; el editor abandona el
seguimiento del cursor mientras se arrastra y lo recupera con la siguiente tecla.

El selector de archivos del editor es modal interno de la app (no del kernel):
recorre el FS con `nodes`/`cwd`/`chdir`/`cwd_path`, guarda el `cwd` de entrada y
lo restaura al cerrar para no mover el estado global que comparte con la ventana
Archivos. Guardar siempre escribe en la carpeta mostrada con el nombre tecleado.
El limite util es 1023 bytes (`FS_FILE_MAX_SIZE - 1`); el editor avisa al
llegar y no deja que `write_file` falle por tamano.

ABI 4 anade `launch_file`: `Archivos > Editar` y `GUI EDIT <archivo>` depositan
la ruta antes de `elf_run` y la app la consume en una unica llamada (el kernel la
borra al entregarla), lo que evita que una segunda ejecucion herede la ruta
anterior. `elf_find()` resuelve el indice del modulo por nombre. El teclado
entrega `TAB` como byte 9: la shell lo descarta por ser menor que 32 y las apps
lo usan para abrir el selector y alternar Abrir/Guardar.

La revision con capturas anadio dos correcciones al compositor. `raise_window()`
comparaba `window->z == z_top` para no repintar de mas, pero una ventana recien
creada llega con `z == 0` y ese es tambien el valor inicial de `z_top`, asi que
se quedaba en 0 y `redraw_region()`, que recorre `z` desde 1, no la dibujaba:
con `GUI RUN`/`GUI EDIT` la app se ejecutaba sin ventana visible. La guarda exige
ahora `z > 0`. Ademas el marco de la app reservaba margen solo en el alto, de
forma que las ultimas columnas de la superficie de la app caian fuera del
recorte; el ancho pasa a `width + margin * 2 + 2` y la superficie ocupa
exactamente el area interior. La verificacion visual vive en
`scripts/verify_editor_capture.py` y guarda `artifacts/verificacion-*.png`.

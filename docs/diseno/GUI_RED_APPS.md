# Escritorio, red, ratón y aplicaciones ELF

Documenta los subsistemas añadidos sobre V3.1: `src/gui.c`, `src/mouse.c`,
`src/net.c`, `src/elf.c`, `src/font_ui.c` y las apps de `apps/`. Todo el código
sigue compilando con `-mgeneral-regs-only`, sin libc y sin SSE/FPU.

## 1. Perfiles de resolución

La resolución la entrega Limine y se fija en el build:

```bash
make iso                     # 1280x720x32 (perfil por defecto, ventana 1:1)
make iso RES=1920x1080       # perfil HD (pantalla completa)
make iso RES=1280x800        # perfil compacto (mas alto)
make run                     # ventana GTK de 1280x720 pixeles reales
make run FULLSCREEN=1        # pantalla completa
```

La ventana del emulador se abre con `-display gtk,zoom-to-fit=off`
(`DISPLAY_QEMU`): sin reescalado, QEMU usa el tamano exacto del modo del huesped
(1280x720) y la resolucion interna no cambia. Para llenar la pantalla del
anfitrion esta `make run DISPLAY_QEMU=gtk,zoom-to-fit=on`, y `FULLSCREEN=1` (o
`run_vm.py --fullscreen`, o `QEMU_FULLSCREEN=1`) anade `-full-screen` para
eliminar las decoraciones.

`RES` se sella en `build/resolution.stamp`: cambiar de perfil reconstruye la ISO
y el `sed` sobre `build/iso_root/boot/limine/limine.conf` no toca el
`limine.conf` versionado.

### Ejecución en QEMU

`scripts/qemu_config.py` centraliza los argumentos del emulador y lo usan
`qemu_test.py`, `smoke_test.py`, `test_gui.py` y `run_vm.py`, de modo que el
Makefile y las pruebas arrancan la misma máquina:

- `-accel kvm -cpu host` si `/dev/kvm` es accesible; si no, `-accel tcg -cpu max`.
  Se puede forzar con `make run ACCEL=tcg` o `QEMU_ACCEL=tcg`.
- `-machine q35 -m 256M -smp 2 -vga std` y `-global VGA.vgamem_mb=32`, suficiente
  para 1920x1080x32 y para el doble buffer de las apps.
- `-rtc base=utc,clock=host` (el uptime no depende del reloj emulado) y disco con
  `cache=writeback,aio=threads` en las pruebas de persistencia.
- `-netdev user,id=net0 -device rtl8139,netdev=net0` para el driver de red; el
  lanzador interactivo (`scripts/run_vm.py` e `INICIAR_DEMO_LINUX.sh`) incluye
  esa red de usuario por defecto y la sustituye por `-nic none` con `--no-net`.

| Perfil | Escala | Fuente pequeña | Cuerpo | Título | Ventana de app |
|---|---|---|---|---|---|
| 1280x720 (defecto) | 1 | `reg15` | `reg21` | `bold30` | hasta 1240x580 |
| 1920x1080 (HD) | 2 | `reg21` | `reg30` | `bold40` | hasta 1860x860 |
| 1280x800 (compacto) | 1 | `reg15` | `reg21` | `bold30` | hasta 1240x620 |

`gui_init` rechaza cualquier framebuffer que no sea RGB de 32 bpp con máscaras de
8 bits y al menos 640x400, así que un modo inesperado cae en consola en vez de
dibujar basura.

## 2. Composición

- El lienzo canónico es `0x00RRGGBB` en memoria normal, no en MMIO.
- El doble buffer se reserva con `pmm_alloc_run`: 1280x720x4 = 3,52 MB y
  1920x1080x4 = 8,29 MB, muy por encima del máximo del heap (512 KiB), de ahí el
  uso directo del PMM.
- Cada `present` convierte al framebuffer respetando
  `red_mask_shift`/`green_mask_shift`/`blue_mask_shift`; con 16/8/0 se usa la ruta
  rápida (una escritura de 32 bits por píxel).
- **Repintado por zonas:** `invalidate()` acumula un rectángulo sucio; el bucle
  gráfico redibuja solo esa región (fondo cacheado, ventanas que la cruzan, barra
  y dock) y volca únicamente ese rectángulo. El fondo es un gradiente
  precalculado (`gradient[256]`), sin mezclar píxel a píxel, y las sombras usan un
  relleno sólido en vez de `gui_blend`.
- Cuando no hay eventos ni daño pendiente, el bucle ejecuta `sti; hlt`: la CPU
  duerme hasta la siguiente interrupción en lugar de repintar en vacío. Las apps
  solo llaman a `present()` cuando llega un evento.
- El cursor se dibuja **directamente en el framebuffer** después del volcado y se
  restaura desde el back buffer antes del siguiente, así que no deja estelas ni
  obliga a repintar la ventana entera al mover el ratón. La reposición ocurre en
  todos los modos de volcado y, si ni el puntero ni el daño cambiaron, no se
  redibuja nada.
- Todo dibujo pasa por `draw_target` (back buffer o superficie de la app) y por
  un rectángulo de recorte, lo que permite que las apps pinten sin salirse de su
  ventana.

## 3. Tema claro

| Rol | Color |
|---|---|
| Fondo superior / inferior | `0xEEF3FA` → `0xDDE6F3` |
| Panel | `0xFFFFFF` |
| Panel alterno | `0xF4F7FC` |
| Borde | `0xC9D5E6` |
| Texto / texto atenuado | `0x14213A` / `0x5A6B85` |
| Acento / acento oscuro / acento suave | `0x2470E8` / `0x1A4FB0` / `0xDBE9FF` |
| Correcto / aviso / error | `0x15803D` / `0xB45309` / `0xD92B2B` |

La paleta vive en `palette` (`gui_theme_t`) y se aplica igual en el escritorio,
en las ventanas del sistema y en `host_api_t`, de modo que una app y el sistema
comparten aspecto sin duplicar constantes.

## 4. Assets escalables

Dos mecanismos, ambos sin rasterizar tipografías en caliente:

1. **Tipografías horneadas.** `scripts/build_fonts.py` (Pillow + fuente del
   sistema) rasteriza 6 tamaños con cobertura de 4 bits y los emite como
   `include/font_ui.h` + `src/font_ui.c`. El kernel elige el juego según la
   resolución; regenerar es `make fonts`.
2. **Iconos procedurales.** Cada icono se dibuja sobre un lienzo lógico 24x24
   (`IX/IY/IS`) y se escala por enteros, así que no hay PNG ni dependencia de
   decodificadores. `HOST_ICON_*` expone el mismo catálogo a las apps.
3. **Imágenes horneadas.** `scripts/build_images.py` convierte `images/*.jpg|png`
   a RGB565, recorta `wallpaper` al aspecto de `RES` y escribe un blob con
   cabecera `TCOSIMG1` y entradas de 48 B; `objcopy` lo enlaza en
   `.rodata.images` y `src/images.c` lo valida al arrancar. No hay decodificador
   JPEG/PNG en el kernel; el fondo se escala con tablas por columna y fila y el
   visor muestrea la imagen a su ventana.

Consecuencia: el binario crece (~5,4 MiB de ELF con el fondo de 1280x720) pero el
arranque no decodifica nada y el render es una copia de máscaras o de píxeles ya
convertidos.

## 5. Escritorio y ventanas

- Barra superior con título, estado de red, resolución y reloj (HH:MM) del CMOS.
- Escritorio con tarjetas y dock inferior: Sistema, Archivos, Red, Entrada,
  Imágenes y Apps, más `Salir`. El fondo es `images/wallpaper.jpg` convertido a
  RGB565 y escalado al modo de vídeo; sin imágenes se usa el degradado.
- Ventanas del sistema (`WIN_FILES`, `WIN_SYSTEM`, `WIN_NET`, `WIN_INPUT`,
  `WIN_APPS`, `WIN_IMAGES`) y una ventana de aplicación (`WIN_APP`) que hospeda el
  lienzo de la app. Cada tipo abre con un tamaño proporcional a la resolución.
- **Ventana Sistema:** doce filas con CPU (vendor y modelo por CPUID), núcleos,
  RAM detectada y libre, heap, procesos e hilos, uptime del PIT, resolución, red
  (MAC/IP/enlace), ratón, teclado, despachos del planificador y archivos del FS.
- **Ventana Red:** MAC, IP, puerta, DNS, enlace, ARP, ultimo ping (con RTT),
  ultimo nombre resuelto, TX y RX. Los valores salen de `net_status()` y se
  actualizan con `RED PING`, `RED DNS` o `RED INTERNET` desde la shell.
- **Ventana Archivos:** navegación con flechas y `Enter`, visor del contenido,
  barra de desplazamiento vertical (lista y visor), filas de tamaño fijo y
  botones `Nuevo`, `Carpeta`, `Renombrar`, `Borrar` y `Editar`. Los atajos son
  `n` archivo nuevo, `c` carpeta, `r` renombrar, `d`/`x` borrar, `e` abrir el
  archivo marcado en el editor, `Backspace` sube de directorio y `ESC` cierra el
  visor o vuelve al escritorio. Las acciones usan el mismo `fs_*` del kernel,
  con mensajes de error en la propia ventana. `Editar` entrega la ruta al editor
  por el `launch_file` del ABI. Los nodos internos (`/etc`, `/boot`, `/proc`,
  `/docs`, `/wallpapers`) se pintan en ámbar con `RO` y deshabilitan
  `Borrar`/`Renombrar` (y `Nuevo`/`Carpeta` dentro de una carpeta del sistema).
  La última línea de la ventana muestra la ruta absoluta del elemento marcado
  (`files_node_path()`), o el mensaje de la última acción si lo hay.
- **Ventana Imágenes:** lista las imágenes del blob con su tamaño y una vista
  previa escalada; las flechas cambian de imagen y `ESC` la cierra. El pie indica
  la ruta del archivo dentro del sistema (`/wallpapers/<imagen>`). Abrir un
  `.jpg`, `.jpeg` o `.png` desde Archivos abre el visor en esa imagen por
  coincidencia de nombre con el blob; si el nombre no está empaquetado se avisa
  en la línea de estado en lugar de mostrar otra imagen. Emite
  `GUI: VISOR IMAGENES` y `GUI: IMAGEN <n> WxH`.
- **Galería sembrada:** `fs_seed_gallery()` (en `src/fs.c`) recorre el catálogo
  del blob al arrancar y crea en `/wallpapers` una ficha por imagen con el nombre
  del archivo de origen, sus dimensiones y sus bytes RGB565. El nombre sale de
  `images_name()` y la extensión de `images_extension()`, que lee el último `u32`
  de cada entrada del blob (1 jpg, 2 jpeg, 3 png; 0 en blobs antiguos se trata
  como jpg). Así el explorador y el visor enumeran el mismo conjunto de imágenes
  y cada fila tiene una ruta real. Los ficheros de `/docs` se siembran igual y
  documentan la consola, el escritorio y el propio árbol.
- Arrastre por la barra de título, cierre por el botón o con `ESC`. Todo cierre
  pasa por `close_window()`, que invalida el área y el dock; la siguiente
  presentación devuelve el fondo y no quedan restos del contenido anterior.
- `GUI` abre la sesión interactiva; `GUI DEMO` dibuja un fotograma y vuelve a la
  consola; `GUI RUN <indice>` lanza un módulo ELF directamente.

## 6. Ratón y touchpad

Secuencia de arranque sobre el i8042: `0xA8`, lectura/escritura del byte de
configuración con IRQ12 habilitada y reloj activo, `0xF6` (valores por defecto),
`0xF3 0x64` (100 muestras/s), `0xE8 0x03` (8 cuentas/mm) y `0xF4` (habilitar).
La secuencia se ejecuta con las interrupciones cerradas: el ACK `0xFA` llega por
el mismo puerto 0x60 y una IRQ del PS/2 podría consumirlo.

- La cascada del PIC maestro queda abierta en `interrupts_init()` (máscara
  `0xFA` = IRQ0 + IRQ2) y la IRQ1 del teclado sigue enmascarada. Sin la cascada,
  la IRQ12 del ratón nunca llega al procesador.
- `mouse_init()` desenmascara solo IRQ12 en el PIC esclavo (`inb(0xa1) & ~0x10`).
- La IRQ12, la IRQ1 (atendida por el mismo despacho si se habilita) y el sondeo
  comparten el i8042: `mouse_irq()` drena el controlador en cada interrupción y
  `mouse_poll()` hace lo mismo con `cli`/`sti`, repartiendo cada byte por el bit 5
  del estado (`1` ratón). `keyboard_read()` llama a `mouse_poll()` en vez de leer
  y descartar bytes, de modo que un paquete de 3 bytes nunca se rompe.
- `mouse_state()` toma posición, botones y contadores con las interrupciones
  cerradas, para no mezclar una X nueva con una Y vieja.
- Los paquetes de 3 bytes se resincronizan por el bit 3 del primer byte; el
  contador `DESCARTES` (visible en `RATON`) delata cualquier desalineación. Los
  paquetes con desbordamiento aplican el byte recibido en lugar de anular el
  movimiento.
- `mouse_poll()` replica la lectura en sondeo: si la IRQ no llega (por ejemplo en
  un arranque UEFI con el controlador en otro estado), el puntero sigue
  respondiendo.
- Coordenadas acotadas al framebuffer en el propio driver.

**Touchpad:** no hay driver Synaptics/Elan absoluto. Se anuncia explícitamente
como «modo compatibilidad PS/2 (relativo)», que es lo que ofrece cualquier
touchpad moderno por defecto. Un protocolo absoluto requeriría cambiar el
controlador a modo avanzado (comandos `0xF4`/`0xE1` sobre el dispositivo 2) y
queda fuera de esta entrega.

## 7. Red: PCI + RTL8139 + IPv4

- Enumeración por `0xCF8/0xCFC` en los buses 0-7 buscando `10EC:8139`.
- Solo BAR de E/S (el driver rechaza BAR de memoria), bus mastering habilitado.
- Anillo de recepción de 8 KiB+16 con bit WRAP y `RCR` en modo promiscuo; **cuatro**
  buffers y descriptores de transmisión que se rotan (`TSAD0-3`/`TSD0-3`).
  Escribir siempre `TSD0` funciona una vez y deja la cola parada, porque el chip
  (y QEMU) avanza al descriptor siguiente: es el fallo que impedía transmitir.
- El enlace se lee de `BMSR` (0x64, bit 2). `MSR` (0x58) no es el registro de
  estado básico y devolvía siempre «sin enlace».
- Tramas: Ethernet + ARP, IPv4 con checksum RFC 1071, ICMP echo y UDP sin
  checksum (válido en IPv4) hacia el puerto 53.
- `RED ARP` resuelve la puerta por ARP; `RED PING <ip>` envía un eco ICMP y
  muestra el RTT en ms; `RED DNS <nombre>` codifica el nombre (etiquetas +
  compresión al leer la respuesta) y devuelve la primera respuesta tipo A;
  `RED INTERNET` encadena ping a 10.0.2.2 y resolución de `example.com` por
  10.0.2.3. La ventana Red muestra puerta, DNS, último ping y último nombre.
- IP fija 10.0.2.15/24 con puerta 10.0.2.2 y DNS 10.0.2.3 (red de usuario de
  QEMU, SLIRP).

**Límite:** hay Ethernet, ARP, ICMP y DNS, pero no pila TCP/IP, DHCP ni IPv6.
Sin TCP no hay HTTP ni sockets: no se puede navegar. Tampoco se atiende la IRQ
del dispositivo: la recepción es por sondeo desde `net_poll()`, invocado en la
shell y en el bucle gráfico, y cada espera (ARP 0,5 s, ping 1 s, DNS 3 s) se
resuelve dentro de ese sondeo.

## 8. Aplicaciones ELF64

- Se compilan como PIE estático sin libc:
  `-pie -fpie -fno-plt -static-pie -Wl,-e,main -T apps/app.ld`.
- `src/elf.c` valida magia, `ELFCLASS64`, máquina y `ET_DYN`, reserva un tramo
  contiguo en el PMM, copia los segmentos `PT_LOAD` y aplica las reubicaciones.
- Las reubicaciones viven en `.rela.dyn`, que se localiza por cabeceras de
  sección (`sh_info == 0`) porque el enlace estático no emite `.dynamic`. Solo se
  aplica `R_X86_64_RELATIVE` (tipo 8); los PC32 de `.rela.text` no lo necesitan.
- `src/elf_enter.asm` presta a la app una pila propia de 4 páginas y garantiza la
  alineación previa a `call`. `elf_abort()` devuelve el control al kernel desde el
  manejador de excepciones, así que una app que falla no cuelga el sistema.
- Al terminar se liberan páginas y pila e imprime
  `ELF: FIN <nombre> CODIGO=<n>`.

Contrato `host_api_t` (`HOST_ABI_VERSION == 6`) recibido en `RDI`:

| Campo | Efecto |
|---|---|
| `write`, `finish`, `close` | Consola serie y cierre controlado |
| `window`, `width`, `height` | Ventana propia y tamaño del lienzo |
| `clear`, `rect`, `frame`, `text`, `icon`, `text_width` | Primitivas con el tema del sistema |
| `pump`, `present`, `ticks` | Bucle de eventos, volcado y reloj |
| `read_file`, `write_file` | Lectura y escritura de archivos del FS del kernel |
| `make_dir`, `remove_node`, `rename_node` | Crear carpeta, borrar y renombrar |
| `nodes`, `node_count`, `cwd`, `chdir`, `cwd_path` | Listado, directorio actual y navegación |
| `is_system` | Indica si una ruta es un archivo interno de solo lectura |
| `exec` | Ejecuta una orden del shell y devuelve su salida capturada |
| `key` | Siguiente tecla extendida pendiente (`KB_KEY_*`) |
| `launch_file` | Ruta con la que el escritorio lanzó la app; consume el valor |

Eventos: `HOST_EV_NONE`, `MOVE`, `DOWN`, `UP`, `KEY`, `CLOSE`; las coordenadas ya
vienen relativas al contenido de la ventana. Las teclas extendidas (flechas,
`Inicio`, `Fin`, `Supr`) llegan como `HOST_KEY_*` (`0x101`…), distintas de los
bytes ASCII; `HOST_KEY_TAB` sigue siendo `9`.

Aplicaciones incluidas:

- `apps/editor.c`: editor de texto de 1023 bytes (límite del FS) con plantilla
  en blanco sin nombre. Barra con `Nuevo`, `Abrir` y `Guardar`; `Abrir`/`Guardar`
  abren un selector interno con el árbol del FS (ruta actual, `..`, lista con
  barra de desplazamiento y campo de nombre al guardar). `TAB` abre el selector y
  dentro alterna Abrir/Guardar, `Enter` confirma y `Q` cancela. El área de texto
  muestra una barra vertical cuando el contenido no cabe y sigue al cursor salvo
  mientras se arrastra el pulgar. Guarda y carga con `read_file`/`write_file`.
  Los archivos internos se marcan `sistema` en el selector y se abren con
  `[solo lectura]`: las teclas de edición no los modifican y `store_file`/`dlg_save`
  rechazan escribir sobre ellos (`editor.elf: SOLO LECTURA <ruta>`).
- `apps/terminal.c`: terminal con scrollback de 300 líneas e historial de 8
  órdenes con las flechas y ejecución real vía `exec`; el ajuste se hace por
  píxeles con `text_width`, cortando por palabras, y la línea de entrada recorta
  por delante lo que no cabe. Barra vertical arrastrable cuando la salida no
  cabe, `Inicio`/`Fin` para los extremos, `limpiar` vacía la pantalla y `salir`
  cierra la ventana.
- `apps/hoja.c`: hoja de cálculo de 8x16. Barra con `Guardar`, `Abrir` y
  `Limpiar`, barra de fórmula con el nombre de la celda activa y rejilla con
  cabeceras A–H y 1–16. Las celdas guardan texto o una fórmula `=` con `+ - * /`,
  paréntesis, referencias `A1` y las funciones `SUMA`, `PROMEDIO`, `MIN`, `MAX` y
  `CONTAR` sobre celda o rango (`A1:B3`); todo en aritmética entera porque las
  apps no enlazan `libgcc`, con `#ERROR` y `#CICLO` como avisos. Persiste en
  `hoja.csv` del directorio actual con `read_file`/`write_file` y emite
  `hoja.elf: CELDA|FORMULA|GUARDADO|ABIERTO|…` por COM1.

El reloj de la barra sale de `rtc_now()` (`src/rtc.c`), que lee el CMOS
compensando BCD/binario, 12/24 h y el bit de actualización en curso. QEMU se
lanza con `-rtc base=localtime,clock=host`, así que muestra la hora del
anfitrión; `HORA` imprime lo mismo con segundos y fecha. Solo se repinta la
barra cuando cambia el minuto.

## 9. Verificación sin capturas

`scripts/test_gui.py` arranca QEMU sin ventana y conduce la shell por QMP
(`send-key`, `input-send-event`). Comprueba, solo con marcadores de COM1:

1. `RED: RTL8139` con MAC y contadores; `RED PING 10.0.2.2` responde con RTT y
   `RED DNS example.com` imprime la consulta hacia 10.0.2.3; `HORA` devuelve
   `HH:MM:SS DD/MM/AAAA`.
2. `RATON PS/2: ACTIVO` y, tras mover y pulsar por QMP, `IRQ_PAQUETES > 0`.
3. `APLICACIONES ELF DISPONIBLES` con `editor.elf`, `terminal.elf` y
   `hoja.elf`, y sin `hola.elf` ni `pintar.elf`.
4. `FS: ARCHIVOS DEL SISTEMA EN SOLO LECTURA` en el arranque.
5. `GUI_READY RES=1280x720` (o el perfil elegido con `RES`), `GUI_FRAME` y
   `GUI: FONDO IMAGEN <nombre> WxH`, con la latencia de la sesión medida entre
   `GUI` y `SESION FINALIZADA`.
6. `GUI: VISOR IMAGENES` tras pulsar el icono Imágenes del dock (clic por QMP),
   y `SESION FINALIZADA` al salir con `ESC`.
7. `editor.elf: PLANTILLA NUEVA`, `SELECTOR ABRIR`, `SELECTOR GUARDAR` y
   `GUARDADO /demo2.txt` al escribir el nombre y pulsar `Enter`; el archivo se lee
   después desde la terminal (`open demo2.txt`).
8. `GUI: VENTANA CERRADA Editor de texto LIMPIA` al salir del editor, que valida
   que el área cerrada vuelve al fondo del escritorio, y `ELF: FIN editor.elf
   CODIGO=0`.
9. `GUI EDIT demo2.txt` abre el editor con `ABIERTO /demo2.txt` (handoff ABI 6) y
   `gui edit motd` desde `/etc` devuelve `editor.elf: SOLO LECTURA /etc/motd`.
10. La terminal ejecuta `help` y su salida aparece por COM1 antes de volver al
    escritorio.
11. `hoja.elf: HOJA 8X16 LISTA ABI=6`, `ABIERTO /hoja.csv` al pulsar Abrir, la
    fórmula `=b1-a1` deja `FORMULA C1 =b1-a1 -> 2`, una celda nueva imprime
    `CELDA C2 10` y Guardar deja `GUARDADO /hoja.csv`, que la shell lee después.
    El bloque cambia a `TECLADO US` para escribir `=` y vuelve a `ES`.

La evidencia visual de la hoja se regenera con
`scripts/verify_hoja_capture.py`, que deja `artifacts/e56-hoja-calculo.png`.

```bash
make test-gui
make iso RES=1920x1080 && python3 scripts/test_gui.py   # perfil HD
```

## 10. Límites conocidos

- Render por software en CPU, sin aceleración ni SSE: suficiente para el
  escritorio, no para vídeo. El coste por fotograma depende de la zona sucia.
- No hay modo usuario: las apps ELF corren en ring 0 con el `host_api_t` como
  única frontera; siguen siendo código de confianza.
- Un único ratón y un único teclado; sin repetición automática ni rueda.
- Las imágenes se convierten en el anfitrión: el kernel no decodifica JPEG/PNG,
  acepta hasta 8 (`IMAGES_MAX`) y cada fondo de 1280x720 ocupa ~1,8 MiB del ELF.
- Los archivos internos son de solo lectura y se vuelven a sembrar en cada
  arranque; sus cambios no se persisten.
- Una ventana de aplicación a la vez.
- La hoja de cálculo está acotada a 8x16 celdas de 24 caracteres y a aritmética
  entera con `long long`; no hay decimales, formato de número ni más de una hoja
  por archivo, y `hoja.csv` debe caber en el límite de 1 KiB del FS.
- Sin TCP/IP ni IPv6 (solo ARP, ICMP y DNS sobre IPv4), DHCP, teclado USB,
  audio ni cambio de resolución en caliente.


## Navegador ELF

Navegador gráfico ELF integrado en Apps y GUI WEB. Iniciar con
python3 scripts/run_vm.py --web; IA simultánea con --ai --web.
HTTP/HTTPS y TLS verificado se ejecutan en Ubuntu por COM3; RTL8139
conserva ICMP/DNS y no implementa TCP/TLS nativos. HTML estático y enlaces,
sin CSS completo, JavaScript, login o multimedia. Apps en ring 0.
Consultar docs/diseno/NAVEGADOR_WEB.md y docs/guias/USO_NAVEGADOR.md
para límites y comandos; la validación de integración se registra en
docs/guias/VALIDACION_TOOLCHAINOS_WEB.md. Los resultados generados son locales.

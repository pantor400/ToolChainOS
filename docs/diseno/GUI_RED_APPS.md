# Escritorio, red, ratón y aplicaciones ELF

Documenta los subsistemas añadidos sobre V3.1: `src/gui.c`, `src/mouse.c`,
`src/net.c`, `src/elf.c`, `src/font_ui.c` y las apps de `apps/`. Todo el código
sigue compilando con `-mgeneral-regs-only`, sin libc y sin SSE/FPU.

## 1. Perfiles de resolución

La resolución la entrega Limine y se fija en el build:

```bash
make iso                     # 1920x1080x32 (perfil por defecto, pantalla completa HD)
make iso RES=1280x800        # perfil compacto
make run                     # ventana GTK del tamaño del modo del huesped
make run FULLSCREEN=1        # pantalla completa
```

La ventana del emulador se abre con `-display gtk,zoom-to-fit=on`
(`DISPLAY_QEMU`), de modo que se ajusta a la pantalla del anfitrion sin recortar
el modo de video del huesped ni cambiar la resolucion interna. Con 1920x1080 el
guest ocupa toda una pantalla Full HD; `FULLSCREEN=1` (o `run_vm.py --fullscreen`,
o `QEMU_FULLSCREEN=1`) anade `-full-screen` para eliminar las decoraciones.

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
| 1920x1080 (defecto) | 2 | `reg21` | `reg30` | `bold40` | hasta 1860x860 |
| 1280x800 (compacto) | 1 | `reg15` | `reg21` | `bold30` | hasta 1240x620 |

`gui_init` rechaza cualquier framebuffer que no sea RGB de 32 bpp con máscaras de
8 bits y al menos 640x400, así que un modo inesperado cae en consola en vez de
dibujar basura.

## 2. Composición

- El lienzo canónico es `0x00RRGGBB` en memoria normal, no en MMIO.
- El doble buffer se reserva con `pmm_alloc_run`: 1920x1080x4 = 8,29 MB, muy por
  encima del máximo del heap (512 KiB), de ahí el uso directo del PMM.
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
  obliga a repintar la ventana entera al mover el ratón.
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

Consecuencia: el binario crece (~818 KiB de ELF) pero el arranque no decodifica
nada y el render es una copia de máscaras.

## 5. Escritorio y ventanas

- Barra superior con título, estado de red, resolución y reloj (HH:MM) del CMOS.
- Escritorio con tarjetas y dock inferior: Archivos, Red, Entrada, Apps.
- Ventanas del sistema (`WIN_FILES`, `WIN_SYSTEM`, `WIN_NET`, `WIN_INPUT`,
  `WIN_APPS`) y una ventana de aplicación (`WIN_APP`) que hospeda el lienzo de la
  app. Cada tipo abre con un tamaño proporcional a la resolución.
- **Ventana Sistema:** doce filas con CPU (vendor y modelo por CPUID), núcleos,
  RAM detectada y libre, heap, procesos e hilos, uptime del PIT, resolución, red
  (MAC/IP/enlace), ratón, teclado, despachos del planificador y archivos del FS.
- **Ventana Red:** MAC, IP, puerta, DNS, enlace, ARP, ultimo ping (con RTT),
  ultimo nombre resuelto, TX y RX. Los valores salen de `net_status()` y se
  actualizan con `RED PING`, `RED DNS` o `RED INTERNET` desde la shell.
- **Ventana Archivos:** navegación con flechas y `Enter`, visor del contenido,
  `n` archivo nuevo, `c` carpeta, `r` renombrar, `d`/`x` borrar, `Backspace` sube
  de directorio y `ESC` cierra el visor o vuelve al escritorio. Las acciones
  usan el mismo `fs_*` del kernel, con mensajes de error en la propia ventana.
- Arrastre por la barra de título, cierre por el botón o con `ESC`.
- `GUI` abre la sesión interactiva; `GUI DEMO` dibuja un fotograma y vuelve a la
  consola; `GUI RUN <indice>` lanza un módulo ELF directamente.

## 6. Ratón y touchpad

Secuencia de arranque sobre el i8042: `0xA8`, lectura/escritura del byte de
configuración con IRQ12 habilitada y reloj activo, `0xF6` (valores por defecto),
`0xF3 0x64` (40 muestras/s), `0xE8 0x03` (resolución) y `0xF4` (habilitar).

- La IRQ12 se desenmascara sola en el PIC esclavo (`inb(0xa1) & ~0x10`).
- Los paquetes de 3 bytes se resincronizan por el bit 3 del primer byte, de modo
  que un byte perdido no desalinea el flujo.
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

Contrato `host_api_t` (`HOST_ABI_VERSION == 3`) recibido en `RDI`:

| Campo | Efecto |
|---|---|
| `write`, `finish`, `close` | Consola serie y cierre controlado |
| `window`, `width`, `height` | Ventana propia y tamaño del lienzo |
| `clear`, `rect`, `frame`, `text`, `icon`, `text_width` | Primitivas con el tema del sistema |
| `pump`, `present`, `ticks` | Bucle de eventos, volcado y reloj |
| `read_file`, `write_file` | Lectura y escritura de archivos del FS del kernel |
| `make_dir`, `remove_node`, `rename_node` | Crear carpeta, borrar y renombrar |
| `nodes`, `node_count`, `cwd`, `chdir`, `cwd_path` | Listado, directorio actual y navegación |
| `exec` | Ejecuta una orden del shell y devuelve su salida capturada |
| `key` | Siguiente tecla extendida pendiente (`KB_KEY_*`) |

Eventos: `HOST_EV_NONE`, `MOVE`, `DOWN`, `UP`, `KEY`, `CLOSE`; las coordenadas ya
vienen relativas al contenido de la ventana. Las teclas extendidas (flechas,
`Inicio`, `Fin`, `Supr`) llegan como `HOST_KEY_*` (`0x101`…), distintas de los
bytes ASCII; `HOST_KEY_TAB` sigue siendo `9`.

Aplicaciones incluidas:

- `apps/editor.c`: editor de texto de 2048 bytes con cursor movible, comandos con
  `TAB` (`w nombre`, `o nombre`, `n`, `q`) y guardado/carga por `read_file` y
  `write_file`.
- `apps/terminal.c`: terminal con scrollback circular de 400 líneas, historial de
  8 órdenes con las flechas y ejecución real vía `exec`; `limpiar` vacía la
  pantalla y `salir` cierra la ventana.

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
3. `APLICACIONES ELF DISPONIBLES` con `editor.elf` y `terminal.elf`, y sin
   `hola.elf` ni `pintar.elf`.
4. `GUI_READY RES=1920x1080` (o 1280x800) y `GUI_FRAME`, con la latencia de la
   sesión medida entre `GUI` y `SESION FINALIZADA`.
5. `SESION FINALIZADA` al salir con `ESC`.
6. `ELF: EJECUTANDO editor.elf` → `ELF: FIN editor.elf CODIGO=0`, escribiendo
   texto antes de salir; después la terminal ejecuta `help` y su salida aparece
   por COM1 antes de volver al escritorio.

```bash
make test-gui
make iso RES=1280x800 && python3 scripts/test_gui.py   # perfil compacto
```

## 10. Límites conocidos

- Render por software en CPU, sin aceleración ni SSE: suficiente para el
  escritorio, no para vídeo. El coste por fotograma depende de la zona sucia.
- No hay modo usuario: las apps ELF corren en ring 0 con el `host_api_t` como
  única frontera; siguen siendo código de confianza.
- Un único ratón y un único teclado; sin repetición automática ni rueda.
- Una ventana de aplicación a la vez.
- Sin TCP/IP ni IPv6 (solo ARP, ICMP y DNS sobre IPv4), DHCP, teclado USB,
  audio ni cambio de resolución en caliente.

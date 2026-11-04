# Binarios Linux ejecutados por la personalidad Linux

Origen: Alpine Linux v3.24 (x86_64), paquete `busybox-static`,
<https://alpinelinux.org>.

`busybox.static` es BusyBox compilado con musl y enlace estatico. ToolchainOS lo carga
como modulo Limine y lo ejecuta en ring 0: el kernel atiende sus llamadas al
sistema (`syscall`) con la capa de `src/linux_abi.c`. No se enlaza con el
kernel ni modifica su codigo, de modo que ToolchainOS sigue siendo MIT y este
binario conserva su licencia.

## Que se versiona

| Archivo | Tamano | Versionado |
|---|---|---|
| `busybox.static` | ~1 MiB | si |
| `provisionado.json` | ~1 KiB | si |
| `licencias/GPL-2.0.txt` | ~18 KiB | si |

## Licencia

BusyBox se distribuye bajo GPL-2.0. El texto completo esta en `licencias/`.
ToolchainOS solo lo ejecuta como programa separado y ofrece su procedencia y su
licencia junto al binario, tal como exige la GPL para la redistribucion.

## Applets disponibles

Cualquier applet de BusyBox puede ejecutarse con `LINUX <applet> [argumentos]`.
La lista completa se obtiene con `LINUX --list`.

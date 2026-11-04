# Pruebas y resultados — ToolchainOS V3.1

Fecha local: 2026-10-26T09:21:58.845541-05:00. Evidencia NUEVA: artifacts/v3.1-20261026T133146Z.
En el paquete: artifacts/final-validation/ y evidencias/. Las pruebas corrieron
en una copia QA aislada; no se sobrescribieron los registros históricos.

## Resultado final: 18 grupos PASSED, 0 FAILED, 0 BLOCKED y 0 SKIPPED
| Grupo | Estado | Log |
|---|---|---|
| build | PASSED | build-final.log |
| elf | PASSED | elf-final.log |
| utf8-host | PASSED | utf8-host.log |
| utf8-keyboard | PASSED | utf8-keyboard.log |
| presentation | PASSED | presentation.log |
| fs-host | PASSED | fs-host.log |
| ai-protocol-host | PASSED | ai-protocol-host.log |
| legacy | PASSED | legacy.log |
| files-editor | PASSED | files-editor.log |
| core | PASSED | core.log |
| uefi | PASSED | uefi.log |
| persistence | PASSED | persistence.log |
| utf8-persistence | PASSED | utf8-persistence.log |
| ai-transport-mock | PASSED | ai-transport-mock.log |
| ai-real | PASSED | ai-real.log |
| ai-utf8-real | PASSED | ai-utf8-real.log |
| launcher | PASSED | launcher.log |
| static-artifacts | PASSED | static-artifacts.log |

El recuento es de grupos, no de aserciones. La prueba host recorre 1 112 064
escalares Unicode válidos y comprueba roundtrip y fragmentación con ASan/UBSan.
Incluye inválidos/overlong/surrogates/fuera de rango y mapas/modificadores/glifos/FS.

## Comprobaciones específicas
- Selección ES y US mediante teclas PS/2 inyectadas por QMP y Enter, no solo por UART.
- Default ES sin responder y sin bloqueo; TECLADO consulta/cambia ES/US.
- PS/2: año, Perú, á/Ñ/ü/¿/¡/?/!, Shift, teclas muertas, AltGr+Q y Backspace.
- Serial: año, Perú, programación, ¿Qué es un sistema operativo?, todo el conjunto
  español solicitado y símbolos de 3/4 bytes. Decodificación estricta de las capturas.
- Backspace: ñ/ú/emoji completos y prefijo UTF-8 incompleto; casos malformados
  descartan la línea sin ejecutar. Desborde en bytes no deja caracteres cortados.
- Editor: edición, Backspace multibyte, guardar/abrir, cancelar y 1023 bytes.
- Nombres y contenido UTF-8 persistentes tras tres arranques; editor/REN/COPY/RM.
- Framebuffer: español, recoloreado y selección; capturas reales inspeccionadas.
- IA real abierta heredada y consulta de cobertura española mediante qwen2.5:0.5b.
  Resultado registrado: ¡Hola, Perú! El año trae programación y pingüinos. ¿Qué tal?
  Se pidió repetición para fijar el repertorio, pero la respuesta procede del modelo
  real por /api/generate. La consulta abierta también se ejecutó, no es un eco simulado.
- Mock COM2 aparte: caracteres completos y respuesta larga truncada sin cortar UTF-8.
- Toda la regresión: BIOS/UEFI, memoria, IDT/timer, tareas, archivos/editor, ATA y
  persistencia con imágenes desechables; shell tras IA y lanzador aislado.
- Kernel, ISO y copia QA idénticos; make check sin símbolos pendientes/RWX.

## Intentos y correcciones
build-primer-intento.log conserva errores de indentación de glyph.c tratados
como error por -Werror. Se separaron sentencias; build-final.log y elf-final.log
son correctos. No se desactivaron advertencias ni se ocultó el intento inicial.
Se ajustó la ubicación de tildes minúsculas para no mantener el punto de í.
La batería final se ejecutó después de esa corrección y pasó completa.

## Manual vs automatizado
Este cierre verifica teclado por PS/2 emulado y serial automáticamente; las capturas
se inspeccionaron visualmente. No se afirma prueba humana del teclado físico
ni interacción manual Windows/WSLg. La evidencia humana de V3.0 permanece histórica.
El test usa inyección QMP real en el dispositivo emulado, no una respuesta fija.
Host tests no sustituyen la evidencia de QEMU, que se conserva separadamente.

## Conservación y alcance
Respaldo previo independiente: respaldo-v3.0.tar.gz en esta carpeta de evidencia.
protegidos.json y results.json prueban hashes idénticos de imágenes preexistentes
y ZIP V3.0. No se tocaron carpetas de respaldo/recuperación ni configuraciones
del anfitrión. No hubo instalaciones, sudo, limpiezas generales ni cambios gráficos.
Las pruebas destructivas usaron exclusivamente discos nuevos y copias desechables.
ai_bridge.py y protocolo TC/1 no se modificaron; PMM/tareas/FS/ATA permanecen intactos.

UTF-8 de transporte/almacenamiento no implica Unicode gráfico completo.
La fuente cubre español precompuesto y algunos símbolos; los restantes son recuadros
solo en framebuffer. Serial conserva sus bytes. Backspace es por punto de código,
no por grafemas; no hay normalización, shaping ni garantía general de NFD/ZWJ.
Ancho CJK/emoji aproximado. FS mantiene límites en bytes y casefold ASCII.
Los controles de salida se filtran y los bytes malformados se señalan con U+FFFD.
No se recupera memoria Limine, no hay aislamiento/procesos de usuario ni C++ completo.

## Integridad
Kernel SHA256: 17ad43a34a9539fb16f5cd3b3c09ebc3fbd72b0c1afe4e47e841e0412f0e0f99
ISO SHA256: 25016b61255228b3e9e5e29b8c2cf94cf84215a1caf2f5c4415858a0804b6ba7
version.json enumera fuentes, cabeceras, Makefile, linker y dependencia Limine.
El ZIP nuevo se verifica por manifiesto/CRC y contiene el mismo kernel/ISO probados.
Los dos hashes anteriores corresponden al cierre V3.1; las comprobaciones de la
sección siguiente son posteriores y se registran en `artifacts/`.

## Hoja de cálculo en el escritorio y apagado nativo
Estas pruebas se ejecutan sobre el árbol actual y cubren la ofimática nativa y el
apagado desde el propio kernel, sin hipervisor anfitrión.

| Objetivo | Comprueba |
|---|---|
| `make test-gui` | Hoja de cálculo: `hoja.elf: HOJA 8X16 LISTA ABI=5`, apertura de `hoja.csv`, fórmula `=b1-a1` → `hoja.elf: FORMULA C1 =b1-a1 -> 2`, edición de celda (`hoja.elf: CELDA C2 10`) y guardado (`hoja.elf: GUARDADO /hoja.csv`) |
| `make test-apagar` | Escenario de consola: `APAGAR` → `POWER: APAGADO SOLICITADO (COMANDO APAGAR)`, `POWER: S5 ENVIADO` y evento QMP `SHUTDOWN` con `guest: true` |
| `python3 scripts/test_presentation.py` | El HELP publica `GUI HOJA` y `APAGAR`, y ya no enumera `VMX` ni `GUI HIPERVISOR` |

Comando y resultado esperado:

```bash
make test-apagar
# PASSED: APAGAR apaga el equipo por ACPI S5
```

El evento `SHUTDOWN` de QMP es la prueba de que el firmware atendió el S5; el
registro serie solo confirma que el kernel envió la orden. QEMU se arranca con
`-no-shutdown`, de modo que la máquina queda detenida y el proceso sigue vivo
para que la prueba pueda leer el evento.

## USB, arranque desde pendrive y Secure Boot

Fecha de ejecución: 2026-11-04. QEMU `q35` con `-device qemu-xhci` y una imagen
cruda de 32 MiB expuesta como `usb-storage`.

| Objetivo | Comprueba | Resultado |
|---|---|---|
| `make test-usb` | FAT32 con etiqueta `PRUEBAUSB`, `USB LISTAR`, `USB VER`, `USB IMPORTAR` + `OPEN` del archivo copiado, `USB ESCRIBIR` y `USB LEER` de un sector, persistencia tras reiniciar, aviso LUKS, medio `readonly`, controladora sin dispositivos y arranque del propio sistema desde el pendrive por BIOS y por UEFI | PASSED |
| `make test-secureboot` | Control con OVMF sin Secure Boot (`KERNEL_READY` en 5.0 s) y rechazo del cargador sin firmar con `OVMF_CODE.secboot.fd` (sin `KERNEL_READY` en 20 s) | PASSED |
| `make usb-lista` | Enumera unidades extraíbles con `lsblk`; sin unidades, informa y no escribe | PASSED |
| `python3 scripts/escribir_usb.py verificar build/toolchain-os.iso` | Firma `0x55AA` del sector 0 y presencia del cargador Limine en el primer MiB | PASSED |

Salida resumida de `make test-usb`:

```text
OK  arranque del kernel
OK  FAT32 montado en el pendrive
OK  etiqueta de volumen PRUEBAUSB
OK  puerto USB conectado
OK  listado FAT con HOLA.TXT
OK  contenido leido del USB
OK  importado al FS en RAM
OK  archivo importado legible
OK  escritura SCSI de un sector
OK  lectura del sector escrito
OK  el sector escrito sobrevive al reinicio
OK  aviso de medio cifrado sin descifrar
OK  el medio cifrado no se monta ni se descifra
OK  medio de solo lectura detectado
OK  escritura rechazada sin tocar el medio
OK  controladora sin dispositivos
--- Arranque BIOS desde pendrive ---
OK  arranque del kernel
OK  Arranque BIOS desde pendrive: kernel arrancado y medio de 27412 sectores montado
OK  la particion EFI del pendrive se lista
--- Arranque UEFI desde pendrive ---
OK  arranque del kernel
OK  Arranque UEFI desde pendrive: kernel arrancado y medio de 27412 sectores montado
OK  la particion EFI del pendrive se lista
USB: TODAS LAS COMPROBACIONES SUPERADAS
```

Salida resumida de `make test-secureboot`:

```text
CONTROL: ISO normal con OVMF sin Secure Boot
OK  KERNEL_READY en 5.0 s (referencia de arranque)
FIRMA: cargador con claves propias
OMITIDO: falta toolchain-os-secureboot.iso (make secureboot-claves && make secureboot-iso)
RECHAZO: ISO sin firmar con OVMF y Secure Boot activo
OK  el firmware rechazo el cargador sin firmar (sin KERNEL_READY en 20 s)
SECURE BOOT: COMPROBACIONES SUPERADAS
```

Notas de alcance:

- En este equipo no está instalado `sbsigntools`, así que `make secureboot-iso`
  no puede generar la ISO firmada: `scripts/secure_boot.py` lo detecta, explica
  el paquete que falta y se detiene. La clave y el certificado propios sí se
  generan (`make secureboot-claves`), y la política del firmware queda
  demostrada por el caso de rechazo.
- Para completar el arranque firmado hay que inscribir `build/secureboot/db.crt`
  (MOK en un equipo real o KeyTool.efi en OVMF) y repetir
  `python3 scripts/test_secureboot.py --firmado` (o `--vars ARCHIVO` con un
  `OVMF_VARS` que ya tenga la clave).
- Los casos de `test_usb.py` que necesitan `sfdisk`, `mkfs.vfat` o `mcopy` se
  omiten con aviso si el anfitrión no los tiene; el resto se ejecuta igual.
- El arranque desde pendrive usa la ISO híbrida copiada como `usb-storage`
  (`-boot c`, sin unidad de CD) y la ejecuta Limine en BIOS y en UEFI. El medio
  montado es el propio pendrive: 27412 sectores y la partición EFI del ISO.
- Con OVMF la controladora xHCI queda con el BAR en `0x7000000000`, por encima de
  4 GiB. `mmio_map()` lo pagina con el mapa directo del HHDM (`pmm_hhdm()`), no
  con `pmm_ptr()`, que solo cubre RAM baja; sin ese cambio el arranque UEFI
  abortaba en `USB: BAR MMIO NO MAPEABLE`.
- El montaje reconoce MBR y GPT: cabecera en LBA 1 (LBA de la tabla en el offset
  72, número de entradas en el 80), GUID de la ESP y de datos de Microsoft/Linux
  y, como respaldo, cualquier partición GPT cuyo sector 0 tenga un BPB FAT válido.
- `make test-all` incluye ahora los grupos `usb` y `secureboot` además de los 18
  históricos del cierre V3.1; ambos se ejecutaron por separado con PASSED. No se
  reescribieron `artifacts/results.json` ni las evidencias anteriores.

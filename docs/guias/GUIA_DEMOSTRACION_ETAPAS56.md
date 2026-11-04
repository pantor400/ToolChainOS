# Demostración de las etapas 5 y 6
Alcance: ToolchainOS V3.1 E56, x86-64 Limine. RR cooperativo, entidades kernel sin aislamiento.

## En Ubuntu WSL
Desde PowerShell:
~~~powershell
wsl.exe -d Ubuntu -- bash -lc 'cd /home/user/proyectos/ToolchainOS && python3 scripts/run_vm.py --headless'
~~~
La ISO canónica es build/toolchain-os.iso. No se conecta ningún disco al perfil básico.
Cerrar con Ctrl+C. Para ventana gráfica, omitir --headless; requiere un servidor gráfico compatible.
Para reproducir las comprobaciones (una sola VM cada vez):
~~~bash
cd /home/user/proyectos/ToolchainOS
make iso check test-memory-host test-scheduler-host
python3 scripts/test_memory_scheduler.py
~~~

## En OS>
~~~text
HELP
MEMSTATS
HEAPSTATS
MEMMAP
PMMTEST
HEAPTEST
MEMSTATS
HEAPSTATS
PS
THREADS
SCHED
SCHED TEST
SUMA 19 23
PS
THREADS
~~~
Esperar aproximadamente un segundo. La prueba imprime
SCHEDTEST PASSED TRACE=ABCABCABCABC REG_PROBES=12 LOCALS=12
PMM_RESTORED=1 HEAP_RESTORED=1 TERMINATED=3.
Luego:
~~~text
PS
THREADS
SCHED
MEMSTATS
HEAPSTATS
PROC TEST
~~~
Esperar de nuevo la finalización y probar:
~~~text
TASKS START
SUMA 4 5
TASKS
TECLADO ES
ECO Perú año programación
UPTIME
~~~
Las listas muestran datos reales; sus PID, TID, ticks y direcciones pueden variar.
Puede intercalarse salida de los hilos con el eco de una orden.
No ejecutar EXTEST en una demostración que deba continuar.

## Hoja de cálculo y apagado
La ofimática es una app nativa del escritorio, sin virtualización por debajo:
~~~text
GUI HOJA
~~~
La ventana de la hoja queda activa. Con las flechas se cambia de celda, se
escribe `5` y RET, se baja con la flecha y se escribe `=b1-a1` y RET: la serie
anota `hoja.elf: CELDA B1 5` y `hoja.elf: FORMULA B2 =b1-a1 -> 5`; con el ratón,
**Guardar** deja `hoja.elf: GUARDADO /hoja.csv` y la consola lo lee con
`open hoja.csv`. Las funciones `SUMA`, `PROMEDIO`, `MIN`, `MAX` y `CONTAR`
aceptan rangos como `=suma(a1:b3)`. Con el mapa ES-LATAM conviene `TECLADO US`
para escribir `=` y `-`, que en español no tienen tecla directa.
`APAGAR`, desde la consola, cierra el escritorio, sincroniza el sistema de
archivos y envía ACPI S5 (`POWER: S5 ENVIADO`). La captura
`artifacts/e56-hoja-calculo.png` se regenera con
`python3 scripts/verify_hoja_capture.py` si se quiere enseñar el resultado sin
abrir el emulador.

## USB y Secure Boot
Con una imagen FAT de prueba directamente en `build/` (8 MiB, superfloppy) se
arranca QEMU con la controladora xHCI y el pendrive pinchado:
~~~bash
truncate -s 8M build/usb-demo.img
mkfs.vfat -F 12 -n DEMOUSB build/usb-demo.img
mcopy -i build/usb-demo.img README.md ::LEEME.TXT
python3 scripts/run_vm.py --usb build/usb-demo.img
~~~
El arranque ya publica `USB: SISTEMA FAT12 ETIQUETA="DEMOUSB"` antes del prompt;
en `OS>` se puede recorrer el medio:
~~~text
USB INFO
USB LISTAR
USB VER LEEME.TXT
USB IMPORTAR LEEME.TXT
OPEN LEEME.TXT
USB LEER 0
~~~
`USB ESCRIBIR <sector> <texto>` escribe sectores crudos y se rechaza si el medio
está protegido. Un pendrive LUKS o BitLocker no se monta: el kernel solo avisa y
no intenta descifrarlo. `make test-usb` reproduce todo esto sin ventana.

Para el arranque desde un pendrive real, la ISO es híbrida y se copia tal cual:
~~~bash
make usb-lista
sudo python3 scripts/escribir_usb.py escribir /dev/sdX
~~~
La escritura exige teclear `ESCRIBIR /dev/sdX`; el script rechaza discos con
particiones montadas o del sistema.

Secure Boot no se desactiva: se firma el cargador de Limine con claves propias
y se inscribe el certificado en el firmware (MOK o KeyTool.efi).
~~~bash
make secureboot-claves     # build/secureboot/db.key y db.crt
make secureboot-iso        # requiere sbsigntools (sbsign)
sudo mokutil --import build/secureboot/db.crt
make test-secureboot       # control sin SB y rechazo del cargador sin firmar
~~~

## IA y persistencia
El protocolo COM2 no cambia. Con Ollama local y Qwen2.5 ya disponibles:
~~~bash
OLLAMA_MODEL=qwen2.5:0.5b python3 scripts/run_vm.py --ai --headless
~~~
Usar IA STATUS e IA ¿Qué es un kernel?; no hay descarga automática.
Para una demostración de disco que no toque imágenes anteriores, elegir un nombre NUEVO:
~~~bash
python3 scripts/run_vm.py --ata --disk build/demo-etapas56-nueva.img --headless
~~~
No reutilizar el nombre si se requiere una imagen vacía. El lanzador conserva imágenes existentes.

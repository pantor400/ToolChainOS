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

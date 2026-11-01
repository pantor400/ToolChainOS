# Cierre técnico del avance E56
Fecha: 29 de octubre de 2026. Base: V3.1; alcance actual: V3.1 E56.
Se revisaron las 15 páginas de la guía docente y visualmente sus páginas 1 y 2.

| Componente | Antes | Entrega E56 | Evidencia |
|---|---|---|---|
| PMM | página individual y bitmaps | bloques contiguos, reservas prevalentes, liberación validada | memory-host-final.log y memoria-despues.png |
| Heap | ausente | kmalloc/kcalloc/kfree, alineación 16 B, división/fusión | HEAPTEST y sanitizadores |
| Procesos | ausentes | PCB con PID, estados, miembros y recursos | PS, THREADS, procesos-hilos.png |
| Hilos | dos tareas fijas | TCB genérico, 15 posiciones, pila propia de 16 KiB | prueba de capacidad y QEMU |
| Planificación | barrido de dos posiciones | cola FIFO Round Robin cooperativa | TRACE y 156 pruebas de registros en QEMU |
| Recolección | página por tarea | pila y estado de trabajos recuperados desde shell | contadores antes/después, fallo parcial controlado |
| Compatibilidad | V3.1 funcional | teclado/UTF-8, FS/editor, ATA, BIOS/UEFI, COM2 y Ollama conservados | 19 grupos finales aprobados |

## Estado de aceptación
Etapa 5: implementada y verificada dentro del límite físico de 4 GiB y heap de 512 KiB.
Etapa 6: implementada y verificada para entidades de proceso del kernel y RR cooperativo.
No implementado: aislamiento de usuario, planificación preventiva, SMP, swapping, páginas de guarda.
No se atribuye cumplimiento a estos mecanismos ausentes.

Resultado final: 19 PASSED; 0 FAILED; 0 BLOCKED; 0 SKIPPED.
El fallo esperado por agotamiento parcial del test_scheduler_host se imprime entre
EXPECTED_FAILURE_BEGIN/END; la prueba comprueba recuperación y creación posterior.
Los fallos de sincronización de los primeros guiones y el bloqueo inicial por
Ollama apagado se conservan por separado. Ollama se inició temporalmente y se cerró.
El renderizador DOCX empaquetado no disponía de soffice.exe: se utilizó Word nativo
y pypdfium2, con revisión visual de las 18 páginas. No se instalaron dependencias.

## Archivos
Fuentes del kernel: include/heap.h, include/pmm.h, include/tasks.h,
src/heap.c, src/pmm.c, src/tasks.c, src/context.asm, src/kernel.c.
Compilación: Makefile.
Pruebas nuevas: test_memory_host.c, test_scheduler_host.c, test_memory_scheduler.py.
Guiones de validación/entrega: validate_etapas56.py, package_etapas56.py, create_academic_report.py.
Guiones adaptados: qemu_test.py, test_core.py, test_all.py y test_presentation.py.
Inventario completo: artifacts/etapas56-20261029/archivos-modificados.json.
Se verificaron sin cambios once módulos críticos de archivos, disco, interrupciones,
texto y protocolo IA. El inventario protegido contiene 18 imágenes y ZIP previos.

## Reproducción
~~~bash
cd /home/user/proyectos/ToolchainOS
make iso check test-memory-host test-scheduler-host
python3 scripts/test_memory_scheduler.py
python3 scripts/run_vm.py --headless
~~~
En OS>: MEMSTATS, HEAPSTATS, PMMTEST, HEAPTEST, PS, THREADS, SCHED,
SCHED TEST, SUMA 19 23. Esperar SCHEDTEST PASSED; consultar PS y THREADS;
comparar MEMSTATS y HEAPSTATS. PROC TEST repite la demostración.
Guía completa: ../guias/GUIA_DEMOSTRACION_ETAPAS56.md.

ISO publicada: build/toolchain-os.iso, también en build/etapas56/.
SHA-256: 2765b41fb9b58d99b5663d1ca2e7842aa4592170dfe80169cf64f3470ebf1144.
El ELF publicado coincide con /boot/kernel.elf dentro de esa ISO y con el probado.

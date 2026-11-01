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

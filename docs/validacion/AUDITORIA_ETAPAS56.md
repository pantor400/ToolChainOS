# Auditoría previa al avance de las etapas 5 y 6
Fecha: 29 de octubre de 2026. Base comprobada: Toolchain OS V3.1.
Se leyó la guía completa (15 páginas); las etapas 5 y 6 figuran en las páginas 1 y 2.
El ejemplo GRUB de 32 bits es introductorio; se conserva Limine x86-64.

| Requisito | Implementación previa | Mejora | Aceptación |
|---|---|---|---|
| Páginas físicas | pmm.c, dos bitmaps USABLE/ocupada, HHDM, límite 4 GiB | validar aritmética, solapamientos y bloques contiguos; consultas | host con mapa sintético y PMMTEST en QEMU |
| Reservas | solo LIMINE_MEMMAP_USABLE y metadatos excluidos | las regiones no utilizables prevalecen ante solapamiento | rechazar reservas, límites y doble liberación |
| Memoria dinámica | no existe heap | arenas respaldadas por PMM, kmalloc/kcalloc/kfree | tamaños, alineación, datos, coalescencia, agotamiento acotado |
| Memoria virtual | tablas de Limine, HHDM; no VMM propio | reutilizar HHDM sin duplicar tablas | documentar espacio compartido y límites |
| Procesos | no existe PCB | entidades kernel con PID, estados y recursos | crear dos propietarios, listar y recuperar |
| Hilos | dos tareas estáticas, pila física de 4 KiB | TCB general, TID, entrada y salida, pila 16 KiB | locales y registros conservados en cambios reales |
| Planificador | barrido fijo de dos tareas y retorno a shell | FIFO circular RR, dormir/despertar, contadores | A B C A B C derivado de despacho, retirar terminados |
| Interrupciones | interrupts.c/stubs, PIT 100 Hz, solo ticks en IRQ | mantener ISR sin cambios de contexto | despertar por ticks; sin afirmar prevención |
| Finalización | libera página de cada demostración | recolector desde pila de shell, heap y pilas | repetir sin pérdida de contadores |
| Compatibilidad | TASKS START y comandos V3.1, teclado y UTF-8 reales | preservar interfaz de tareas antiguas e integrar nuevas | pruebas heredadas, BIOS/UEFI, FS, persistencia, IA |

## Dependencias y seguridad
kernel.c inicia PMM antes de PIT; tasks_poll se ejecuta en el bucle de consola.
context.asm conserva seis registros callee saved según SysV; los registros volátiles
son responsabilidad del llamador en cesiones mediante llamadas C. No guarda un
contexto completo de interrupción para prevención. No existen GDT/TSS/IST propios,
procesos de usuario, CR3 por proceso ni protección de páginas por hilo. El PIT salva
registros en interrupt_stubs.asm y vuelve al mismo contexto con IRETQ.
Los módulos FS/editor/ATA/COM2 son síncronos o se consultan desde shell; las IRQ no
asignan memoria. Se mantendrá ese contrato BSP, sin ejecutar tareas en los AP.
No se introducirá prevención dentro de IRQ ni se presentará el RR cooperativo como
preventivo. Las pilas se recogerán únicamente después de volver a la pila del shell.
El respaldo previo está fuera del proyecto, en Documents/Codex/ToolchainOS_Etapas56_2026-10-29.
Las 18 imágenes/ZIP previos están inventariados en artifacts/etapas56-20261029/protegidos.json.

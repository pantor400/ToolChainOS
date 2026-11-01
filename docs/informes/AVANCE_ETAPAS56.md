# IMPLEMENTACIÓN DE UN ADMINISTRADOR DE MEMORIA, PROCESOS, HILOS Y PLANIFICADOR EN TOOLCHAINOS

Universidad Nacional Hermilio Valdizán  
Curso de Sistemas Operativos  
Docente: Dr. Freddy R. Huapaya C.  
Proyecto ToolchainOS  
29 de octubre de 2026

## 1 Introducción y contexto del sistema operativo

Este avance amplía ToolchainOS V3.1 para desarrollar las etapas 5 y 6 de la guía del curso. La versión resultante se identifica como V3.1 E56, una denominación de alcance académico que conserva la numeración existente. La contribución principal consiste en un administrador de memoria dinámica conectado al gestor físico y una infraestructura de procesos del kernel e hilos con planificación Round Robin cooperativa.

La guía de Huapaya C. (s. f., pp. 1-2) propone implementar un administrador de memoria y crear procesos o hilos con un planificador. Su ejemplo de arranque es introductorio. En este trabajo se mantiene la arquitectura de Limine y x86-64 ya operativa, sin introducir GRUB ni convertir el kernel a 32 bits.

## 2 Objetivos del nuevo avance

El objetivo general es demostrar la administración y recuperación de recursos de memoria y ejecución mediante mecanismos reales del kernel. Los objetivos específicos son consolidar las reservas del PMM, ofrecer kmalloc, kcalloc y kfree, representar propietarios de ejecución mediante PCB, generalizar las tareas a TCB con pilas independientes y comprobar la selección Round Robin.

La aceptación exige pruebas de datos, alineación, agotamiento acotado, liberaciones inválidas, registros conservados, estados observables y recuperación de páginas. La salida de consola se obtiene de estructuras internas y de la ejecución efectiva. Los resultados de inferencia real se distinguen del transporte probado con un backend controlado.

## 3 Arquitectura inicial de ToolchainOS

La base auditada es V3.1. El kernel C freestanding utiliza NASM, GCC, LD y Limine, con direcciones en la mitad superior y páginas de 4 KiB. Limine proporciona el mapa de memoria y el desplazamiento HHDM. La IDT, los manejadores ensamblador y el PIC atienden excepciones y un PIT configurado a aproximadamente 100 Hz. Los procesadores adicionales no ejecutan un planificador del kernel.

La consola gráfica y COM1 ya admitían UTF-8, con teclado PS/2 ES-LATAM y US. El sistema de archivos, el editor, la persistencia ATA/IDE y el puente COM2 se conservan. La respuesta de inteligencia artificial se procesa en Linux mediante Ollama; no se ejecuta un modelo de lenguaje dentro del kernel.

## 4 Diagnóstico de las funcionalidades existentes

El PMM anterior ya asignaba y liberaba páginas mediante dos bitmaps y excluía memoria no marcada USABLE. Sin embargo, no ofrecía asignación dinámica de bloques, ni una interfaz de bloques contiguos para pilas mayores. El nuevo código refuerza las operaciones aritméticas y hace que las regiones reservadas prevalezcan si un mapa contiene solapamientos.

Las dos tareas anteriores disponían de pilas propias y una rutina auténtica de cambio de contexto. Su estructura era fija y se limitaba a una demostración de pasos. Se aprovechó context_switch y se sustituyó el barrido fijo por una infraestructura común de PCB, TCB y cola circular. TASKS START permanece disponible como cliente de esta infraestructura. La auditoría y la matriz de aceptación están en ../validacion/AUDITORIA_ETAPAS56.md.

## 5 Implementación del administrador de memoria

El PMM mantiene dos bitmaps: uno registra qué páginas pueden asignarse y otro cuáles están ocupadas. Se calcula el tamaño de sus metadatos dentro de una región segura del mapa de Limine. La página cero, los propios bitmaps, las regiones no utilizables, el kernel, los módulos y la memoria del cargador permanecen excluidos según las categorías del protocolo. Solo se administra RAM física inferior a 4 GiB. No se recuperan de forma anticipada las regiones del cargador, porque contienen respuestas y tablas que siguen en uso.

La asignación individual conserva la interfaz pmm_alloc. La nueva pmm_alloc_run busca páginas contiguas. pmm_free_run valida alineación, límites, disponibilidad y ocupación de todas las páginas antes de cambiar un bit. Un error no produce una liberación parcial. MEMSTATS muestra páginas administradas, libres, ocupadas y reservadas; MEMMAP expone base, longitud y tipo de las regiones recibidas.

El heap usa arenas obtenidas del PMM y accesibles mediante HHDM. Los bloques están alineados a 16 bytes. La primera coincidencia suficiente se divide cuando el resto admite un encabezado y un bloque mínimo; los vecinos libres se fusionan. Cuando toda una arena queda libre, se devuelve al PMM. kcalloc comprueba el producto antes de reservar y pone a cero el contenido. Las solicitudes nulas, excesivas o sin recursos devuelven un puntero nulo.

La capacidad total respaldada del heap se limita a 128 páginas, equivalentes a 512 KiB, y una arena puede ocupar hasta 64 páginas. Estas cotas permiten provocar un agotamiento real del heap sin consumir toda la RAM del huésped. kfree valida el puntero por comparación con los bloques conocidos; no interpreta un puntero arbitrario como un encabezado. Los errores incrementan contadores consultables. heap_check verifica relaciones, tamaños y continuidad de los metadatos. Este control no reemplaza la protección de memoria frente a escrituras arbitrarias del código kernel.

## 6 Implementación de procesos y PCB

Un PCB representa una entidad del kernel y contiene PID monotónico, nombre, estado, número de hilos vivos, hilos creados, páginas de pila y despachos acumulados. El proceso de consola tiene PID 1 y queda protegido de las operaciones de demostración. Hay quince posiciones adicionales reutilizables. La reutilización de una posición asigna un identificador nuevo y requiere que el proceso anterior haya terminado y no conserve hilos vivos.

Los estados disponibles son NEW, READY, RUNNING, WAITING y TERMINATED. En un proceso con varios hilos, el estado se deriva de sus miembros. PROC TEST crea dos propietarios y tres hilos: dos pertenecen a demo-memory y uno a demo-workers. Esta relación se comprueba mediante PS y THREADS.

Todas las entidades comparten las tablas de páginas y los privilegios del kernel. No existen procesos independientes de usuario, cambio de CR3 por proceso, llamadas al sistema, ejecutables aislados ni protección completa entre propietarios. El PCB organiza el ciclo de vida y la contabilidad; no crea aislamiento por sí mismo.

## 7 Implementación de hilos y TCB

Cada TCB conserva TID, PCB propietario, estado, función de entrada, argumento, puntero de pila, dirección física de la pila, instante de despertar y estadísticas. Se asignan cuatro páginas contiguas, es decir, 16 KiB por hilo. Una marca al pie de la pila y la comprobación del RSP detectan determinados daños en los límites de cesión; no se han instalado páginas de guarda.

El contexto inicial respeta la alineación SysV. La rutina NASM cambia RSP y conserva RBX, RBP y R12 a R15. El punto de retorno reside en la pila. Como la cesión es una llamada de función, el compilador conserva los valores volátiles necesarios alrededor de la llamada. La compilación utiliza registros generales y desactiva la red zone; no se ofrece contexto de coma flotante para tareas ajenas a este contrato.

El hilo puede ceder, dormir por ticks o finalizar. El retorno normal de su función pasa por un trampolín que llama a thread_exit. Tras volver al shell, el recolector libera la pila y actualiza el PCB. Nunca se libera la pila que está ejecutando el recolector. Las creaciones parcialmente fallidas deshacen los recursos y las entradas pendientes antes de cualquier despacho.

## 8 Diseño e implementación del planificador

El algoritmo es Round Robin cooperativo con cola FIFO circular. Un hilo READY entra una sola vez. Cada llamada a tasks_poll extrae el primero, lo marca RUNNING y cambia realmente de pila desde el shell. Si cede, vuelve al final de la cola; si duerme, sale de la cola hasta su despertar; si termina, se retira y recoge. Los hilos terminados no vuelven a encolarse.

La consola constituye el contexto de coordinación y continúa atendiendo entrada entre despachos. Si no hay hilos listos, tasks_poll retorna al shell y aumenta IDLE_POLLS. Este mecanismo seguro de inactividad utiliza sondeo; no es un hilo idle dedicado ni reduce por sí mismo el uso de CPU de QEMU.

SCHED muestra longitud de la cola, despachos, sondeos inactivos y errores. La prueba registra cada paso desde el hilo que lo ejecuta; la secuencia ABCABCABCABC se contrasta con el registro, sin imprimir una secuencia fija como sustituto de ejecución. Una prueba NASM introduce centinelas distintos en los seis registros preservados y comprueba sus valores después de una cesión real.

## 9 Integración con interrupciones y temporizador

La IRQ del PIT incrementa ticks y envía EOI; no conmuta de hilo ni modifica el heap o la cola. El shell comprueba los tiempos de despertar. Esta separación permite mantener el contrato del asignador y el planificador en el procesador de arranque sin añadir bloqueos entre CPU o estructuras mutadas desde una interrupción.

El sistema no implementa quantum preventivo. Un hilo que nunca llama a thread_yield, thread_sleep o thread_exit puede monopolizar el kernel. Tampoco debe ceder con interrupciones deshabilitadas ni durante una operación que requiera exclusión. La prevención requeriría un diseño adicional de marcos de interrupción, protección de secciones críticas y contexto completo; queda fuera de este avance estable.

## 10 Pruebas funcionales y resultados

La línea base anterior a las modificaciones superó compilación, verificación ELF, test_core y test_utf8_keyboard. Las pruebas nuevas de memoria compilan exactamente pmm.c y heap.c con AddressSanitizer y UndefinedBehaviorSanitizer sobre RAM y mapas Limine controlados. Comprueban reservas solapadas, desbordamientos, agotamiento físico completo en mapas pequeños, fragmentación, reutilización y liberaciones atómicas. El heap se repite veinte veces y recupera las páginas iniciales.

La prueba de capacidad del planificador ejecuta el mismo código C y NASM en Linux, sin sustituir el cambio de pila por una simulación. Comprueba quince PCB y quince TCB simultáneos, rechazo de propietarios inválidos, treinta rondas completas y una ronda posterior a un fallo parcial de creación provocado por falta de páginas. La validación principal del kernel se realiza de forma separada en QEMU.

En QEMU se ejecutan trece demostraciones: 39 hilos y 156 comprobaciones de registros y variables locales. Cada ejecución verifica tres direcciones de variables locales en pilas diferentes, PID y TID nuevos, secuencia RR, terminación, retirada y recuperación de los contadores de PMM y heap. La consola calcula una suma mientras los hilos están vivos. Se conservan capturas del estado READY, WAITING y TERMINATED y de la operación posterior.

El archivo results.json registra los grupos finales, con estados PASSED, FAILED, BLOCKED y SKIPPED. La validación final registra 19 grupos PASSED, 0 FAILED, 0 BLOCKED y 0 SKIPPED, incluida inferencia real y texto español con Qwen2.5. Las pruebas de regresión comprenden archivos, editor, teclado y UTF-8, persistencia, arranque UEFI, transporte COM2 e inferencia real cuando el servicio local está disponible. Los discos empleados para escritura son nuevos; las imágenes y ZIP anteriores se verifican por SHA-256.

Los primeros intentos del guion detectaron problemas de sincronización: la salida de un hilo puede intercalarse con el eco de una orden, COM1 utiliza CRLF y PS puede repetir PID ya observados. Se corrigieron los criterios de lectura del guion manteniendo las aserciones funcionales. El mismo ajuste se aplicó a la suma concurrente del test_core heredado. Los registros fallidos se conservan y no se presentan como pruebas aprobadas del kernel.

## 11 Limitaciones técnicas

Los procesos son entidades del kernel sin aislamiento. La planificación es cooperativa y se ejecuta en BSP; no hay planificación SMP ni prioridades. Se admiten hasta quince procesos de demostración y quince hilos además del shell. El heap tiene una cota de 512 KiB y necesita tramos físicamente contiguos para sus arenas. La fragmentación física puede impedir una solicitud aunque existan páginas libres dispersas.

No hay VMM propio, swapping, memoria de usuario, protección frente a todos los desbordamientos de pila ni recuperación tras una excepción fatal. Los identificadores no se reciclan; si se agotara el contador de 64 bits, se rechazarían creaciones nuevas. Los contadores de ticks por hilo son mediciones gruesas del PIT, no perfiles de CPU de alta precisión.

El soporte previo de texto se conserva con sus límites: UTF-8 válido en serie y un repertorio gráfico español acotado, sin normalización ni composición general de grafemas. La salida asíncrona puede intercalarse con el eco de consola aunque las órdenes sigan procesándose. El protocolo de IA y los formatos de persistencia no cambian.

## 12 Conclusiones

El avance proporciona un administrador de memoria que conecta páginas físicas y bloques dinámicos y permite observar sus recursos desde la consola. La recuperación comprobada de páginas tras asignaciones, agotamiento y ciclos de hilos muestra que la finalización es parte del diseño y no únicamente una etiqueta de estado.

La etapa 6 se desarrolla mediante PCB, TCB y Round Robin con cambios auténticos de pila y registros. El alcance académico se satisface en el contexto de entidades del kernel y planificación cooperativa. El aislamiento de procesos de usuario y la prevención siguen siendo objetivos posteriores y no se atribuyen a esta entrega.

## 13 Evidencias y anexos

Los anexos incluyen la matriz de auditoría, resultados JSON, registros serie, capturas QMP, fuentes y guiones de reproducción. El respaldo previo completo se guarda fuera del proyecto. El manifiesto de la entrega identifica cada archivo por SHA-256 y el inventario de protección confirma que no se sobrescribieron imágenes persistentes ni ZIP anteriores.

La guía ../guias/GUIA_DEMOSTRACION_ETAPAS56.md contiene el arranque y las órdenes exactas. Para una exposición se recomienda mostrar MEMSTATS y HEAPSTATS antes y después, ejecutar PMMTEST y HEAPTEST, iniciar SCHED TEST, consultar PS y THREADS durante la espera y finalizar con SCHED y una orden ordinaria de consola.

## Referencias

Huapaya C., F. R. (s. f.). Como construir un sistema operativo [Guía del curso Sistemas Operativos, archivo Semana 6 Crear Un Sistema Operativo.pdf]. Universidad Nacional Hermilio Valdizán.

Limine Bootloader. (s. f.). The Limine boot protocol (rama v8.x). https://github.com/limine-bootloader/limine/blob/v8.x/PROTOCOL.md

x86-64 psABI community. (s. f.). System V application binary interface AMD64 architecture processor supplement [Especificación y fuentes]. https://gitlab.com/x86-psABIs/x86-64-ABI

Proyecto ToolchainOS. (2026). Código fuente y registros de validación V3.1 E56 [Software educativo y evidencia de ejecución]. Entrega del 29 de octubre de 2026.

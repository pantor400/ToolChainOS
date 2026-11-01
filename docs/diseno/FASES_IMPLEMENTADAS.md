# Estado por fases — V3.1

| Fase original | Estado actual | Evidencia |
|---|---|---|
| 0 Estabilidad | Preservada/verificada | make check, BIOS, UEFI, regresión |
| 1 Archivos | Preservada + texto UTF-8 | ASan/UBSan, rutas/nombres, RM/REN/COPY/TREE |
| 2 Editor | Preservada + entrada multibyte | Guardar/cancelar, Backspace, límites y archivos |
| 3 PMM | Sin cambios, verificada | Agotamiento/liberación y estadísticas reales |
| 4 IDT/timer | Sin cambios, verificada | DE/PF esperados y PIT creciente |
| 5 Tareas | Sin cambios, verificada | Pilas, alternancia, shell y recuperación |
| 6 ATA/persistencia | Formato sin cambios, verificada | Discos desechables y 3 arranques UTF-8 |
| 7 Ollama | Protocolo/puente intactos, verificada | Inferencia real abierta y española UTF-8 |

V3.1 añade menú de teclado, TECLADO, UTF-8 y glifos españoles sobre V3.0.
18 grupos finales superados. No se promete Unicode gráfico completo; véase ../guias/TECLADO_UTF8.md.

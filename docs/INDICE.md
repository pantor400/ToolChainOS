# Índice de la documentación

Punto de entrada: [`../README.md`](../README.md). Este índice ordena el resto de los
documentos por intención y recuerda las convenciones del árbol.

## 1. Empezar aquí

| Documento | Para qué |
|---|---|
| [`../LEEME_PRIMERO.txt`](../LEEME_PRIMERO.txt) | Qué hay en la entrega y cómo abrirla |
| [`../LEEME_ENTREGA.md`](../LEEME_ENTREGA.md) | Contenido del paquete y resumen de evidencias |
| [`validacion/VALIDACION.md`](validacion/VALIDACION.md) | Cómo reproducir la validación paso a paso |

## 2. Diseño

| Documento | Contenido |
|---|---|
| [`diseno/ARQUITECTURA.md`](diseno/ARQUITECTURA.md) | Subsistemas, mapa de memoria y flujo de arranque |
| [`diseno/GUI_RED_APPS.md`](diseno/GUI_RED_APPS.md) | Escritorio, repintado por zonas, tema claro, assets escalables, ratón/touchpad, RTL8139, ventanas Sistema/Archivos y apps ELF64 (editor, terminal y hoja de cálculo) |
| [`diseno/DECISIONES_TECNICAS.md`](diseno/DECISIONES_TECNICAS.md) | Decisiones tomadas y alternativas descartadas |
| [`diseno/FASES_IMPLEMENTADAS.md`](diseno/FASES_IMPLEMENTADAS.md) | Alcance de cada fase (estado histórico de V3.1) |
| [`etapas56-diagramas/`](etapas56-diagramas/) | Diagramas de contexto, estados, memoria y Round Robin |

## 3. Uso y demostración

| Documento | Contenido |
|---|---|
| [`guias/EXPLICACION.md`](guias/EXPLICACION.md) | Del código al arranque, explicado para exposición |
| [`guias/GUIA_CODIGO.md`](guias/GUIA_CODIGO.md) | Recorrido comentado de `src/` e `include/` |
| [`guias/GUIA_DEMOSTRACION.md`](guias/GUIA_DEMOSTRACION.md) | Guion de demostración general |
| [`guias/GUIA_DEMOSTRACION_ETAPAS56.md`](guias/GUIA_DEMOSTRACION_ETAPAS56.md) | Guion específico de las etapas 5 y 6 |
| [`guias/TECLADO_UTF8.md`](guias/TECLADO_UTF8.md) | Distribuciones, teclas muertas y entrada UTF-8 |
| [`diseno/GUI_RED_APPS.md`](diseno/GUI_RED_APPS.md) | Comandos `GUI`, `GUI DEMO`, `GUI RUN n`, `RED`, `RATON`, `HORA` y `ELF`; configuración de QEMU en `scripts/qemu_config.py` |
| [`guias/ENTREGA_DOCENTE.md`](guias/ENTREGA_DOCENTE.md) | Resumen para el docente |
| [README §12](../README.md#12-personalidad-linux-busybox-en-ring-0) | Personalidad Linux: SYSCALL, pila con auxv, despacho de llamadas y BusyBox ejecutado por el kernel propio |
| [README §13](../README.md#13-hoja-de-cálculo-en-el-escritorio-gui-hoja) | Hoja de cálculo nativa: rejilla 8x16, fórmulas con referencias y funciones, teclado y ratón, y persistencia CSV |
| [README §14](../README.md#14-apagado-nativo-apagar) | Apagado nativo: orden `APAGAR`, cierre ordenado (GUI y FS) y ACPI S5 por `PM1a_CNT` |
| [README §15](../README.md#15-arranque-desde-usb-y-secure-boot) | USB: xHCI y BOT/SCSI, montaje FAT, aviso de medios cifrados, copia de la ISO a un pendrive y Secure Boot con claves propias |
| [`vendor/linux-bin/ORIGEN.md`](../vendor/linux-bin/ORIGEN.md) | Origen, versión y licencia del BusyBox que ejecuta la personalidad Linux |

## 4. Validación, evidencias e informes

| Documento | Contenido |
|---|---|
| [`validacion/PRUEBAS_Y_RESULTADOS.md`](validacion/PRUEBAS_Y_RESULTADOS.md) | Tabla de pruebas y resultados |
| [`validacion/AUDITORIA_ETAPAS56.md`](validacion/AUDITORIA_ETAPAS56.md) | Auditoría de la entrega |
| [`validacion/DIAGNOSTICO_INICIAL.md`](validacion/DIAGNOSTICO_INICIAL.md) | Diagnóstico previo a las etapas 5 y 6 |
| [`validacion/RECUPERACION_2026-10-26.md`](validacion/RECUPERACION_2026-10-26.md) | Recuperación tras el incidente del 26/10/2026 |
| [`informes/AVANCE_ETAPAS56.md`](informes/AVANCE_ETAPAS56.md) | Avance de las etapas 5 y 6 |
| [`informes/CIERRE_ETAPAS56.md`](informes/CIERRE_ETAPAS56.md) | Cierre con los 19 grupos superados |
| [`informes/INFORME_FINAL.md`](informes/INFORME_FINAL.md) | Informe final del proyecto |
| [`informes/INFORME_ACADEMICO_ETAPAS56.docx`](informes/INFORME_ACADEMICO_ETAPAS56.docx) | Informe académico en Word |

Las evidencias ejecutables (registros serie, capturas y `results.json`) viven en
[`../artifacts/etapas56-20261029/`](../artifacts/etapas56-20261029/).

## 5. Revisión de código

| Documento | Contenido |
|---|---|
| [`auditoria/REVISION_FUNCIONES.md`](auditoria/REVISION_FUNCIONES.md) | Funciones a corregir o eliminar, priorizadas P1/P2/P3 |

## 6. Convenciones del árbol

- Los nombres de archivo son estables: los documentos se citan por nombre en scripts y
  en los informes, así que moverlos entre carpetas exige actualizar esas referencias.
- `src/`, `include/`, `scripts/` y `artifacts/` están referenciados por el `Makefile`,
  por la suite de pruebas y por `MANIFEST_SHA256.json`; reordenarlos obliga a regenerar
  el manifiesto y a revalidar.
- Los scripts sustituidos se eliminaron: `scripts/` contiene solo herramientas
  vigentes invocadas por el `Makefile`, la suite o la validación.
- Las secuencias de fecha en los nombres (`2026-10-26`, `2026-10-29`) identifican
  instantáneas históricas y no deben renombrarse.

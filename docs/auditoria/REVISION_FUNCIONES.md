# Revisión de código — funciones a corregir o eliminar

Auditoría sobre `src/`, `include/`, `Makefile` y `scripts/`. Prioridades:
**P1** = riesgo de corrupción o comportamiento incorrecto; **P2** = redundancia,
código muerto o deuda técnica; **P3** = cosmético o documental.

## 1. Correcciones obligatorias (P1)

| # | Ubicación | Problema | Acción |
|---|---|---|---|
| 1 | `src/cpp_runner.c:831-872` | `collect_functions()` escribe `funcs[func_count]` sin comprobar `func_count < MAX_FUNCS` (16). Un script con 17 funciones desborda el array estático y corrompe `.bss`. | Añadir cota y error explícito. |
| 2 | `src/cpp_runner.c:146-289, 887` | `tokenize()` **siempre** devuelve 1; la rama de error de `cpp_run_script()` es código muerto. Además descarta caracteres desconocidos en silencio y trunca literales de cadena a 47 caracteres sin avisar. | Devolver estado real y reportar el descarte. |
| 3 | `src/cpp_runner.c:209-219, 440` | Literales numéricos con `val = val*10 + dígito` sin control de rango y `-eval_unary()` con `INT64_MIN`: desbordamiento con signo (UB). | Acumular en `uint64_t` con tope y tratar `INT64_MIN`. |
| 4 | `src/cpp_runner.c:406-410` | `last_return_val` no se reinicia al entrar en una función: una función sin `return` devuelve el valor de la llamada anterior. | Reiniciar `last_return_val = 0` por invocación. |
| 5 | `src/console.c:82-86` | `console_erase_last()` repite `\b` según `utf8_columns()` (1 o 2) por serial, pero llama a `graphical('\b')` una sola vez: con ancho 2 queda un glifo fantasma en framebuffer. | Repetir el borrado en pantalla `width` veces. |
| 6 | `src/keyboard.c:14-19` | `emit()` descarta caracteres cuando la cola de 8 bytes está casi llena; un escalar de 3-4 bytes puede perderse en silencio. | Ampliar la cola o encolar solo si cabe completa. |
| 7 | `src/keyboard.c:79-83` | Con tecla muerta y carácter no componible se emite **acento y luego letra** (`emit(dead)` antes de `emit(c)`), produciendo el orden inverso al esperado. | Emitir la letra y después la marca, o descartar la marca. |
| 8 | `src/storage.c:11, 78-95` | La bandera `dirty` nunca se activa cuando el FS cambia; `FSSTATUS` solo puede mostrar `DIRTY=YES` tras un fallo de escritura, lo que induce a error. | Eliminarla y basar el informe en la comparación `previous`/`image`. |
| 9 | `src/heap.c:40` | `kmalloc()` ejecuta `heap_check()` (recorrido completo de arenas y bloques) en **cada** asignación: coste cuadrático en el caso típico. | Compilar la verificación solo bajo `HEAP_DEBUG` o en pruebas. |
| 10 | `src/heap.c:113-115` | `if(n==32||!heap_check())ok=0;` marca la prueba como fallida cuando las 32 reservas de 64 KiB **tienen éxito**, que es lo contrario de lo que se quiere demostrar. | Reordenar la condición y comentarla. |
| 11 | `src/editor.c:18-26` | Los comandos del editor (`:q`, `:wq`, `:show`, `:clear`) se comparan con `same()` (sensible a mayúsculas) mientras la shell usa `equal()`. | Homogeneizar la comparación. |
| 12 | `src/kernel.c:224-225` | En cualquier estado guiado, `CANCELAR` cancela siempre: no es posible guardar ese texto como contenido de archivo ni con `TEXTO`. | Documentarlo o exigir `:cancel` en `FILE_CONTENT_INPUT`. |

### Parche sugerido para el desbordamiento (punto 1)

```c
static int collect_functions(void) {
    func_count = 0;
    /* ... */
                if (match(TOK_LPAREN)) {
                    if (func_count >= MAX_FUNCS) {
                        set_error(tokens[cur_tok].line, "DEMASIADAS FUNCIONES");
                        return 0;
                    }
                    func_t *fn = &funcs[func_count];
```

`cpp_run_script()` debe comprobar el valor devuelto por `collect_functions()`.

## 2. Funciones duplicadas o redundantes (P2)

Las duplicaciones triviales ya se eliminaron; el detalle está en §6.

| Ubicación | Situación | Recomendación |
|---|---|---|
| `src/kernel.c:37-41` (`upper`, `equal`) | Reimplementan `ci_same()` de `include/util.h`, pero con otra semántica: aquí se ignoran mayúsculas y se usa en 61 comparaciones. | No sustituir a ciegas: `ci_same` cambiaría esas comparaciones. Dejar como está. |
| `src/tasks.c` (`legacy_entry`, `TASKS START`) | Segunda demostración de tareas que replica `SCHED TEST`. | Conservar: `scripts/test_core.py` envía `tasks start`, así que eliminarla invalidaría esa prueba. |
| `src/glyph.c:26` (`glyph_supported`) | Solo lo usa `scripts/test_utf8_host.c`. | Mantener como API de prueba. |
| `src/ata.c:97` | Comparación manual de `"READ"` carácter a carácter. | Usar un helper de prefijo y `skip_spaces`. |

## 3. Riesgos de diseño y acoplamiento (P2)

1. **Ciclo `ata.c` ↔ `storage.c`.** `ata.c` llama a `storage_can_write()` para validar
   escrituras, e `include/ata.h` incluye `storage.h`. Conviene invertir la dependencia:
   pasar un predicado de rango autorizado desde `storage_init()` o moverlo a un módulo
   de dispositivo.
2. **`cpp_runner.c` pesa más que el resto del shell.** `token_t` ocupa 64 bytes, por lo
   que `tokens[1024]` son ~64 KiB de `.bss` y el intérprete completo ronda los 90 KiB,
   además de ~500 líneas de estado global. Si no es requisito de la rúbrica, moverlo a
   un módulo opcional del `Makefile`.
3. **`kernel.c` concentra demasiado** (526 líneas: arranque, solicitudes de Limine,
   `INFO`/`MEM`, máquina de 13 estados, 40 comandos y el bucle de entrada). Extraer
   `shell.c` (bucle y tokenización) y `commands.c` (tabla de comandos) reduciría el
   riesgo de regresión al añadir comandos.
4. **`ai_serial.c:44` (`discard`)** reordena el búfer byte a byte: coste cuadrático en
   el peor caso (1036 bytes). Sustituir por un índice de inicio en lugar de
   desplazamiento.
5. **`console.c:21-23` (`serial_char`)** descarta el byte tras 100 000 sondeos sin
   informar; conviene incrementar un contador de desbordamiento visible en `INFO`.
6. **`interrupt_stubs.asm:4-6` (`isr_ignore`)** hace `iretq` sin enviar EOI. Hoy es
   inocuo porque el PIC solo tiene IRQ0 desenmascarada, pero es frágil si se habilita
   IRQ1 (teclado) o IRQ14 (ATA).
7. **`src/cpu.asm:25`** — el comentario «No tenemos IDT ni manejadores de IRQ propios»
   está obsoleto desde V2.3.
8. **`Makefile:32-33`** — `build/cpu.o` tiene a la vez regla de patrón y regla
   explícita; y `test-all` no depende de `iso` (lo compila dentro del script). Unificar.
9. **`storage.c:8-13`** — el formato usa el literal 1064 en `encode()` y `decode()`.
   Definir `NODE_BYTES` y añadir `_Static_assert` sobre `sizeof(fs_node_t)` para que un
   cambio en `fs.h` no rompa la persistencia en silencio.

## 4. Inconsistencias documentales (P3)

- `LEEME_ENTREGA.md` mezclaba «19 grupos» (cierre E56) con «18 grupos» (V3.1). El cierre
  vigente es 19; `docs/diseno/FASES_IMPLEMENTADAS.md` y `CHANGELOG.md` conservan 18
  porque describen el estado histórico de V3.1: conviene etiquetarlos como tales.
- `docs/guias/EXPLICACION.md` y `docs/guias/GUIA_CODIGO.md` tenían secuencias `\n`
  literales en su cabecera; ya no las usan.
- `scripts/test_all.py` ejecuta 17 pasos y no incluye `launcher` ni `presentation`,
  que sí figuran en la evidencia final. Añadirlos o aclararlo en la documentación.

## 5. Qué no tocar

- `pmm_is_usable()`, `utf8_valid()` y `glyph_supported()` parecen «sin uso» en el
  kernel, pero los consumen `scripts/test_memory_host.c` y `scripts/test_utf8_host.c`:
  eliminarlos rompería las pruebas de host.
- `asm_sumar()` es intencionadamente trivial: demuestra el paso de argumentos
  System V AMD64 desde C a NASM. Conservar y documentar como material didáctico.
- El formato de snapshots, el protocolo `TC/1` y la autorización de escritura ATA están
  congelados: cambiarlos invalida las evidencias de persistencia e IA.
- Las rutas de `src/`, `include/`, `scripts/` y `artifacts/` están referenciadas por el
  `Makefile`, los scripts de prueba y `MANIFEST_SHA256.json`: reordenarlas obliga a
  regenerar el manifiesto y a revalidar la suite completa.

## 6. Limpieza aplicada (estado actual)

Eliminado por no tener ningún consumidor ni prueba asociada:

- `fs_get_cwd()` (`src/fs.c`, `include/fs.h`).
- `keyboard_get_layout()` (`src/keyboard.c`, `include/keyboard.h`).
- `tasks_test_poll()` (`src/tasks.c`): `tasks_poll()` ya llama a `finish_test()`.
- `process_command()` (`src/tasks.c`, `include/tasks.h`): `PROC TEST` se resuelve en
  `kernel.c` sin función intermedia.
- `str_copy_kernel()` y `split_path_name()` (`src/kernel.c`): se usan `copystr()` y
  `fs_split()` de la cabecera.
- Duplicación del intérprete: `RUN CPP`, `RUN_CPP` y la entrada guiada comparten
  `run_cpp_path()` (~45 líneas menos).
- `scripts/legacy/`: los seis guiones archivados y su README.

Conservados a propósito: `upper`/`equal` (61 comparaciones con semántica propia),
`legacy_entry`/`TASKS START` (usado por `scripts/test_core.py`), `glyph_supported`,
`pmm_is_usable` y `utf8_valid` (pruebas de host) y `asm_sumar` (didáctico).

## 7. Verificación sugerida tras aplicar cambios

```bash
make BUILD=/tmp/qos-verify iso      # compila sin tocar build/
make test-memory-host               # PMM y heap con ASan/UBSan
make test-utf8-host                 # UTF-8, teclado, glifos y FS
make test-scheduler-host            # cambios de contexto
make test-all                       # regresión completa en QEMU
```

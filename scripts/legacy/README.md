# Scripts archivados

Estos guiones fueron sustituidos por las herramientas vigentes y no los invoca
ningún objetivo del `Makefile` ni ninguna prueba de la suite. Se conservan como
histórico; no forman parte del paquete de entrega.

| Script | Sustituido por | Nota |
|---|---|---|
| `package_release.py` | `scripts/package_etapas56.py` | Empaquetado anterior a E56; lista una selección fija de scripts |
| `final_validate.py` | `scripts/validate_etapas56.py` | Validación previa al cierre E56 |
| `write_final_report.py` | `scripts/create_academic_report.py` | Generaba `docs/validacion/PRUEBAS_Y_RESULTADOS.md` e `docs/informes/INFORME_FINAL.md` |
| `recovery_report.py` | — | Informe puntual de la recuperación del 26/10/2026 |
| `probe_ollama.py` | `scripts/test_ai_real.py` | Sondeo manual del servicio Ollama |
| `capture_demo.py` | `scripts/test_presentation.py` | Captura por QMP; necesita `scripts/qemu_test.py` |

## Ejecución

Los guiones que dependen de módulos de `scripts/` (`qemu_test.py`, `qmp_test.py`,
`create_test_disk.py`) siguen funcionando porque ajustan `sys.path` o porque se
invocan desde la raíz del proyecto:

```bash
python3 scripts/legacy/capture_demo.py
```

## Estado

Ninguno está cubierto por `scripts/test_all.py` ni por `scripts/validate_etapas56.py`.
Si necesita reproducir un cierre anterior, conserve estos archivos fuera del paquete
para no confundirlos con las herramientas vigentes.

# Entrega ToolchainOS V3.1

Entregar el ZIP nuevo entrega/ToolchainOS-V31-*.zip y su SHA256.
Conservar intacta la entrega V3.0; no fue reemplazada ni eliminada.

Incluye fuentes, kernel, ISO V3.1, scripts de construcción y pruebas,
documentación técnica de teclado/UTF-8, guía, evidencias actuales y manifiestos.
El disco build/data.img del ZIP es NUEVO y VACÍO, generado para la entrega.
No se incluyen discos del usuario, respaldos, QA, pesos Ollama ni configuraciones privadas.

Verificar: sha256sum -c SHA256SUMS.
ISO: build/toolchain-os.iso. Informe: docs/informes/INFORME_FINAL.md.
Evidencias: artifacts/final-validation/ y evidencias/.
Selección ES/US, TECLADO, tildes/ñ/diéresis/signos, Backspace y persistencia
se verificaron mediante pruebas host y QEMU real emulado.
IA real con Ollama 0.40.11 y qwen2.5:0.5b conserva los caracteres españoles.

No se presenta como Unicode completo: distribución PS/2, UTF-8 serial y glifos
gráficos son capas diferentes. Véase docs/guias/TECLADO_UTF8.md para el alcance y límites.

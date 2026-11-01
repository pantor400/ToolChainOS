# Avance académico V3.1 E56 del 29 de octubre de 2026

Novedades: heap kernel, PCB/TCB y Round Robin cooperativo. La numeración base V3.1 se conserva.
Guía actual: docs/guias/GUIA_DEMOSTRACION_ETAPAS56.md. Informe: docs/informes/AVANCE_ETAPAS56.md.
ISO actual: build/toolchain-os.iso. Resultados: artifacts/etapas56-20261029/results.json (19 grupos aprobados).
No hay aislamiento de usuario ni prevención. Los procesos comparten el kernel.

# ToolchainOS V3.1 — entrega docente

Novedades: selección inicial ES-LATAM (Perú) / US, TECLADO, entrada UTF-8,
Backspace por punto de código y fuente gráfica española ampliada.
Default ES tras 3 segundos sin confirmación; KERNEL_READY y OS> se conservan.

~~~bash
sha256sum -c SHA256SUMS
make iso
make check
python3 scripts/run_vm.py --headless
~~~
En la shell: TECLADO; TECLADO US; TECLADO ES; ECO año Perú programación.
Serial recibe el UTF-8 que genera el terminal anfitrión; TECLADO solo cambia
la traducción de PS/2 del huésped. No modifica Windows/WSL ni el teclado físico.

IA con servidor local activo:
~~~bash
OLLAMA_MODEL=qwen2.5:0.5b OLLAMA_URL=http://127.0.0.1:11434 python3 scripts/run_vm.py --ai --headless
~~~
Dentro: IA STATUS; IA ¿Qué es un sistema operativo?; IA CANCEL.
No se distribuyen servicio ni pesos de Ollama. No hay descarga automática.

Evidencia automatizada: 18 grupos superados, incluyendo español/inglés por PS/2
emulado, serial UTF-8, editor/Backspace, persistencia y Ollama real con tildes.
No se presenta como prueba humana de un teclado físico.
Serial conserva Unicode válido; el framebuffer representa un repertorio limitado.
No hay Unicode gráfico completo, normalización ni borrado general por grafemas.

ISO: build/toolchain-os.iso; kernel: build/kernel.elf.
La imagen build/data.img del ZIP es NUEVA y VACÍA; no procede de discos del usuario.
Evidencias: artifacts/final-validation/. Integridad: MANIFEST_SHA256.json y SHA256SUMS.
Guía: docs/guias/GUIA_DEMOSTRACION.md. Detalles/límites: docs/guias/TECLADO_UTF8.md.

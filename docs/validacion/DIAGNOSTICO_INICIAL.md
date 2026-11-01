# Diagnóstico inicial
Fecha: 2026-10-25 (America/Lima).
Inventario confirmado: Limine, C11/NASM x86-64, framebuffer, COM1, PS/2 por sondeo.
FS RAM de 64 nodos; RUN CPP es un intérprete limitado. Sin PMM, IDT propia,
temporizador, tareas, disco o puente IA inicialmente. No hay .git en esta copia.
Defectos encontrados: truncamiento silencioso de nombres, rutas, contenido y
líneas de consola; resolución permite atravesar archivos. Se corregirán antes
de ampliar operaciones. Se conserva el perfil q35; ATA usa pc/i440fx aparte.
Línea base ejecutada: make -B iso, make check y python3 scripts/smoke_test.py:
todos correctos. Versiones, logs y transcripción en artifacts/baseline/.
Respaldo inicial interno: artifacts/baseline/source-original.tar.gz.
No se accedió a la carpeta de respaldo externa. Artefactos preexistentes no se
consideran resultados de esta ejecución. El PDF del taller no fue proporcionado.

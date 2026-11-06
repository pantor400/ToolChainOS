# ToolchainOS Web: ejecución y demostración

Ejecutar los comandos desde la raíz del clon que contiene Makefile, apps/ y src/.
Requisitos: gcc/binutils, NASM, xorriso, Python 3, Pillow y QEMU x86.
El puente HTTP/HTTPS solo utiliza la biblioteca estándar de Python.

## Compilar y arrancar

~~~sh
make -j2 iso check
python3 scripts/run_vm.py --web
~~~

La ISO generada es build/toolchain-os.iso. QEMU GTK necesita WSLg o un servidor
gráfico; puede seleccionarse otro backend disponible con QEMU_DISPLAY.
Para pruebas sin ventana usar --headless. Para probar TCG explícitamente:

~~~sh
QEMU_ACCEL=tcg python3 scripts/run_vm.py --web
~~~

En la consola del huésped escribir GUI WEB. También se puede abrir GUI,
pulsar Apps y seleccionar navegador.elf (módulo 3).
La ventana Navegador empieza en about:inicio, ayuda local sin Internet.

## Demostración de navegación real

1. Introducir https://info.cern.ch/ y pulsar Enter.
2. Esperar HTTP 200 y el título recibido del servidor.
3. Tab selecciona Browse the first website; Enter abre el enlace.
4. Se descarga http://info.cern.ch/hypertext/WWW/TheProject.html.
5. < regresa, > avanza, R recarga e Inicio vuelve a la ayuda.
6. Probar http://example.com/ y example.com, que se normaliza a HTTPS.

Los enlaces y párrafos proceden de las respuestas HTTP, no de contenido
precargado. En la comprobación inicial de noviembre de 2026 example.com no
contenía enlaces HTML estáticos: CERN permite demostrar también enlaces.

Clic en la barra o L desde el documento permite sustituir/editar la URL.
Izquierda/derecha, Home/End, Backspace y Delete respetan UTF-8.
Tab recorre enlaces; Enter o clic abre el seleccionado.
Arriba/abajo, Home/End y la pista vertical desplazan la página. No se presupone
rueda de ratón en el ABI. X cancela una carga; la GUI sigue atendiendo eventos.
ESC sale de edición y después cierra la app. Otro ESC cierra el escritorio.
El botón rojo cierra directamente el navegador.

## IA y canales independientes

Ollama debe servir un modelo instalado. Usar el nombre que muestra ollama list:

~~~sh
OLLAMA_MODEL=nombre_del_modelo python3 scripts/run_vm.py --ai --web
~~~

Si Ollama no está iniciado, ejecutar ollama serve en otra terminal.
En el huésped: IA STATUS, IA hola y GUI WEB.
COM1 conserva consola, COM2 se dedica a IA y COM3 a web.
Sin modelo la web puede funcionar, pero la inferencia IA informará error.
Los mocks verifican transporte y no sustituyen una prueba de inferencia real.

Cerrar GUI y usar APAGAR. QEMU puede quedar detenido por -no-shutdown;
Ctrl+C en la terminal del lanzador detiene QEMU y ambos puentes y limpia
sus sockets propios. Un servidor Ollama iniciado aparte se detiene en su terminal.

## Pruebas reproducibles

~~~sh
make check test-memory-host test-utf8-host test-scheduler-host
make test-web-app-host test-web-transport-host test-keyboard-ring-host
python3 scripts/test_web_host.py
QEMU_ACCEL=tcg python3 scripts/test_web_qemu.py
QEMU_ACCEL=tcg python3 scripts/test_web_qemu.py --real --skip-offline
QEMU_ACCEL=tcg python3 scripts/test_web_large_qemu.py
QEMU_ACCEL=tcg python3 scripts/test_web_launcher.py
QEMU_ACCEL=tcg python3 scripts/test_web_profiles.py
QEMU_ACCEL=tcg python3 scripts/test_gui.py
QEMU_ACCEL=tcg python3 scripts/test_all.py
~~~

Ejecutar suites QEMU de una en una. Los tests generan registros/capturas en
artifacts/ y build/; no se incluyen esos resultados generados en el commit.
--real requiere Internet: si falla no debe sustituirse por un fixture y
declararse aprobada. --skip-offline omite la fase offline cuando ya fue validada.
La prueba large utiliza un fixture explícito cercano al límite de 24 KiB.
La prueba de perfiles restaura la ISO a 1280x720 al terminar.
CI ejecuta build/check, sanitizers, protocolo IA y pruebas web de host sin Internet.
La validación de esta integración está en [VALIDACION_TOOLCHAINOS_WEB.md](VALIDACION_TOOLCHAINOS_WEB.md).

## Diagnóstico y límites

- Sin puente disponible: iniciar con --web; la página local sigue disponible.
- Sin conexión: comprobar Internet de Ubuntu. El puente utiliza DNS del sistema
  y, si falla, DoH TLS de Cloudflare, sin cambiar resolv.conf.
- HTTPS no válido: corregir URL, reloj o cadena; no desactivar certificados.
- Timeout: cancelar o reintentar. El presupuesto COM3 total es 30 s incluyendo
  descarga y entrega UART; la GUI permanece activa.
- 404: es el estado recibido del servidor.
- Contenido no compatible: se admiten HTML y texto, no PDF/binarios/multimedia.
- Socket existente: comprobar la instancia anterior propia antes de retirarlo;
  nunca borrar sockets ajenos.
- --no-net --web desactiva la NIC del huésped, pero el puente aún utiliza Internet
  de Ubuntu. No constituye una prueba de TCP nativo del huésped.
- Sin --web no se inicia un servicio web invisible.

HTTP/TLS ocurre en Ubuntu. RTL8139 mantiene ICMP/DNS; no implementa TCP/TLS.
Apps ELF siguen en ring 0. Sin CSS completo, JavaScript, formularios/login,
cookies, multimedia, extensiones o pestañas.
Ver [arquitectura y límites](../diseno/NAVEGADOR_WEB.md).

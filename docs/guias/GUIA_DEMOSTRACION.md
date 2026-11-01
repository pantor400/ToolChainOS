# Demostración ToolchainOS V3.1

## Arranque y distribuciones
~~~bash
make iso
make check
python3 scripts/run_vm.py --headless
~~~
Elija 1 o 2 y Enter; sin confirmación en 3 segundos se usa español.
En headless esa selección no cambia el mapa de Windows/Ubuntu: el terminal
anfitrión ya envía UTF-8. Con PS/2 de QEMU sí cambia la traducción de posiciones.
~~~text
TECLADO
TECLADO US
TECLADO ES
HELP
ECO año Perú programación
ECO ¿Qué es un sistema operativo?
~~~
En QEMU con teclado PS/2, ES usa ñ y teclas muertas de acento/diéresis.
US conserva puntuación estadounidense. No hace falta instalar controladores.

## Editor y Backspace
~~~text
EDIT año
:clear
Perú y programación.
¡Hola! ¿Qué tal?
:wq
OPEN año
~~~
Durante una línea pruebe escribir ñ, Backspace y Ñ. El borrado elimina el carácter
multibyte completo. :show muestra el búfer; :line N texto reemplaza; :del N elimina;
:q cancela. Los límites se miden en bytes.

## Persistencia
Cierre la VM anterior. Use una imagen nueva dedicada:
~~~bash
python3 scripts/run_vm.py --ata --headless --disk build/demo-v31.img
~~~
Cree/edite un archivo con «año, Perú, programación». Espere PERSISTENCIA OK.
Cierre y repita el mismo comando sin recrear el disco; OPEN recupera los acentos.
No borre ni recree build/data.img o build/exposicion.img del usuario.

## IA real
Si Ollama ya está activo, no lo reinicie. Si no lo está, una terminal Ubuntu:
~~~bash
CUDA_VISIBLE_DEVICES=-1 HIP_VISIBLE_DEVICES=-1 ROCR_VISIBLE_DEVICES=-1 GGML_VK_VISIBLE_DEVICES=0 OLLAMA_LLM_LIBRARY=cpu OLLAMA_HOST=127.0.0.1:11434 /home/user/aplicaciones/ollama/bin/ollama serve
~~~
Otra terminal, desde el proyecto:
~~~bash
OLLAMA_MODEL=qwen2.5:0.5b OLLAMA_URL=http://127.0.0.1:11434 python3 scripts/run_vm.py --ai --headless
~~~
~~~text
IA STATUS
IA ¿Qué es un sistema operativo?
IA Repite: ¡Hola, Perú! El año trae programación y pingüinos. ¿Qué tal?
IA CANCEL
~~~
La respuesta termina con IA END. La inferencia es externa al kernel y se conserva
la consola COM1. En serial se conservan los caracteres UTF-8; la fuente gráfica
solo cubre un repertorio latino limitado. No prometa Unicode gráfico completo.

## Evidencia y cierre
Mostrar seleccion-teclado.png, espanol-framebuffer.png, espanol-redibujado.png
e ia-utf8-framebuffer.png de artifacts/final-validation/evidencias/ en el paquete.
ai-utf8-result.json y las transcripciones prueban texto real del modelo, no un mock.
Las pruebas actuales son automatizadas y las capturas se inspeccionaron; no se
afirma una prueba humana del teclado físico.
PMMTEST/UPTIME/TASKS siguen operativos. EXTEST DE/PF detienen deliberadamente
la VM: no usarlos dentro de la demostración principal.

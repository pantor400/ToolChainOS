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

## Escritorio, red, ratón y apps ELF
La ISO por defecto arranca a 1280x720 y la ventana de QEMU se abre con ese
mismo tamano (sin zoom), de modo que el escritorio se ve a escala 1:1;
`make run DISPLAY_QEMU=gtk,zoom-to-fit=on` la estira a la pantalla del anfitrion
y `make run FULLSCREEN=1` la abre sin decoraciones. Los perfiles HD y compacto
siguen disponibles reconstruyendo con `make iso RES=1920x1080` o
`make iso RES=1280x800`.
~~~text
RED
RED PING 10.0.2.2
RED DNS example.com
RED INTERNET
RATON
ELF
GUI
~~~
Con la red de usuario de QEMU (SLIRP) ya configurada, `RED PING` debe responder
en pocos milisegundos y `RED DNS`/`RED INTERNET` dan una IPv4 si el anfitrion
tiene Internet. Sin Internet, el DNS puede no responder: es el unico paso que
depende del exterior.
En el escritorio: mover el puntero, abrir Archivos, Red, Entrada y Apps desde el
dock, arrastrar una ventana por su barra de título y cerrarla con el botón o ESC.
Desde Apps se lanza una aplicación ELF; también directamente:
~~~text
GUI RUN 0
GUI RUN 1
GUI HOJA
~~~
`editor.elf` (índice 0) abre un editor de texto con una plantilla en blanco sin
nombre. Los botones `Nuevo`, `Abrir` y `Guardar` de su barra hacen lo que dicen:
`Abrir` y `Guardar` despliegan un selector con el árbol del sistema de archivos
(ruta actual, `..` y lista de archivos) donde se elige qué leer y en qué carpeta
dejar el texto escribiendo el nombre. Con el teclado, `TAB` abre el selector y
dentro alterna Abrir/Guardar, `Enter` confirma y `Q` cancela. Si el texto no cabe
aparece una barra vertical a la derecha que se puede pulsar y arrastrar.
`terminal.elf` (índice 1) ejecuta órdenes reales del shell —por ejemplo `help` o
`ls`—, guarda historial con las flechas, muestra su propia barra vertical cuando
la salida no cabe (`Inicio`/`Fin` van a los extremos) y `limpiar` borra la
pantalla mientras `salir` cierra.
`hoja.elf` (índice 2, o `GUI HOJA`) es la hoja de cálculo: rejilla de 8x16 con
fórmulas (`=b1-a1`, `=SUMA(a1:b3)`, `=PROMEDIO`, `=MIN`, `=MAX`, `=CONTAR`),
botones `Guardar`/`Abrir`/`Limpiar` sobre `hoja.csv` y flechas/RET/TAB/SUPR para
moverse y editar. En el mapa ES-LATAM hay que cambiar a `TECLADO US` para
escribir `=` y `-`, que no tienen tecla directa en español.
Para abrir un archivo concreto desde la shell: `GUI EDIT nota` (también sirve el
botón `Editar` de la ventana Archivos).
Al salir, ESC devuelve el control al kernel y la serie muestra
`ELF: FIN <nombre> CODIGO=0`; la GUI informa `GUI: VENTANA CERRADA ... LIMPIA`
para confirmar que el área de la ventana se devolvió al escritorio.
La barra superior muestra la hora local junto a la resolución y la IP; `HORA`
la imprime por consola con segundos y fecha.
En la ventana Sistema se leen CPU, núcleos, RAM, heap, tareas, red, ratón y
uptime; en Archivos, las flechas navegan, `Enter` abre, `n` crea un archivo,
`c` una carpeta, `r` renombra, `d` borra y `e` abre el archivo en el editor.
Todo se comprueba sin capturas con `make test-gui`: marcadores COM1 conducidos
por QMP (`GUI_READY`, `GUI_FRAME`, `IRQ_PAQUETES`, `SESION FINALIZADA`), la
latencia de la sesión gráfica y la ejecución de `help` desde la terminal, más el
cálculo y el guardado de la hoja de cálculo.
Para revisar el aspecto real están las capturas de
`python3 scripts/verify_editor_capture.py`: `artifacts/verificacion-terminal-scroll.png`,
`verificacion-editor-scroll.png`, `verificacion-editor-selector.png`,
`verificacion-editor-guardar.png`, `verificacion-ventana-abierta.png` y
`verificacion-ventana-cerrada.png`. La última demuestra que el escritorio
recupera su fondo y el dock su estado al cerrar la ventana.

## Evidencia y cierre
Mostrar seleccion-teclado.png, espanol-framebuffer.png, espanol-redibujado.png
e ia-utf8-framebuffer.png de artifacts/final-validation/evidencias/ en el paquete.
ai-utf8-result.json y las transcripciones prueban texto real del modelo, no un mock.
Las pruebas actuales son automatizadas y las capturas se inspeccionaron; no se
afirma una prueba humana del teclado físico.
PMMTEST/UPTIME/TASKS siguen operativos. EXTEST DE/PF detienen deliberadamente
la VM: no usarlos dentro de la demostración principal.


## Demostración web real

Navegador gráfico ELF integrado en Apps y GUI WEB. Iniciar con
python3 scripts/run_vm.py --web; IA simultánea con --ai --web.
HTTP/HTTPS y TLS verificado se ejecutan en Ubuntu por COM3; RTL8139
conserva ICMP/DNS y no implementa TCP/TLS nativos. HTML estático y enlaces,
sin CSS completo, JavaScript, login o multimedia. Apps en ring 0.
Consultar docs/diseno/NAVEGADOR_WEB.md y docs/guias/USO_NAVEGADOR.md
para límites y comandos; la validación de integración se registra en
docs/guias/VALIDACION_TOOLCHAINOS_WEB.md. Los resultados generados son locales.

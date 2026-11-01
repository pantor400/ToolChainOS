# Informe técnico V3.1: teclado, UTF-8 y consolas

## Inspección y diseño
V3.0 tenía tres obstáculos independientes: tabla PS/2 US fija, shell que filtraba
todo byte mayor de 126 y salida IA que sustituía bytes altos por ?. La fuente
5x7 y el historial gráfico solo aceptaban ASCII de un byte. FS/editor ya
guardaban texto por bytes y rechazaban excesos: no necesitaban cambiar su formato.

Se separan:
- Distribución: posiciones/scancodes PS/2 → caracteres.
- Codificación: caracteres Unicode → secuencias UTF-8 válidas.
- Representación: serial/terminal anfitrión o glifos del framebuffer huésped.

## PS/2 y selección
keyboard.c contiene un decodificador set 1 independiente y comprobable en host.
keyboard_menu.c ofrece 1/2 + Enter durante 300 ticks PIT (~3 segundos).
Sin confirmación se usa ES-LATAM; la selección se imprime antes de la bienvenida.
TECLADO consulta; TECLADO ES/US/1/2 cambia en ejecución. No es configuración persistente.
Cambiar distribución restablece modificadores y composición pendiente.

ES-LATAM sigue las posiciones habituales latinoamericanas: ñ junto a L,
acento agudo/diéresis en tecla muerta, ¿/¡, Shift y Caps Lock; AltGr+Q = @.
Se implementan también signos y algunas combinaciones AltGr/acentos grave/circunflejo.
US usa puntuación estadounidense sin teclas muertas de español.
Es un mapa educativo de teclas habituales, no toda la funcionalidad de una consola Linux.
Referencia de posiciones: [kbd la-latin1](https://github.com/legionus/kbd/blob/master/data/keymaps/i386/qwerty/la-latin1.map).
No se importó ni instaló un keymap del anfitrión.

Las pruebas inyectan teclas por QMP para atravesar el dispositivo PS/2 emulado,
puertos 0x64/0x60, modificadores y composición real del kernel. No son solo mocks
del mapa. No se controló ni modificó un teclado físico de Windows.

## Entrada y almacenamiento UTF-8
utf8.c valida secuencias de 1–4 bytes conforme a
[RFC 3629](https://www.rfc-editor.org/rfc/rfc3629).
Rechaza overlongs, sustitutos UTF-16, valores >U+10FFFF y secuencias incompletas.
La shell acumula hasta completar un escalar y entonces lo almacena/eco completo.
Una línea con UTF-8 inválido se descarta; no se ejecuta un comando parcialmente
alterado. Backspace cancela una secuencia aún incompleta o elimina el último
punto de código completo almacenado. No parte una ñ en dos bytes.

FS/editor conservan bytes UTF-8 sin normalización ni transliteración.
El formato persistente, los tamaños de nodos y los snapshots no cambian.
Los límites son bytes, no caracteres: 1023 de contenido, 31 de nombre,
127 de ruta y 1152 de entrada shell. El editor detecta exceso antes de modificar.
Comparación de nombres: casefold ASCII heredado, sin equivalencia Unicode NFC/NFD
ni comparación automática ñ/Ñ. Las teclas muertas PS/2 producen formas precompuestas.

## Consola serial --headless
COM1 recibe bytes del terminal Ubuntu, que ya aplicó su distribución y codificación.
TECLADO no los remapea. Se requiere un terminal UTF-8 con fuente adecuada.
Unicode válido sale sin sustituir tildes/ñ por ?; otros alfabetos también conservan
sus bytes aunque el framebuffer carezca del glifo.
Backspace maneja puntos de código. El cálculo de ancho para CJK/emoji es aproximado.
No hay segmentación completa de grafemas, secuencias ZWJ ni edición Unicode compleja.
NFD se conserva como bytes, pero su borrado/representación no tiene la garantía de NFC.

## Consola gráfica
Aunque QEMU usa -vga std, el kernel utiliza framebuffer RGB de 32 bits de Limine;
no usa memoria de texto VGA/CP437 ni una fuente ROM del BIOS.
cells pasa a guardar escalares de 32 bits para redibujar y cambiar colores sin
perder caracteres. glyph.c amplía la fuente didáctica a 5x9 celdas lógicas,
escaladas a 12x18 píxeles, sin controladores gráficos nuevos.
Se verifican áéíóú ÁÉÍÓÚ ñÑ üÜ ¿¡ y puntuación ASCII; se incluyen otras variantes
latinas y símbolos comunes. Comillas tipográficas/guiones tienen aproximación visual.
Los glifos no implementados se muestran como recuadro; el texto serial no cambia.
No se promete Unicode completo, shaping de escrituras complejas ni composición NFD.

## COM2 y Ollama
No se cambia TC/1: cabecera, IDs, checksum, límites, tiempos y tipos son los mismos.
No se modifica ai_bridge.py. El kernel interpreta el payload completo como UTF-8
y conserva caracteres válidos, filtrando controles. CR se normaliza y tabulaciones
se expanden al mostrar; no se ejecutan escapes de terminal enviados por el modelo.
Los bytes inválidos de salida se señalan con U+FFFD, no se borran tildes válidas.
El puente ya truncaba al límite sin dejar una secuencia UTF-8 incompleta.

Se hizo consulta abierta real del test heredado y otra de repetición para cubrir
deliberadamente tildes, eñe, diéresis y signos. Ollama devolvió:
«¡Hola, Perú! El año trae programación y pingüinos. ¿Qué tal?».
La prueba no simula un modelo: usa servicio local y qwen2.5:0.5b.
La prueba mock sigue marcada separadamente como transporte, sin confundir ambas.

## Validación y alcance
18 grupos finales superados. Host ASan/UBSan: roundtrip y fragmentación de
1 112 064 escalares válidos; secuencias inválidas, ES/US, teclas muertas,
Shift/Caps/AltGr, glifos y FS. QEMU: selección ES/US por teclado emulado,
default sin respuesta, cambio TECLADO, serial, Backspace, editor/FS,
persistencia UTF-8 en tres arranques, IA real, BIOS/UEFI y regresión.
Capturas reales inspeccionadas de menú, español, redibujado e IA.
No hubo prueba manual humana del teclado físico ni cambios del anfitrión.

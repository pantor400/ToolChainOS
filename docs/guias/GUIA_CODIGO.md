> Documento histórico de la versión inicial. Para el estado actual consulte\n> ../diseno/ARQUITECTURA.md, ../diseno/FASES_IMPLEMENTADAS.md y ../validacion/PRUEBAS_Y_RESULTADOS.md.\n\n# Guía del código de la consola interactiva V2

Este núcleo educativo muestra cómo C y NASM trabajan juntos, y cómo se escribe directamente en un framebuffer. La demo ya está compilada en demo/toolchain-os.iso. Para editar y reconstruir, abre el proyecto en VS Code con Linux/WSL y sigue README.md.

## Primero prueba lo que vas a explicar

1. Ejecuta INICIAR_DEMO_WINDOWS.bat.
2. Escribe SUMA y Enter. Ingresa 20 y Enter; luego -7 y Enter. Debe aparecer 20 + (-7) = 13.
3. Escribe SUMA 35 12. Debe aparecer 47 sin hacer preguntas.
4. Escribe TEXTO. En la siguiente línea escribe Hola mi King y Enter. El núcleo responde ESCRIBISTE: Hola mi King.
5. Escribe COLOR. Elige ROJO. Las letras del historial visible cambian de color.
6. Escribe FONDO CLARO. Se cambia la pareja de fondo y letras para mantener contraste.
7. Escribe COLOR CIAN para regresar al fondo oscuro con letras cian.
8. Prueba SUMA hola 2 para demostrar que se rechazan datos incorrectos.

No es necesario modificar el código para cambiar los números o el mensaje.

## Cómo llega una tecla al núcleo

Archivo: src/console.c. Función: console_read.

El núcleo consulta el puerto serie y el controlador del teclado PS/2. Al pulsar una tecla, PS/2 entrega un scancode, que es un identificador de tecla. La tabla map lo convierte en un carácter como a, 5 o Enter. No son el mismo dato: el scancode identifica la tecla; el carácter representa el texto que queremos utilizar.

Los estados shift_left, shift_right y caps permiten distinguir mayúsculas y minúsculas. El mapa es US y solo se admiten caracteres ASCII imprimibles. No se implementó todavía un controlador completo para idiomas, Unicode o teclado USB directo.

Cuando no hay tecla disponible, console_read devuelve -1. kmain continúa consultando sin tratar ese valor como una letra. Este es un diseño por sondeo; no hemos implementado IRQ de teclado.

## Cómo se guarda lo que escribes

Archivo: src/kernel.c. Función: kmain.

```c
char line[121];
size_t used = 0;
```

line reserva 121 posiciones: 120 para caracteres y una para el terminador cero. used indica cuántos caracteres hay escritos.

Al recibir una letra válida, se guarda en line[used], aumenta used y se dibuja con console_putc. El carácter no se convierte a mayúscula: así se conserva el mensaje original.

Al pulsar retroceso, used disminuye si hay algo que borrar y la consola elimina el último carácter visible. Al pulsar Enter, se escribe el terminador cero y se llama a execute(line). Ese cero marca dónde termina una cadena C; no es el carácter del número 0.

La comparación de comandos ignora mayúsculas mediante upper/equal, pero no modifica el texto almacenado. Por eso suma, SUMA y Suma ejecutan lo mismo y ECO Hola mantiene su H mayúscula.

## Cómo reconoce cada comando

Archivo: src/kernel.c. Función: execute.

En modo normal separa la primera palabra de sus argumentos. Por ejemplo, en SUMA 25 -8, el comando es SUMA y los argumentos son 25 -8. Después compara el comando con las opciones disponibles y llama a su función.

Antes de separar palabras revisa state. Si el usuario está respondiendo a una pregunta, su línea debe interpretarse como un número, texto o color; no como un comando nuevo.

## Por qué existe state

La variable state recuerda qué espera el programa:

| Estado | Qué espera |
|---|---|
| COMMAND | Un comando como SUMA o COLOR. |
| FIRST_NUMBER | El primer operando de una suma guiada. |
| SECOND_NUMBER | El segundo operando. |
| TEXT_INPUT | La frase que se repetirá. |
| COLOR_INPUT | El nombre de un color de letras. |
| BACKGROUND_INPUT | El nombre de un fondo. |

Al escribir SUMA sin argumentos pasa de COMMAND a FIRST_NUMBER. Tras un número válido guarda first_number y pasa a SECOND_NUMBER. Con el segundo calcula el resultado y vuelve a COMMAND.

Si se escribe un dato incorrecto permanece en el estado actual para permitir corregirlo. CANCELAR permite volver a COMMAND. La función prompt muestra la pregunta apropiada: OS>, PRIMER NUMERO>, SEGUNDO NUMERO>, etc.

Esto se llama máquina de estados: el comportamiento depende de la entrada y del estado que el programa recuerda.

## Cómo el texto 25 se convierte en el número 25

Archivo: src/kernel.c. Función: number.

Las teclas producen caracteres. El carácter '2' no tiene el mismo valor interno que el entero 2. Por eso se convierte cada dígito con una resta:

```c
unsigned digit = *s - '0';
n = n * 10 + digit;
```

Para leer 25: comenzamos con n=0; llega 2 y n pasa a 2; llega 5 y hacemos 2*10+5=25. Si hay un signo menos, se aplica al final.

Se comprueba el límite ANTES de multiplicar y sumar, evitando que una entrada excesiva desborde el acumulador. Cada operando está limitado a +/-1000000000; así la suma cabe con margen en int64_t. Los decimales, letras mezcladas y argumentos extra se rechazan.

## Dónde se suma realmente

Archivo C: src/kernel.c, función sum_result.

```c
int64_t result = asm_sumar(a, b);
```

Archivo NASM: src/cpu.asm.

```asm
asm_sumar:
    mov rax, rdi
    add rax, rsi
    ret
```

Según System V AMD64 ABI, el primer argumento entero llega en RDI, el segundo en RSI y el resultado se devuelve en RAX. MOV copia el primer valor a RAX; ADD incorpora el segundo; RET vuelve al código que llamó la función.

GCC genera kernel.o, NASM genera cpu.o y ld resuelve la referencia asm_sumar al enlazarlos. Esta función muestra de forma concreta para qué sirven los archivos objeto y el enlazador.

No hay una lista de resultados guardados. El núcleo lee los números que ingresas y la instrucción ADD realiza la operación con esos valores.

## Cómo se muestra un número

Archivo: src/console.c. Funciones: console_signed y console_dec.

console_signed comprueba si el número es negativo y dibuja el signo cuando corresponde. console_dec separa sus dígitos utilizando división y resto entre 10. Después dibuja esos dígitos como caracteres. Es el proceso inverso a leer el número escrito por el usuario.

Los paréntesis alrededor del segundo número negativo son solo presentación para que 25 + (-8) se lea con claridad.

## Cómo se repite el texto

En TEXTO se cambia a TEXT_INPUT. La siguiente línea se imprime mediante console_write, precedida por ESCRIBISTE:. ECO permite proporcionar el mensaje en la misma línea.

No se guarda en disco ni se envía a Internet. Se utiliza el buffer temporal y se imprime en framebuffer y serie. Las mayúsculas, minúsculas y espacios interiores se conservan. El límite es de 120 caracteres por línea. Para TEXTO guiado, la palabra CANCELAR se reserva para salir.

## Cómo funcionan los colores

Archivo: src/kernel.c. Funciones: color y background.

El nombre del color se asocia a un valor RGB. Por ejemplo, 0xFF8A8A representa un rojo suave: FF es el componente rojo, 8A el verde y 8A el azul. Aquí 0x indica hexadecimal; ese número describe un color, no una dirección de memoria. El significado depende de dónde se utilice.

La función console_set_colors recibe el color del texto y del fondo. En console.c, rgb adapta los componentes a las posiciones de bits que informa el framebuffer de Limine.

COLOR siempre elige letras sobre fondo oscuro. FONDO aplica una pareja completa de colores para mantener contraste. No existe selección arbitraria de RGB desde la consola en esta versión.

## Cómo se dibuja en pantalla

Archivo: src/console.c. Funciones: pixel y draw_char.

pixel localiza un píxel con:

```c
address + y * pitch + x * 4
```

address es el comienzo del framebuffer; y el número de fila; pitch los bytes entre comienzos de filas; x el número de columna. Multiplicamos x por 4 porque se utiliza un framebuffer de 32 bits por píxel, es decir, cuatro bytes.

No usamos width*4 para localizar la siguiente fila: puede existir relleno y por eso se respeta pitch.

La función draw_char consulta la matriz de bits de font.h. Un bit encendido dibuja el color de la letra y uno apagado dibuja el fondo. La fuente tiene 5x7 puntos y se amplía al dibujar. Se añadieron formas minúsculas propias para que Hola y HOLA se distingan visualmente.

## Cómo cambia el color sin perder el historial

La matriz cells conserva los caracteres que aparecen en la pantalla visible. Cuando cambias de color, console_set_colors pinta el fondo y vuelve a dibujar esos caracteres con los nuevos colores.

La matriz se desplaza cuando la pantalla se llena; el historial fuera de la pantalla no se conserva. Esto no es un sistema de ventanas ni un controlador gráfico completo, sino un renderizador sencillo de texto sobre píxeles.

El puerto serie transmite texto, así que la terminal de Windows puede seguir viéndose con sus propios colores. Observa el cambio en la ventana gráfica de QEMU.

## Qué archivos debes mostrar al profesor

| Archivo | Qué demostrar |
|---|---|
| src/kernel.c | Lectura de comandos, estados, números y selección de acciones. |
| src/cpu.asm | Suma real mediante instrucciones del procesador. |
| src/console.c | Teclado, framebuffer, texto y colores. |
| include/font.h | Representación de letras mediante bits. |
| linker.ld | Conexión con la etapa de enlace de la exposición. |
| Makefile | Cómo se compila y se genera la ISO. |

## Explicación oral breve

La entrada del teclado se convierte en caracteres y se guarda en un buffer. Al pulsar Enter, el núcleo reconoce el comando. Para sumar convierte los caracteres en enteros y llama a una función NASM que devuelve el resultado a C. Para repetir texto recorre la cadena y la dibuja. Para cambiar colores actualiza los valores RGB y redibuja los caracteres sobre el framebuffer que proporcionó Limine.

## Cambiar el programa y volver a ejecutarlo

En Linux/WSL, desde la raíz del proyecto:

```bash
make iso check
python3 scripts/smoke_test.py
make run
```

Para que el BAT de Windows use esa compilación nueva, copia build/toolchain-os.iso a demo/toolchain-os.iso. Editar kernel.c y abrir la ISO vieja no actualiza el programa automáticamente. Cierra la instancia anterior de QEMU antes de comparar los resultados.

#!/usr/bin/env python3
"""Pruebas reales por UART: arranque, parser, suma, texto y comandos de color."""
import os,select,subprocess,time
from pathlib import Path
import qemu_config
iso=Path('build/toolchain-os.iso')
if not iso.exists():raise SystemExit('Primero ejecuta make iso')
q=subprocess.Popen(qemu_config.base(iso=iso)+['-nic','none']+qemu_config.display('none')+qemu_config.serial_stdio(),stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
output=bytearray();transcript=bytearray()
def until(needle,seconds=30):
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        if needle in output:return
        if q.poll() is not None:raise RuntimeError(q.stderr.read().decode(errors='replace'))
        ready,_,_=select.select([q.stdout],[],[],.1)
        if ready:
            data=os.read(q.stdout.fileno(),4096);output.extend(data);transcript.extend(data)
    raise RuntimeError('Tiempo agotado: '+output.decode(errors='replace'))
def send(text,prompt='OS> ',contains=()):
    output.clear();q.stdin.write(text.encode('ascii'));q.stdin.flush();until(prompt.encode())
    for expected in contains:assert expected.encode() in output,output.decode(errors='replace')
try:
    until(b'OS> ')
    send('suma 25 -8\n',contains=['25 + (-8) = 17'])
    send('SUMA -1000000000 -1000000000\n',contains=['= -2000000000'])
    send('suma 1000000000 1000000000\n',contains=['= 2000000000'])
    for bad in ['1.5 2','1 2 3','1000000001 2','999999999999999999999 1','2x 3','1']:
        send('suma '+bad+'\n',contains=['USO: SUMA'])
    send('suma\n','PRIMER NUMERO> ')
    send('hola\n','PRIMER NUMERO> ',['USA UN ENTERO'])
    send('-12\n','SEGUNDO NUMERO> ')
    send('7\n',contains=['-12 + 7 = -5'])
    send('suma\n','PRIMER NUMERO> ')
    send('cancelar\n',contains=['OPERACION CANCELADA'])
    send('texto\n','TU TEXTO> ')
    send('Hola mi King, C y NASM!\n',contains=['ESCRIBISTE: Hola mi King, C y NASM!'])
    send('eco Hola   mundo\n',contains=['ESCRIBISTE: Hola   mundo'])
    send('eco abcx\bD\n',contains=['ESCRIBISTE: abcD'])
    send('color\n','COLOR> ')
    send('invalido\n','COLOR> ',['COLOR NO RECONOCIDO'])
    send('rojo\n',contains=['COLOR DE TEXTO ACTUALIZADO'])
    for color in ['azul','verde','amarillo','blanco','morado','cian']:
        send('color '+color+'\n',contains=['COLOR DE TEXTO ACTUALIZADO'])
    for background in ['claro','oscuro','azul']:
        send('fondo '+background+'\n',contains=['FONDO ACTUALIZADO'])
    send('fondo\n','FONDO> ')
    send('claro\n',contains=['FONDO ACTUALIZADO'])
    send('info\n',contains=['PAGINACION: ACTIVA','BASE VIRTUAL REAL'])
    send('mem\n',contains=['REGIONES DEL MAPA:'])
    send('limpiar\n')
    send('open nota\n',contains=['Bienvenido a ToolchainOS'])
    send('open script\n',contains=['int main()'])
    send('run cpp script\n',contains=['EJECUTANDO SCRIPT C++: script','VALOR DE RETORNO (main): 55'])
    send('mkdir docs\n',contains=['CARPETA CREADA CON EXITO'])
    send('cd docs\n',contains=['DIRECTORIO ACTUAL: /docs'])
    send('crear saludo Hola ToolchainOS\n',contains=['ARCHIVO GUARDADO CON EXITO'])
    send('open saludo\n',contains=['Hola ToolchainOS'])
    send('cd ..\n',contains=['DIRECTORIO ACTUAL: /'])
    send('open docs/saludo\n',contains=['Hola ToolchainOS'])
    send('mkdir\n','DONDE GUARDAR> ')
    send('/\n','NOMBRE CARPETA> ')
    send('archivos\n',contains=['CARPETA CREADA CON EXITO'])
    send('crear\n','DONDE GUARDAR> ')
    send('archivos\n','NOMBRE ARCHIVO> ')
    send('mensaje\n','CONTENIDO> ')
    send('Texto guardado en carpeta archivos\n',contains=['ARCHIVO GUARDADO CON EXITO'])
    send('open\n','ARCHIVO> ')
    send('archivos/mensaje\n',contains=['Texto guardado en carpeta archivos'])
    send('crear calc int main(){ int a = 14; int b = 28; return a + b; }\n',contains=['ARCHIVO GUARDADO CON EXITO'])
    send('run cpp calc\n',contains=['VALOR DE RETORNO (main): 42'])
    send('open noexiste\n',contains=['ARCHIVO NO ENCONTRADO'])
    send('run cpp noexiste\n',contains=['ARCHIVO NO ENCONTRADO'])
    send('ls\n',contains=['[DIR] docs','[DIR] archivos','[TXT] script','[TXT] nota','[TXT] calc'])
    send('pwd\n',contains=['DIRECTORIO ACTUAL: /'])
    send('eco Fin de prueba\r\n',contains=['ESCRIBISTE: Fin de prueba'])
    print('OK: suma directa/guiada, limites, colores, fs (carpetas/archivos/open), y ejecutor C++ script.')
finally:
    Path('build/prueba-interactiva.txt').write_bytes(transcript)
    q.terminate()
    try:q.wait(timeout=3)
    except subprocess.TimeoutExpired:q.kill();q.wait()

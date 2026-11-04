#!/usr/bin/env python3
"""Verificacion visual de las barras de desplazamiento y del selector.

Arranca QEMU sin ventana, abre la terminal grafica con salida larga y el
editor con mas lineas de las que caben, y guarda capturas en artifacts/ para
comprobar que la barra vertical aparece, que el selector muestra el arbol del
FS y que el escritorio queda limpio al cerrar las ventanas.

Uso: python3 scripts/verify_editor_capture.py
"""

import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import qemu_config
from test_gui import QMP, wait_for

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / 'build' / 'toolchain-os.iso'
OUT = ROOT / 'artifacts'


def capture(client, name, delay=0.6):
    time.sleep(delay)
    client.call('screendump', {'filename': str(OUT / name), 'format': 'png'})
    print('CAPTURA %s' % name)


def main():
    if not ISO.exists():
        print('Falta %s: ejecuta make iso.' % ISO)
        return 2
    OUT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='capture-') as workdir:
        serial_path = Path(workdir) / 'serial.log'
        socket_path = Path(workdir) / 'qmp.sock'
        command = (qemu_config.base(iso=ISO) + qemu_config.net()
                   + qemu_config.display('none') + qemu_config.serial_file(serial_path)
                   + qemu_config.qmp(socket_path))
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        client = None
        try:
            wait_for(serial_path, 'KERNEL_READY', 40, 'arranque del kernel')
            client = QMP(socket_path)
            wait_for(serial_path, 'OS> ', 10, 'prompt de la shell')

            # Terminal grafica: dos volcados de la ayuda superan las filas
            # visibles y obligan a mostrar la barra vertical.
            client.ascii('gui run 1\n')
            wait_for(serial_path, 'ELF: EJECUTANDO terminal.elf', 20, 'terminal ELF')
            client.ascii('help\n')
            wait_for(serial_path, 'INTERFAZ GRAFICA, RED Y APLICACIONES', 20, 'ayuda 1')
            client.ascii('help\n')
            time.sleep(1.0)
            capture(client, 'verificacion-terminal-scroll.png')
            client.key('esc')
            wait_for(serial_path, 'ELF: FIN terminal.elf', 20, 'cierre de la terminal')

            # Editor: se escriben mas lineas de las que caben en el area de
            # texto para que la barra aparezca, y luego TAB abre el selector.
            client.ascii('gui run 0\n')
            wait_for(serial_path, 'editor.elf: PLANTILLA NUEVA', 20, 'plantilla del editor')
            renglon = 1
            for _ in range(40):
                client.ascii('linea ')
                client.ascii('%d' % (renglon // 10 % 10))
                client.ascii('%d' % (renglon % 10))
                client.key('ret')
                renglon += 1
            time.sleep(1.0)
            capture(client, 'verificacion-editor-scroll.png')
            client.key('tab')
            wait_for(serial_path, 'editor.elf: SELECTOR ABRIR', 15, 'selector abrir')
            capture(client, 'verificacion-editor-selector.png')
            client.key('tab')
            wait_for(serial_path, 'editor.elf: SELECTOR GUARDAR', 15, 'selector guardar')
            capture(client, 'verificacion-editor-guardar.png')
            client.key('q')
            time.sleep(0.4)
            client.key('esc')
            wait_for(serial_path, 'GUI: VENTANA CERRADA Editor de texto LIMPIA', 20,
                     'cierre limpio del editor')
            wait_for(serial_path, 'SESION FINALIZADA', 20, 'vuelta a la consola')

            # Sesion interactiva: abrir y cerrar una ventana con el raton deja
            # el escritorio sin restos del fotograma anterior.
            client.ascii('gui\n')
            wait_for(serial_path, 'GUI_READY RES=', 15, 'escritorio interactivo')
            for _ in range(10):
                client.move(12, 32)              # centro -> boton Apps
            client.button(True)
            client.button(False)
            time.sleep(0.8)
            capture(client, 'verificacion-ventana-abierta.png')
            client.move(-287, -459)              # boton de cerrar de la ventana
            client.button(True)
            client.button(False)
            wait_for(serial_path, 'GUI: VENTANA CERRADA Aplicaciones LIMPIA', 10,
                     'el cierre devuelve el area al escritorio')
            capture(client, 'verificacion-ventana-cerrada.png')
            print('VERIFICACION VISUAL COMPLETA')
            return 0
        finally:
            if client:
                client.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == '__main__':
    sys.exit(main())

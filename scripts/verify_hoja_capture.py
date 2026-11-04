#!/usr/bin/env python3
"""Captura de la hoja de calculo funcionando en el escritorio.

Arranca QEMU sin ventana con el mapa US (necesario para teclear '=' y '-'),
abre `GUI HOJA`, escribe tres importes en la columna A y una formula de rango,
deja la celda de la formula seleccionada para que se vea en la barra superior y
guarda una captura en artifacts/ como evidencia visual.

Uso: python3 scripts/verify_hoja_capture.py
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


def capture(client, name, delay=0.8):
    time.sleep(delay)
    client.call('screendump', {'filename': str(OUT / name), 'format': 'png'})
    print('CAPTURA %s' % name)


def shifted(client, qcode):
    """Pulsa shift junto con una tecla para los simbolos que lo exigen."""
    client.call('send-key', {'keys': [{'type': 'qcode', 'data': 'shift'},
                                      {'type': 'qcode', 'data': qcode}],
                             'hold-time': 20})
    time.sleep(0.05)


def type_formula(client, text):
    """Teclea una formula traduciendo parentesis y dos puntos a shift+tecla."""
    symbols = {'(': '9', ')': '0', ':': 'semicolon'}
    for character in text:
        if character in symbols:
            shifted(client, symbols[character])
        else:
            client.ascii(character)


def main():
    if not ISO.exists():
        print('Falta %s: ejecuta make iso.' % ISO)
        return 2
    OUT.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='captura-hoja-') as workdir:
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

            # El mapa ES-LATAM no tiene '=' ni '-' directos: se cambia a US solo
            # para la demostracion.
            client.ascii('teclado us\n')
            wait_for(serial_path, 'TECLADO: US', 10, 'mapa US para formulas')

            client.ascii('gui hoja\n')
            wait_for(serial_path, 'hoja.elf: HOJA 8X16 LISTA ABI=5', 20,
                     'hoja de calculo en el escritorio')

            for importe in ('1200', '850', '640'):
                client.ascii(importe)
                client.key('ret')
            type_formula(client, '=suma(a1:a3)')
            client.key('ret')
            wait_for(serial_path, 'hoja.elf: FORMULA A4 =suma(a1:a3) -> 2690', 20,
                     'la formula de rango calcula el total')

            # RET deja la celda activa en A5; se sube para que la barra de
            # formula muestre el texto de la formula junto al total.
            client.key('up')
            capture(client, 'e56-hoja-calculo.png')

            client.key('esc')
            wait_for(serial_path, 'hoja.elf: fin correcto', 20, 'cierre de la hoja')
            client.key('esc')
            wait_for(serial_path, 'SESION FINALIZADA', 20, 'vuelta a la consola')
            print('CAPTURA COMPLETA')
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

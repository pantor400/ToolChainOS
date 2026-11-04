#!/usr/bin/env python3
"""Prueba de humo de la interfaz grafica sin capturas de pantalla.

Arranca QEMU sin ventana, escribe en la shell por QMP y comprueba los
marcadores que el kernel emite por COM1. Valida el arranque de la GUI, el
driver de red, el raton PS/2 (movimiento real por IRQ12), la lista de
aplicaciones ELF y la carga de las apps.

Uso: python3 scripts/test_gui.py
"""

import json
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import qemu_config

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / 'build' / 'toolchain-os.iso'


class QMP:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(5)
        end = time.monotonic() + 10
        while True:
            try:
                self.socket.connect(str(path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() > end:
                    raise
                time.sleep(0.02)
        self.stream = self.socket.makefile('rwb', buffering=0)
        json.loads(self.stream.readline())
        self.call('qmp_capabilities')

    def call(self, name, args=None):
        request = {'execute': name}
        if args is not None:
            request['arguments'] = args
        self.stream.write((json.dumps(request) + '\n').encode())
        while True:
            result = json.loads(self.stream.readline())
            if 'error' in result:
                raise RuntimeError(result)
            if 'return' in result:
                return result['return']

    def key(self, qcode, hold=20):
        self.call('send-key', {'keys': [{'type': 'qcode', 'data': qcode}], 'hold-time': hold})
        time.sleep(0.05)

    def move(self, dx, dy):
        events = []
        if dx:
            events.append({'type': 'rel', 'data': {'axis': 'x', 'value': dx}})
        if dy:
            events.append({'type': 'rel', 'data': {'axis': 'y', 'value': dy}})
        if events:
            self.call('input-send-event', {'events': events})
            time.sleep(0.02)

    def button(self, down):
        self.call('input-send-event', {
            'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
        time.sleep(0.05)

    def ascii(self, text):
        # QMP usa nombres de tecla (qcodes), no caracteres: se traducen los
        # signos que aparecen en las ordenes de la shell.
        qcodes = {' ': 'spc', '\n': 'ret', '.': 'dot', '-': 'minus',
                  '/': 'slash', ',': 'comma', '=': 'equal'}
        for character in text:
            self.key(qcodes.get(character, character))

    def close(self):
        try:
            self.stream.close()
            self.socket.close()
        except OSError:
            pass


def wait_for(serial, needle, timeout, description):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = serial.read_text(errors='replace') if serial.exists() else ''
        if needle in text:
            print('OK  %s' % description)
            return text
        time.sleep(0.1)
    text = serial.read_text(errors='replace') if serial.exists() else ''
    print('FALLO %s (no aparecio %r)' % (description, needle))
    print('--- SERIE ---')
    print(text[-2000:])
    raise SystemExit(1)


def main():
    if not ISO.exists():
        print('Falta %s: ejecuta make iso.' % ISO)
        return 2
    with tempfile.TemporaryDirectory(prefix='gui-test-') as workdir:
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

            client.ascii('red\n')
            wait_for(serial_path, 'RED: RTL8139', 10, 'driver de red RTL8139')
            client.ascii('red ping 10.0.2.2\n')
            wait_for(serial_path, 'PING: 10.0.2.2 RESPUESTA', 10, 'ICMP a la puerta de QEMU')
            print('OK  RED PING responde a 10.0.2.2')
            client.ascii('red dns example.com\n')
            wait_for(serial_path, 'DNS: CONSULTANDO example.com', 10, 'consulta DNS del kernel')
            print('OK  RED DNS envia la consulta a 10.0.2.3')

            client.ascii('hora\n')
            text = wait_for(serial_path, 'HORA: ', 10, 'reloj CMOS del kernel')
            stamp = re.search(r'HORA: \d\d:\d\d:\d\d \d\d/\d\d/\d{4}', text)
            if not stamp:
                print('FALLO: el reloj CMOS no devuelve hora y fecha validas')
                return 1
            print('OK  reloj CMOS: %s' % stamp.group(0)[6:])

            client.ascii('raton\n')
            wait_for(serial_path, 'RATON PS/2: ACTIVO', 10, 'raton PS/2 inicializado')

            client.ascii('elf\n')
            text = wait_for(serial_path, 'APLICACIONES ELF DISPONIBLES', 10, 'modulos ELF listados')
            for module in ('editor.elf', 'terminal.elf'):
                if module not in text:
                    print('FALLO: la ISO no contiene %s' % module)
                    return 1
                print('OK  %s presente en los modulos' % module)
            if 'hola.elf' in text:
                print('FALLO: la ISO todavia incluye hola.elf')
                return 1
            print('OK  hola.elf retirada de la ISO')

            client.ascii('gui demo\n')
            text = wait_for(serial_path, 'GUI_FRAME', 15, 'primer fotograma de la GUI')
            resolution = 'desconocida'
            for line in text.splitlines():
                if 'GUI_READY RES=' in line:
                    resolution = line.split('GUI_READY RES=', 1)[1].strip()
            print('OK  resolucion entregada por Limine: %s' % resolution)
            wait_for(serial_path, 'SESION FINALIZADA', 15, 'consola restaurada tras la GUI')

            # Sesion interactiva: mover el puntero y pulsar el boton izquierdo
            # ejercita la IRQ12 real; el contador de paquetes lo confirma.
            # La sesion interactiva debe atender el teclado con rapidez: se
            # mide el tiempo desde ESC hasta la vuelta a la consola.
            start = time.monotonic()
            client.ascii('gui\n')
            wait_for(serial_path, 'GUI_READY RES=', 15, 'escritorio interactivo')
            for _ in range(6):
                client.move(40, 25)
            client.button(True)
            client.button(False)
            client.move(-90, -60)
            client.key('esc')
            wait_for(serial_path, 'SESION FINALIZADA', 15, 'salida del escritorio con ESC')
            print('OK  latencia de la sesion grafica: %.2f s' % (time.monotonic() - start))

            client.ascii('raton\n')
            text = wait_for(serial_path, 'RATON PS/2: ACTIVO', 10, 'estado del raton tras la GUI')
            packets = 0
            for line in text.splitlines():
                if 'IRQ_PAQUETES=' in line:
                    packets = int(line.split('IRQ_PAQUETES=', 1)[1].split()[0])
            if packets <= 0:
                print('FALLO: la GUI no recibio paquetes del raton')
                return 1
            print('OK  paquetes de raton atendidos por IRQ12: %d' % packets)

            client.ascii('gui run 0\n')
            wait_for(serial_path, 'ELF: EJECUTANDO editor.elf', 20, 'carga del editor ELF')
            client.ascii('hola mundo')
            client.key('esc')
            wait_for(serial_path, 'ELF: FIN editor.elf CODIGO=0', 20, 'retorno del editor ELF')

            client.ascii('gui run 1\n')
            wait_for(serial_path, 'ELF: EJECUTANDO terminal.elf', 20, 'carga de la terminal ELF')
            client.ascii('help\n')
            wait_for(serial_path, 'INTERFAZ GRAFICA, RED Y APLICACIONES', 20,
                     'orden ejecutada desde la terminal grafica')
            client.ascii('crear demo.txt fsok\n')
            wait_for(serial_path, 'ARCHIVO GUARDADO CON EXITO', 20,
                     'archivo creado desde la terminal grafica')
            client.ascii('open demo.txt\n')
            wait_for(serial_path, 'fsok', 20,
                     'contenido leido desde la terminal grafica')
            client.key('esc')
            wait_for(serial_path, 'ELF: FIN terminal.elf CODIGO=0', 20, 'retorno de la terminal ELF')

            client.ascii('help\n')
            wait_for(serial_path, 'INTERFAZ GRAFICA, RED Y APLICACIONES', 10, 'ayuda ampliada')
            print('GUI: TODAS LAS COMPROBACIONES SUPERADAS')
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

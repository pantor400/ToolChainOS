#!/usr/bin/env python3
"""Apagado nativo del equipo con la orden APAGAR.

QEMU se lanza con -no-shutdown, asi que al pasar a S5 detiene la maquina pero
no termina el proceso: la prueba espera el evento QMP SHUTDOWN en lugar de
confiar en que el emulador muera.

Orden cubierta:
  1. APAGAR escrito en la consola serie (cierre del escritorio si lo hay y
     ACPI S5 por PM1a_CNT).

Uso: python3 scripts/test_apagar.py  (QEMU_ACCEL=tcg tambien es valido)
"""

import json
import select
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import qemu_config

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / 'build' / 'toolchain-os.iso'


class Machine:
    """Una sesion de QEMU: serie en fichero y control por QMP."""

    def __init__(self, workdir, name):
        self.serial = Path(workdir) / (name + '.log')
        self.socket_path = Path(workdir) / (name + '.sock')
        command = (qemu_config.base(iso=ISO) + qemu_config.display('none')
                   + qemu_config.serial_file(self.serial)
                   + qemu_config.qmp(self.socket_path))
        self.process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                        stderr=subprocess.STDOUT)
        self.socket = None
        self.buffer = b''

    def text(self):
        return self.serial.read_text(errors='replace') if self.serial.exists() else ''

    def until(self, needle, seconds=60):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if needle in self.text():
                print('OK  %s' % needle)
                return
            if self.process.poll() is not None:
                raise AssertionError('QEMU termino sin %r:\n%s' % (needle, self.text()[-2000:]))
            time.sleep(0.1)
        raise AssertionError('Sin %r tras %ds:\n%s' % (needle, seconds, self.text()[-2000:]))

    # --- QMP con bufer propio: eventos y respuestas llegan mezclados --------

    def connect(self):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(5)
        end = time.monotonic() + 15
        while True:
            try:
                self.socket.connect(str(self.socket_path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() > end:
                    raise
                time.sleep(0.02)
        self.wait_message(lambda message: 'QMP' in message, 10)
        self.send({'execute': 'qmp_capabilities'})
        self.wait_message(lambda message: 'return' in message, 10)

    def pump(self):
        ready, _, _ = select.select([self.socket], [], [], 0.2)
        if not ready:
            return
        data = self.socket.recv(4096)
        if data:
            self.buffer += data

    def messages(self):
        while b'\n' in self.buffer:
            line, self.buffer = self.buffer.split(b'\n', 1)
            if line.strip():
                yield json.loads(line)

    def wait_message(self, predicate, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for message in list(self.messages()):
                if predicate(message):
                    return message
                if 'error' in message:
                    raise AssertionError('QMP: %s' % message)
            self.pump()
        raise AssertionError('QMP sin respuesta esperada')

    def send(self, message):
        self.socket.sendall((json.dumps(message) + '\n').encode())

    def key(self, qcode, hold=20):
        self.send({'execute': 'send-key',
                   'arguments': {'keys': [{'type': 'qcode', 'data': qcode}],
                                 'hold-time': hold}})
        self.wait_message(lambda message: 'return' in message, 5)
        time.sleep(0.06)

    def ascii(self, text):
        qcodes = {' ': 'spc', '\n': 'ret', '.': 'dot', '-': 'minus',
                  '/': 'slash', ',': 'comma', '=': 'equal'}
        for character in text:
            self.key(qcodes.get(character, character))

    def shutdown_event(self, seconds=30):
        return self.wait_message(lambda message: message.get('event') == 'SHUTDOWN', seconds)

    def close(self):
        if self.socket:
            try:
                self.socket.close()
            except OSError:
                pass
            self.socket = None
        self.process.terminate()
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()


def scenario_shell(workdir):
    print('--- APAGAR desde la consola serie ---')
    machine = Machine(workdir, 'shell')
    try:
        machine.until('OS> ')
        machine.connect()
        machine.ascii('apagar\n')
        machine.until('POWER: APAGADO SOLICITADO (COMANDO APAGAR)')
        machine.until('POWER: S5 ENVIADO')
        machine.shutdown_event()
        print('OK  evento QMP SHUTDOWN tras APAGAR')
    finally:
        machine.close()


def main():
    if not ISO.exists():
        print('Falta %s: ejecuta make iso.' % ISO)
        return 2
    accel, _ = qemu_config.accelerator()
    print('Acelerador: %s' % accel)
    with tempfile.TemporaryDirectory(prefix='apagar-test-') as workdir:
        scenario_shell(workdir)
    print('PASSED: APAGAR apaga el equipo por ACPI S5')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except AssertionError as error:
        print('FALLO: %s' % error)
        sys.exit(1)

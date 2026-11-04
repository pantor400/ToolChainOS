#!/usr/bin/env python3
"""Verifica la personalidad Linux: BusyBox de Alpine ejecutandose en ToolchainOS.

El binario no se enlaza con el kernel: viaja como modulo Limine en una ISO
aparte y el kernel atiende sus llamadas al sistema. La prueba comprueba tres
cosas, en orden:

  1. La ISO contiene el binario y lo declara como modulo.
  2. El kernel programo SYSCALL (EFER.SCE, STAR y LSTAR) al arrancar.
  3. Un applet real devuelve su salida y termina con codigo 0.
"""
import subprocess
import sys
from pathlib import Path

import qemu_config
import qemu_test

ROOT = Path(__file__).resolve().parents[1]
ISO = qemu_config.ISO_BIN
BINARY = ROOT / 'vendor' / 'linux-bin' / 'busybox.static'


def check_iso():
    if not BINARY.is_file():
        raise SystemExit('Falta %s; ejecuta "make linux-bin"' % BINARY.name)
    if not ISO.is_file():
        raise SystemExit('Falta %s; ejecuta "make build/toolchain-os-busybox.iso"'
                         % ISO.name)
    listing = subprocess.run(['xorriso', '-indev', str(ISO), '-find', '/boot/bin',
                              '-exec', 'lsdl', '--'],
                             capture_output=True, text=True)
    if 'busybox.static' not in listing.stdout:
        raise SystemExit('La ISO no contiene /boot/bin/busybox.static')
    extracted = ROOT / 'build' / 'linux-bin' / 'iso-limine.conf'
    extracted.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['xorriso', '-osirrox', 'on', '-indev', str(ISO), '-extract',
                    '/boot/limine/limine.conf', str(extracted)],
                   capture_output=True, text=True)
    text = extracted.read_text(errors='replace') if extracted.is_file() else ''
    if 'module_path: boot():/boot/bin/busybox.static' not in text:
        raise SystemExit('limine.conf no declara el modulo de BusyBox')


def main():
    check_iso()
    boot = ''
    with qemu_test.Guest('linux-bin', iso=str(ISO)) as guest:
        boot = guest.buffer.decode(errors='replace')
        if 'LINUX ABI: SYSCALL ACTIVA' not in boot:
            raise SystemExit('El kernel no programo SYSCALL:\n' + boot[-2000:])
        guest.send('linux info', expected=['SYSCALL DISPONIBLE', 'MAPAS MAX=32'])
        guest.send('linux echo hola desde busybox',
                   expected=['hola desde busybox', 'LINUX: FIN CODIGO=0'])
        guest.send('linux uname -m', expected=['x86_64', 'LINUX: FIN CODIGO=0'])
        guest.send('linux ls /', expected=['etc', 'LINUX: FIN CODIGO=0'])
        guest.send('linux cat /etc/hostname', expected=['LINUX: FIN CODIGO=0'])
        # Delegacion: con el binario presente, las ordenes del shell las ejecuta
        # el applet real y sin el preambulo de estado de la personalidad Linux.
        for line, expected in (('ls /', ['etc']),
                               ('open /etc/hostname', ['toolchainos']),
                               ('grep toolchainos /etc/hostname', ['toolchainos'])):
            text = guest.send(line, expected=expected)
            if 'LINUX: EJECUTANDO' in text or 'LINUX: FIN CODIGO=' in text:
                raise AssertionError('%r delego con ruido de estado:\n%s' % (line, text))
    print('PASSED: BusyBox de Alpine ejecutado sobre la personalidad Linux')
    print('  captura: artifacts/linux-bin.serial.txt')


if __name__ == '__main__':
    try:
        main()
    except (AssertionError, TimeoutError) as exc:
        print('FAILED: %s' % exc)
        sys.exit(1)

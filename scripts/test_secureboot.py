#!/usr/bin/env python3
"""Comprueba la politica de Secure Boot con OVMF.

Tres comprobaciones, sin tocar el firmware del anfitrion:

  1. CONTROL: la ISO normal arranca con OVMF sin Secure Boot (KERNEL_READY).
  2. RECHAZO: con OVMF + Secure Boot la misma ISO no arranca, porque el
     cargador de Limine no esta firmado. Es el comportamiento esperado: el
     proyecto no desactiva ni salta Secure Boot.
  3. FIRMA: si existe build/toolchain-os-secureboot.iso, se comprueba con
     sbverify que el cargador lleva firma de las claves propias.

Arrancar la ISO firmada bajo Secure Boot exige inscribir db.crt en el
firmware (MOK o KeyTool). Con --vars <archivo> se puede pasar un OVMF_VARS
que ya tenga la clave y entonces se exige KERNEL_READY:

    python3 scripts/test_secureboot.py --vars build/secureboot/OVMF_VARS.fd

Uso: python3 scripts/test_secureboot.py [--firmado] [--vars ARCHIVO]
"""
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import qemu_config

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / 'build' / 'toolchain-os.iso'
ISO_FIRMADA = ROOT / 'build' / 'toolchain-os-secureboot.iso'
CRT = ROOT / 'build' / 'secureboot' / 'db.crt'

CODE_PLAIN = Path('/usr/share/OVMF/OVMF_CODE.fd')
VARS_PLAIN = Path('/usr/share/OVMF/OVMF_VARS.fd')
CODE_SB = Path('/usr/share/OVMF/OVMF_CODE.secboot.fd')
VARS_SB = Path('/usr/share/OVMF/OVMF_VARS.secboot.fd')


def wait_for(serial, needle, timeout):
    """True si el marcador aparece; False si se agota el tiempo."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        text = serial.read_text(errors='replace') if serial.exists() else ''
        if needle in text:
            return time.monotonic()
        time.sleep(0.2)
    return None


def boot(workdir, tag, iso, code, vars_image):
    serial = Path(workdir) / (tag + '.log')
    command = (qemu_config.base(iso=iso) + qemu_config.display('none')
               + qemu_config.serial_file(serial)
               + ['-drive', 'if=pflash,format=raw,readonly=on,file=%s' % code,
                  '-drive', 'if=pflash,format=raw,snapshot=on,file=%s' % vars_image])
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    return process, serial


def stop(process):
    process.kill()
    process.wait()


def caso_control(workdir):
    print('CONTROL: ISO normal con OVMF sin Secure Boot')
    process, serial = boot(workdir, 'control', ISO, CODE_PLAIN, VARS_PLAIN)
    try:
        start = time.monotonic()
        found = wait_for(serial, 'KERNEL_READY', 90)
        elapsed = time.monotonic() - start
        if found is None:
            print('FALLO: la ISO no arranca ni con OVMF normal')
            print((serial.read_text(errors='replace') if serial.exists() else '')[-1500:])
            return None
        print('OK  KERNEL_READY en %.1f s (referencia de arranque)' % elapsed)
        return elapsed
    finally:
        stop(process)


def caso_rechazo(workdir, reference):
    print('RECHAZO: ISO sin firmar con OVMF y Secure Boot activo')
    if not CODE_SB.exists() or not VARS_SB.exists():
        print('OMITIDO: falta %s o %s' % (CODE_SB, VARS_SB))
        return True
    process, serial = boot(workdir, 'secboot', ISO, CODE_SB, VARS_SB)
    try:
        window = max(20.0, (reference or 20.0) * 1.5)
        found = wait_for(serial, 'KERNEL_READY', window)
        if found is not None:
            print('FALLO: Secure Boot acepto un cargador sin firmar')
            return False
        print('OK  el firmware rechazo el cargador sin firmar '
              '(sin KERNEL_READY en %.0f s)' % window)
        return True
    finally:
        stop(process)


def estado_firma():
    print('FIRMA: cargador con claves propias')
    if not ISO_FIRMADA.exists():
        print('OMITIDO: falta %s (make secureboot-claves && make secureboot-iso)'
              % ISO_FIRMADA.name)
        return True
    if not CRT.exists():
        print('OMITIDO: falta el certificado %s' % CRT)
        return True
    if not shutil.which('sbverify'):
        print('OMITIDO: falta sbverify (paquete sbsigntools); no se puede '
              'comprobar la firma en este equipo.')
        return True
    loader = ROOT / 'build' / 'secureboot' / 'BOOTX64.EFI'
    if not loader.exists():
        print('OMITIDO: falta %s' % loader)
        return True
    code = subprocess.call(['sbverify', '--cert', str(CRT), str(loader)],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if code != 0:
        print('FALLO: sbverify no valida la firma del cargador')
        return False
    print('OK  el cargador firmado valida con %s' % CRT.name)
    return True


def caso_firmado(workdir, vars_image):
    print('ARRANQUE FIRMADO: ISO con Secure Boot y claves inscritas')
    process, serial = boot(workdir, 'firmado', ISO_FIRMADA, CODE_SB, vars_image)
    try:
        found = wait_for(serial, 'KERNEL_READY', 90)
        if found is None:
            print('FALLO: la ISO firmada no arranca con las claves de %s' % vars_image)
            print((serial.read_text(errors='replace') if serial.exists() else '')[-1500:])
            return False
        print('OK  KERNEL_READY con Secure Boot y la clave propia inscrita')
        return True
    finally:
        stop(process)


def main():
    if not ISO.exists():
        print('Falta %s: ejecuta make iso.' % ISO)
        return 2
    if not CODE_PLAIN.exists():
        print('Falta %s: instala OVMF (edk2-ovmf).' % CODE_PLAIN)
        # 78 = BLOCKED en scripts/test_all.py: falta un componente opcional.
        return 78
    firmado = '--firmado' in sys.argv
    vars_image = VARS_SB
    if '--vars' in sys.argv:
        index = sys.argv.index('--vars')
        if index + 1 >= len(sys.argv):
            print('--vars necesita la ruta de un OVMF_VARS con la clave inscrita.')
            return 2
        vars_image = Path(sys.argv[index + 1])
        if not vars_image.is_file():
            print('No existe %s' % vars_image)
            return 2
        firmado = True
    with tempfile.TemporaryDirectory(prefix='sb-test-') as workdir:
        reference = caso_control(workdir)
        if reference is None:
            return 1
        if not estado_firma():
            return 1
        if not caso_rechazo(workdir, reference):
            return 1
        if firmado:
            if not ISO_FIRMADA.exists():
                print('Falta %s: ejecuta make secureboot-iso.' % ISO_FIRMADA)
                return 1
            if not caso_firmado(workdir, vars_image):
                return 1
        else:
            print('AVISO: para probar la ISO firmada, inscribe db.crt y repite con')
            print('       --firmado (o --vars <OVMF_VARS con la clave>).')
            print('       Guia: python3 scripts/secure_boot.py guia')
    print('SECURE BOOT: COMPROBACIONES SUPERADAS')
    return 0


if __name__ == '__main__':
    sys.exit(main())

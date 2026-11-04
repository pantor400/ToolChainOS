#!/usr/bin/env python3
"""Pruebas de USB con QEMU: almacenamiento masivo, FAT y protecciones.

Arranca sin ventana con -device qemu-xhci y un pendrive crudo, escribe en la
shell por QMP y comprueba los marcadores del kernel por COM1. Cubre:
  - deteccion de la controladora y del medio (BOT/SCSI);
  - montaje FAT32 y lectura de archivos (listar, ver, importar);
  - escritura de un sector y su persistencia tras reiniciar;
  - aviso de medio cifrado LUKS sin intentar descifrarlo;
  - medio de solo lectura (MODE SENSE) sin escritura.

Uso: python3 scripts/test_usb.py
"""
import json
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import qemu_config

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / 'build' / 'toolchain-os.iso'
OVMF_CODE = Path('/usr/share/OVMF/OVMF_CODE.fd')
OVMF_VARS = Path('/usr/share/OVMF/OVMF_VARS.fd')


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
        time.sleep(0.04)

    def ascii(self, text):
        qcodes = {' ': 'spc', '\n': 'ret', '.': 'dot', '-': 'minus',
                  '/': 'slash', ',': 'comma', '=': 'equal'}
        for character in text:
            if 'A' <= character <= 'Z':
                # QMP usa qcodes en minusculas: la mayuscula es shift + tecla.
                self.call('send-key', {'keys': [
                    {'type': 'qcode', 'data': 'shift'},
                    {'type': 'qcode', 'data': character.lower()}], 'hold-time': 20})
                time.sleep(0.04)
                continue
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
    print(text[-1500:])
    raise SystemExit(1)


def esperar_arranque(serial, timeout=60):
    """Descarta la salida anterior: cada arranque empieza con KERNEL_READY."""
    wait_for(serial, 'KERNEL_READY', timeout, 'arranque del kernel')
    return serial.read_text(errors='replace')


def nueva_salida(serial, before):
    text = serial.read_text(errors='replace')
    return text[len(before):] if text.startswith(before) else text


def crear_imagen_fat(path):
    """Pendrive con MBR, una particion FAT32 y dos archivos de prueba."""
    if not all(shutil.which(tool) for tool in ('sfdisk', 'mkfs.vfat', 'mcopy')):
        print('OMITIDO imagen FAT: faltan sfdisk, mkfs.vfat o mcopy (mtools)')
        return False
    with open(path, 'wb') as image:
        image.truncate(32 * 1024 * 1024)
    subprocess.run(['sfdisk', str(path)], check=True,
                   input=b'label: dos\nstart=2048, size=61440, type=c\n',
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run(['mkfs.vfat', '-F', '32', '-n', 'PRUEBAUSB',
                    '--offset', '2048', str(path)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = str(path) + '@@' + str(2048 * 512)
    for name, content in (('HOLA.TXT', 'Hola desde el USB de prueba.\n'),
                          ('NOTAS.TXT', 'notas de la entrega\n')):
        local = Path(str(path) + '.' + name)
        local.write_text(content)
        subprocess.run(['mcopy', '-i', base, str(local), '::' + name], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        local.unlink()
    return True


def crear_imagen_cifrada(path):
    """Cabecera LUKS real (misma firma que crea cryptsetup) y resto a cero."""
    with open(path, 'wb') as image:
        image.write(b'LUKS\xba\xbe\x00\x02\x00\x00\x00\x00\x00\x00\x40\x00')
        image.truncate(8 * 1024 * 1024)
    return True


def arrancar(imagen, workdir, etiqueta, readonly=False, controladora=True):
    serial = Path(workdir) / (etiqueta + '.log')
    sock = Path(workdir) / (etiqueta + '.sock')
    command = (qemu_config.base(iso=ISO) + qemu_config.display('none')
               + qemu_config.serial_file(serial) + qemu_config.qmp(sock))
    if controladora:
        command += qemu_config.usb_controller()
    if imagen is not None:
        command += qemu_config.usb_storage(imagen, readonly=readonly)
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    return process, serial, sock


def detener(process):
    process.kill()
    process.wait()


def shell(client, orden):
    """Envia una orden a la shell por la conexion QMP ya abierta."""
    client.ascii(orden + '\n')


def caso_fat(workdir):
    path = Path(workdir) / 'fat.img'
    if not crear_imagen_fat(path):
        return True
    process, serial, sock = arrancar(path, workdir, 'fat')
    client = None
    try:
        before = esperar_arranque(serial)
        texto = wait_for(serial, 'USB: SISTEMA FAT32', 30, 'FAT32 montado en el pendrive')
        if 'ETIQUETA="PRUEBAUSB"' not in texto:
            print('FALLO la etiqueta de la particion no se leyo')
            return False
        print('OK  etiqueta de volumen PRUEBAUSB')
        wait_for(serial, 'USB: PUERTO 1 CONECTADO', 10, 'puerto USB conectado')
        client = QMP(sock)
        shell(client, 'usb listar')
        wait_for(serial, 'USB: HOLA.TXT', 10, 'listado FAT con HOLA.TXT')
        shell(client, 'usb ver hola.txt')
        wait_for(serial, 'Hola desde el USB de prueba.', 10, 'contenido leido del USB')
        shell(client, 'usb importar notas.txt')
        wait_for(serial, 'USB: IMPORTADO /NOTAS.TXT', 10, 'importado al FS en RAM')
        shell(client, 'open notas.txt')
        wait_for(serial, 'notas de la entrega', 10, 'archivo importado legible')
        # Escritura del ultimo sector y lectura inmediata.
        total = qemu_config_usb_sectores(texto)
        if total:
            sector = total - 1
            shell(client, 'usb escribir %d FINALUSB' % sector)
            wait_for(serial, 'USB: SECTOR %d ESCRITO' % sector, 15, 'escritura SCSI de un sector')
            shell(client, 'usb leer %d' % sector)
            wait_for(serial, '46 49 4e 41 4c 55 53 42', 15, 'lectura del sector escrito')
        return True
    finally:
        if client is not None:
            client.close()
        detener(process)


def qemu_config_usb_sectores(texto):
    """SECTORES=n del arranque (ALMACENAMIENTO) o de USB INFO."""
    import re
    match = re.search(r'USB: (?:ALMACENAMIENTO )?SECTORES=(\d+)', texto)
    return int(match.group(1)) if match else 0


def caso_persistencia(workdir, total):
    """Segundo arranque sobre la misma imagen: el sector escrito sigue ahi."""
    path = Path(workdir) / 'fat.img'
    if not total:
        return True
    process, serial, sock = arrancar(path, workdir, 'persist')
    client = None
    try:
        esperar_arranque(serial)
        client = QMP(sock)
        shell(client, 'usb leer %d' % (total - 1))
        wait_for(serial, '46 49 4e 41 4c 55 53 42', 15,
                 'el sector escrito sobrevive al reinicio')
        return True
    finally:
        if client is not None:
            client.close()
        detener(process)


def caso_cifrado(workdir):
    path = Path(workdir) / 'luks.img'
    crear_imagen_cifrada(path)
    process, serial, sock = arrancar(path, workdir, 'luks')
    client = None
    try:
        esperar_arranque(serial)
        client = QMP(sock)
        shell(client, 'usb info')
        wait_for(serial, 'USB: MEDIO CIFRADO LUKS', 15,
                 'aviso de medio cifrado sin descifrar')
        if 'NO SE MONTA NI SE INTENTA DESCIFRAR' not in serial.read_text(errors='replace'):
            print('FALLO falta el aviso de no descifrar')
            return False
        print('OK  el medio cifrado no se monta ni se descifra')
        return True
    finally:
        if client is not None:
            client.close()
        detener(process)


def caso_solo_lectura(workdir):
    path = Path(workdir) / 'ro.img'
    if not crear_imagen_fat(path):
        return True
    process, serial, sock = arrancar(path, workdir, 'ro', readonly=True)
    client = None
    try:
        esperar_arranque(serial)
        wait_for(serial, 'USB: ESCRITURA BLOQUEADA', 20, 'medio de solo lectura detectado')
        client = QMP(sock)
        shell(client, 'usb escribir 10 PRUEBA')
        wait_for(serial, 'PROTEGIDO CONTRA ESCRITURA', 10, 'escritura rechazada sin tocar el medio')
        return True
    finally:
        if client is not None:
            client.close()
        detener(process)


def caso_sin_medio(workdir):
    process, serial, sock = arrancar(None, workdir, 'vacio')
    try:
        esperar_arranque(serial)
        wait_for(serial, 'USB: SIN DISPOSITIVOS CONECTADOS', 15, 'controladora sin dispositivos')
        return True
    finally:
        detener(process)


def arrancar_desde_pendrive(workdir, tag, uefi=False):
    """La ISO hibrida escrita en un pendrive: sin CD, solo usb-storage."""
    serial = Path(workdir) / (tag + '.log')
    sock = Path(workdir) / (tag + '.sock')
    command = (qemu_config.base(iso=None) + qemu_config.display('none')
               + qemu_config.serial_file(serial) + qemu_config.qmp(sock))
    command[command.index('-boot') + 1] = 'c'
    command += qemu_config.usb_controller() + qemu_config.usb_storage(ISO, readonly=True)
    if uefi:
        command += ['-drive', 'if=pflash,format=raw,readonly=on,file=%s' % OVMF_CODE,
                    '-drive', 'if=pflash,format=raw,snapshot=on,file=%s' % OVMF_VARS]
    process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    return process, serial, sock


def caso_arranque_usb(workdir, uefi=False):
    """Arranca la ISO copiada en un pendrive, sin unidad de CD."""
    etiqueta = 'Arranque UEFI desde pendrive' if uefi else 'Arranque BIOS desde pendrive'
    print('--- %s ---' % etiqueta)
    if uefi and not (OVMF_CODE.exists() and OVMF_VARS.exists()):
        print('OMITIDO: falta OVMF para el arranque UEFI')
        return True
    process, serial, sock = arrancar_desde_pendrive(workdir, 'usbboot-uefi' if uefi else 'usbboot-bios', uefi)
    client = None
    try:
        texto = esperar_arranque(serial, 120)
        if 'USB: XHCI 0:' not in texto:
            print('FALLO: el kernel no inicializo la controladora del pendrive')
            print(texto[-1200:])
            return False
        if 'USB: PUERTO 1 HABILITADO' not in texto:
            print('FALLO: el pendrive de arranque no se enumero')
            print(texto[-1200:])
            return False
        if 'USB: SISTEMA FAT' not in texto:
            print('FALLO: la particion EFI del ISO no se monto')
            print(texto[-1200:])
            return False
        total = qemu_config_usb_sectores(texto)
        esperado = ISO.stat().st_size // 512
        if total != esperado:
            print('FALLO: el medio expone %d sectores y la ISO mide %d' % (total, esperado))
            return False
        print('OK  %s: kernel arrancado y medio de %d sectores montado' % (etiqueta, total))
        client = QMP(sock)
        shell(client, 'usb listar')
        wait_for(serial, 'USB: EFI <DIR>', 10, 'la particion EFI del pendrive se lista')
        return True
    finally:
        if client is not None:
            client.close()
        detener(process)


def main():
    if not ISO.exists():
        print('Falta %s: ejecuta make iso.' % ISO)
        return 2
    with tempfile.TemporaryDirectory(prefix='usb-test-') as workdir:
        if not caso_fat(workdir):
            return 1
        total = 0
        log = (Path(workdir) / 'fat.log').read_text(errors='replace')
        total = qemu_config_usb_sectores(log)
        if not caso_persistencia(workdir, total):
            return 1
        if not caso_cifrado(workdir):
            return 1
        if not caso_solo_lectura(workdir):
            return 1
        if not caso_sin_medio(workdir):
            return 1
        if not caso_arranque_usb(workdir):
            return 1
        if not caso_arranque_usb(workdir, uefi=True):
            return 1
    print('USB: TODAS LAS COMPROBACIONES SUPERADAS')
    return 0


if __name__ == '__main__':
    sys.exit(main())

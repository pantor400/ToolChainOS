#!/usr/bin/env python3
"""Escribe la ISO hibrida de ToolchainOS en un pendrive (BIOS y UEFI).

La ISO que genera "make iso" es hibrida: lleva El Torito para BIOS y una
particion EFI para UEFI. Copiada tal cual con dd arranca en los dos modos, de
modo que el mismo archivo sirve como instalador de emergencia en un pendrive.

Este script es deliberadamente conservador:
  - solo enumera unidades que el firmware marca como extraibles;
  - rechaza el disco que contiene el sistema de archivos en marcha;
  - rechaza discos con particiones montadas;
  - no escribe nada hasta teclear la confirmacion exacta.

    make usb-lista                                    (solo enumera)
    sudo python3 scripts/escribir_usb.py escribir /dev/sdX
    sudo python3 scripts/escribir_usb.py verificar /dev/sdX

El pendrive se borra por completo: guarda antes lo que necesites. Para que
Secure Boot acepte el arranque, escribe build/toolchain-os-secureboot.iso
(lo genera "make secureboot-iso").
"""
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISO = ROOT / 'build' / 'toolchain-os.iso'
ISO_FIRMADA = ROOT / 'build' / 'toolchain-os-secureboot.iso'


def lsblk_json():
    try:
        result = subprocess.run(
            ['lsblk', '-J', '-b', '-o',
             'NAME,PATH,SIZE,TYPE,RM,RO,MODEL,MOUNTPOINTS,FSTYPE'],
            capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        print('No se pudo ejecutar lsblk: %s' % error)
        return None
    return json.loads(result.stdout)


def size_text(value):
    number = float(value or 0)
    for unit in ('B', 'KiB', 'MiB', 'GiB'):
        if number < 1024:
            return '%.1f %s' % (number, unit)
        number /= 1024
    return '%.1f TiB' % number


def mounted_devices():
    """Dispositivos reales con algo montado, segun /proc/mounts."""
    devices = set()
    try:
        lines = Path('/proc/mounts').read_text().splitlines()
    except OSError:
        return devices
    for line in lines:
        fields = line.split()
        if len(fields) >= 2 and fields[0].startswith('/dev/'):
            devices.add(os.path.realpath(fields[0]))
    return devices


def disk_devices(tree):
    if not tree:
        return []
    return [disk for disk in tree.get('blockdevices', []) if disk.get('type') == 'disk']


def parts_of(disk):
    """El disco y todas sus particiones, para comparar con lo montado."""
    parts = [disk.get('path')]
    stack = list(disk.get('children') or [])
    while stack:
        node = stack.pop()
        parts.append(node.get('path'))
        stack.extend(node.get('children') or [])
    return [part for part in parts if part]


def is_system_disk(disk, mounted):
    return any(part in mounted for part in parts_of(disk))


def find_disk(tree, path):
    wanted = os.path.realpath(path)
    for disk in disk_devices(tree):
        if os.path.realpath(disk.get('path', '')) == wanted:
            return disk
    return None


def listar():
    tree = lsblk_json()
    if tree is None:
        return 2
    mounted = mounted_devices()
    disks = disk_devices(tree)
    extraibles = [disk for disk in disks if disk.get('rm')]
    print('UNIDADES USB / EXTRAIBLES')
    print('=========================')
    if not extraibles:
        print('No hay unidades extraibles conectadas.')
        print('Si acabas de enchufar el pendrive, revisa "lsblk -o NAME,SIZE,RM".')
        print('Algunos firmware marcan los pendrives como discos fijos (RM=0).')
        return 1
    for disk in extraibles:
        path = disk.get('path')
        flags = []
        if disk.get('ro'):
            flags.append('SOLO LECTURA')
        if is_system_disk(disk, mounted):
            flags.append('SISTEMA EN MARCHA: NO USAR')
        print('\n%s  %s  %s%s' % (
            path, size_text(disk.get('size')),
            (disk.get('model') or '').strip(),
            ('  [' + ', '.join(flags) + ']') if flags else ''))
        for child in disk.get('children') or []:
            points = ','.join(child.get('mountpoints') or []) or '-'
            print('  %s  %s  %-6s %s  montado: %s' % (
                child.get('path'), size_text(child.get('size')),
                child.get('fstype') or '-',
                size_text(child.get('size')), points))
    print('\nEscribe con: sudo python3 scripts/escribir_usb.py escribir %s'
          % extraibles[0].get('path'))
    return 0


def confirmar(device, imagen):
    print('\nATENCION: se va a borrar TODO el contenido de %s' % device)
    print('Imagen: %s (%s)' % (imagen, size_text(imagen.stat().st_size)))
    print('El disco quedara con la ISO de ToolchainOS para arrancar en BIOS y UEFI.')
    phrase = 'ESCRIBIR ' + device
    print('\nPara continuar escribe exactamente:  %s' % phrase)
    try:
        answer = input('> ').strip()
    except EOFError:
        return False
    if answer != phrase:
        print('Confirmacion incorrecta: no se ha escrito nada.')
        return False
    return True


def escribir(device, imagen, forzar):
    imagen = Path(imagen)
    if not imagen.is_file():
        print('Falta la imagen %s: ejecuta "make iso".' % imagen)
        return 2
    if os.geteuid() != 0:
        print('Escribir en un dispositivo exige root: usa sudo.')
        return 2
    tree = lsblk_json()
    if tree is None:
        return 2
    disk = find_disk(tree, device)
    if disk is None:
        print('%s no es un disco completo. Indica /dev/sdX, /dev/nvme0n1, '
              'no una particion.' % device)
        return 2
    mounted = mounted_devices()
    if is_system_disk(disk, mounted):
        print('RECHAZADO: %s contiene el sistema en marcha o particiones '
              'montadas. Desmonta y elige otro disco.' % device)
        return 2
    if disk.get('ro'):
        print('RECHAZADO: el firmware presenta %s como solo lectura.' % device)
        return 2
    if not disk.get('rm') and not forzar:
        print('RECHAZADO: %s no esta marcado como extraible (RM=0).' % device)
        print('Si es un pendrive y estas seguro, repite con --forzar.')
        return 2
    if imagen.stat().st_size > int(disk.get('size') or 0):
        print('RECHAZADO: la imagen ocupa mas que el disco.')
        return 2
    if not confirmar(disk.get('path'), imagen):
        return 1
    print('Escribiendo %s en %s ...' % (imagen.name, device))
    try:
        code = subprocess.call(['dd', 'if=%s' % imagen, 'of=%s' % device,
                                'bs=4M', 'conv=fsync', 'status=progress'])
    except OSError as error:
        print('No se pudo ejecutar dd: %s' % error)
        return 2
    if code != 0:
        print('dd termino con error %d: el pendrive puede haber quedado a medias.' % code)
        return 1
    subprocess.call(['sync'])
    print('Listo. Comprueba con: python3 scripts/escribir_usb.py verificar %s' % device)
    print('Arranque UEFI con Secure Boot: escribe build/toolchain-os-secureboot.iso')
    print('  (make secureboot-claves && make secureboot-iso) e inscribe db.crt.')
    return 0


def verificar(device):
    path = Path(device)
    try:
        with path.open('rb') as disk:
            head = disk.read(1024 * 1024)
    except OSError as error:
        print('No se pudo leer %s: %s' % (device, error))
        return 2
    if len(head) < 512:
        print('LECTURA CORTA: el dispositivo no devuelve un sector completo.')
        return 1
    if head[510:512] != b'\x55\xaa':
        print('SIN FIRMA DE ARRANQUE: el sector 0 no tiene 0x55AA.')
        print('La ISO no se ha escrito o el disco apunta a otro sitio.')
        return 1
    print('OK  firma de arranque 0x55AA en el sector 0')
    if b'limine' in head.lower():
        print('OK  cargador Limine presente en el primer MiB')
    else:
        print('AVISO: no se encontro el texto de Limine en el primer MiB.')
    return 0


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    action = sys.argv[1].lower()
    if action == 'listar':
        return listar()
    if action in ('escribir', 'grabar'):
        if len(sys.argv) < 3:
            print('Uso: escribir <dispositivo> [imagen] [--forzar]')
            return 2
        rest = sys.argv[3:]
        forzar = '--forzar' in rest
        imagen = next((arg for arg in rest if not arg.startswith('--')), ISO)
        if not Path(imagen).is_absolute():
            imagen = ROOT / imagen
        return escribir(sys.argv[2], imagen, forzar)
    if action == 'verificar':
        if len(sys.argv) < 3:
            print('Uso: verificar <dispositivo>')
            return 2
        return verificar(sys.argv[2])
    print('Subcomando desconocido: %s' % action)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())

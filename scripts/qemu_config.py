"""Configuracion unica de QEMU para la demo y las suites automaticas.

Un solo sitio decide aceleracion, CPU, memoria de video, red y cache de disco,
de modo que la demo interactiva y las pruebas usan la misma maquina virtual.

Aceleracion: KVM cuando /dev/kvm es accesible y el usuario puede abrirlo; en
caso contrario TCG. Se puede forzar con QEMU_ACCEL=kvm o QEMU_ACCEL=tcg, por
ejemplo "QEMU_ACCEL=tcg make run" para medir el peor caso.
"""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISO = ROOT / 'build' / 'toolchain-os.iso'


def accelerator():
    """Devuelve (acelerador, cpu) segun lo que permita el anfitrion."""
    forced = os.environ.get('QEMU_ACCEL', '').strip().lower()
    usable = Path('/dev/kvm').exists() and os.access('/dev/kvm', os.R_OK | os.W_OK)
    if forced in ('kvm', 'tcg'):
        accel = forced
    else:
        accel = 'kvm' if usable else 'tcg'
    return accel, ('host' if accel == 'kvm' else 'max')


def base(machine='q35', memory='256M', smp='2', iso=None, vgamem='32'):
    """Argumentos comunes: aceleracion, CPU, video, memoria y arranque."""
    accel, cpu = accelerator()
    args = ['qemu-system-x86_64', '-machine', machine, '-accel', accel, '-cpu', cpu,
            '-m', memory, '-smp', smp,
            '-vga', 'std', '-global', 'VGA.vgamem_mb=' + vgamem,
            '-boot', 'd', '-no-reboot', '-no-shutdown',
            '-rtc', 'base=localtime,clock=host']
    if iso is not None:
        args += ['-cdrom', str(iso)]
    return args


def display(mode='none'):
    """'none' para las pruebas; para la demo se pasa la cadena de -display."""
    return ['-display', mode]


def net():
    return ['-netdev', 'user,id=net0', '-device', 'rtl8139,netdev=net0']


def serial_stdio():
    return ['-serial', 'stdio', '-monitor', 'none']


def serial_file(path):
    return ['-serial', 'file:%s' % path, '-monitor', 'none']


def qmp(path):
    return ['-qmp', 'unix:%s,server=on,wait=off' % path]


def disk(path, bus='ide', index=0):
    """Disco de prueba en cache de escritura: sin fsync por sector."""
    value = str(path).replace(',', ',,')
    flags = 'format=raw,if=%s,index=%d,cache=writeback,aio=threads' % (bus, index)
    return ['-drive', 'file=%s,%s' % (value, flags)]

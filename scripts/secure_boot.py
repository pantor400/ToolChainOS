#!/usr/bin/env python3
"""Secure Boot con claves propias para ToolchainOS.

El cargador UEFI de Limine (EFI/BOOT/BOOTX64.EFI) se firma con una clave
propia y se reconstruye la ISO hibrida. Secure Boot solo debe aceptar
cargadores firmados: no se desactiva ni se salta la comprobacion.

Subcomandos:
    estado      Que herramientas y artefactos hay en este equipo.
    claves      Genera la clave y el certificado autofirmados (openssl).
    firmar      Firma el cargador con sbsign (requiere sbsigntools).
    verificar   Comprueba la firma con sbverify.
    guia        Pasos para inscribir la clave (MOK) y probar con OVMF.

La clave privada se guarda en build/secureboot/ y no se distribuye.
"""
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SB_DIR = ROOT / 'build' / 'secureboot'
KEY = SB_DIR / 'db.key'
CRT = SB_DIR / 'db.crt'
SIGNED = SB_DIR / 'BOOTX64.EFI'
LOADER = ROOT / 'vendor' / 'limine' / 'BOOTX64.EFI'
SIGNED_ISO = ROOT / 'build' / 'toolchain-os-secureboot.iso'


def tool(name):
    return shutil.which(name)


def estado():
    print('openssl:     %s' % (tool('openssl') or 'NO ENCONTRADO'))
    print('sbsign:      %s' % (tool('sbsign') or 'NO ENCONTRADO (dnf install sbsigntools)'))
    print('sbverify:    %s' % (tool('sbverify') or 'NO ENCONTRADO'))
    print('mokutil:     %s' % (tool('mokutil') or 'NO ENCONTRADO'))
    print('clave:       %s' % ('presente' if KEY.exists() else 'ausente'))
    print('certificado: %s' % ('presente' if CRT.exists() else 'ausente'))
    print('cargador firmado: %s' % ('presente' if SIGNED.exists() else 'ausente'))
    print('ISO firmada: %s' % ('presente' if SIGNED_ISO.exists() else 'ausente'))


def claves():
    if not tool('openssl'):
        print('Falta openssl: no se pueden generar las claves.')
        return 2
    SB_DIR.mkdir(parents=True, exist_ok=True)
    if KEY.exists() and CRT.exists():
        print('Las claves ya existen en %s' % SB_DIR)
        return 0
    comando = ['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
               '-keyout', str(KEY), '-out', str(CRT), '-days', '3650',
               '-subj', '/CN=ToolchainOS Secure Boot/']
    return subprocess.call(comando)


def firmar():
    if not tool('sbsign'):
        print('Falta sbsign. Instala sbsigntools y repite:')
        print('    sudo dnf install sbsigntools      # Fedora')
        print('    sudo apt install sbsigntool       # Debian/Ubuntu')
        print('Sin firma no hay cargador que Secure Boot acepte.')
        return 2
    if not KEY.exists() or not CRT.exists():
        if claves() != 0:
            return 2
    SB_DIR.mkdir(parents=True, exist_ok=True)
    comando = ['sbsign', '--key', str(KEY), '--cert', str(CRT),
               '--output', str(SIGNED), str(LOADER)]
    code = subprocess.call(comando)
    if code == 0:
        print('Cargador firmado: %s' % SIGNED)
        print('Construye la imagen con: make secureboot-iso')
    return code


def verificar():
    if not SIGNED.exists():
        print('No hay cargador firmado: ejecuta antes "firmar".')
        return 1
    if not tool('sbverify'):
        print('Falta sbverify (paquete sbsigntools).')
        return 2
    return subprocess.call(['sbverify', '--cert', str(CRT), str(SIGNED)])


def guia():
    print('Secure Boot con claves propias')
    print('==============================')
    print('1. make secureboot-claves   genera par de claves y certificado.')
    print('2. make secureboot-iso      firma el cargador y arma la ISO.')
    print('3. Inscribe el certificado en el firmware:')
    print('     sudo mokutil --import %s' % CRT)
    print('   (reinicia y acepta la clave en el menu MOK; en OVMF se puede')
    print('   inscribir con KeyTool.efi desde la shell UEFI).')
    print('4. Arranca el pendrive con Secure Boot activo:')
    print('     qemu-system-x86_64 -drive if=pflash,format=raw,readonly=on,'
          'file=/usr/share/OVMF/OVMF_CODE.secboot.fd \\')
    print('       -drive if=pflash,format=raw,snapshot=on,'
          'file=/usr/share/OVMF/OVMF_VARS.secboot.fd \\')
    print('       -cdrom build/toolchain-os-secureboot.iso')
    print('   El firmware debe aceptar el cargador y llegar a KERNEL_READY.')
    print('5. Sin firmar, OVMF con Secure Boot rechaza el cargador: es el')
    print('   comportamiento esperado. Este proyecto no desactiva Secure Boot.')
    print('La comprobacion automatica esta en: make test-secureboot.')


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    action = sys.argv[1].lower()
    if action == 'estado':
        estado()
        return 0
    if action == 'claves':
        return claves()
    if action == 'firmar':
        return firmar()
    if action == 'verificar':
        return verificar()
    if action == 'guia':
        guia()
        return 0
    print('Subcomando desconocido: %s' % action)
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main())

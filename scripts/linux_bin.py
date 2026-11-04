#!/usr/bin/env python3
"""Aprovisiona los binarios Linux que ejecuta la personalidad Linux del kernel.

Descarga el BusyBox estatico de Alpine y lo deja en vendor/linux-bin junto a su
licencia y su procedencia. El kernel no enlaza este binario: lo carga como
modulo Limine, lo ejecuta en ring 0 y atiende sus llamadas al sistema con la
capa de src/linux_abi.c. Por eso el codigo MIT del kernel no pasa a GPL: hay
agregacion, no obra derivada.

El binario es ELF64 PIE con enlace estatico (static-pie), justo el formato que
sabe cargar src/elf.c, y solo necesita relocalizaciones compactas RELR, que
resuelve el propio programa al arrancar.

Uso:
    python3 scripts/linux_bin.py            # aprovisiona si falta
    python3 scripts/linux_bin.py --force    # vuelve a descargar
    python3 scripts/linux_bin.py --check    # solo informa del estado
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / 'vendor' / 'linux-bin'
WORK = ROOT / 'build' / 'linux-bin'

ALPINE = '3.24'
PACKAGE = 'busybox-static'
BINARY = 'busybox.static'
STAMP = BIN / 'provisionado.json'

ORIGEN = """# Binarios Linux ejecutados por la personalidad Linux

Origen: Alpine Linux v{version} (x86_64), paquete `{package}`,
<https://alpinelinux.org>.

`{binary}` es BusyBox compilado con musl y enlace estatico. ToolchainOS lo carga
como modulo Limine y lo ejecuta en ring 0: el kernel atiende sus llamadas al
sistema (`syscall`) con la capa de `src/linux_abi.c`. No se enlaza con el
kernel ni modifica su codigo, de modo que ToolchainOS sigue siendo MIT y este
binario conserva su licencia.

## Que se versiona

| Archivo | Tamano | Versionado |
|---|---|---|
| `{binary}` | ~1 MiB | si |
| `provisionado.json` | ~1 KiB | si |
| `licencias/GPL-2.0.txt` | ~18 KiB | si |

## Licencia

BusyBox se distribuye bajo GPL-2.0. El texto completo esta en `licencias/`.
ToolchainOS solo lo ejecuta como programa separado y ofrece su procedencia y su
licencia junto al binario, tal como exige la GPL para la redistribucion.

## Applets disponibles

Cualquier applet de BusyBox puede ejecutarse con `LINUX <applet> [argumentos]`.
La lista completa se obtiene con `LINUX --list`.
"""

LICENSES = {
    'GPL-2.0.txt': 'https://raw.githubusercontent.com/spdx/license-list-data/main/text/GPL-2.0-only.txt',
}


def run(cmd, **kw):
    """Ejecuta un comando mostrando su salida en caso de fallo."""
    result = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if result.returncode != 0:
        raise SystemExit('Fallo: %s\n%s\n%s'
                         % (' '.join(str(c) for c in cmd),
                            result.stdout[-4000:], result.stderr[-4000:]))
    return result.stdout


def sha256(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def podman():
    if subprocess.run(['podman', 'info'], capture_output=True).returncode != 0:
        raise SystemExit('Se requiere podman en el anfitrion para descargar el binario')
    return ['podman', 'run', '--rm', '-v', '%s:/out:z' % WORK, 'alpine:%s' % ALPINE]


def fetch():
    """Instala el paquete y copia el binario y su version fuera del contenedor."""
    script = r'''
set -e
apk add --no-cache --no-progress %s >/dev/null
cp /bin/%s /out/%s
awk '/^P:%s$/{f=1} f&&/^V:/{sub(/^V:/,"");print;exit}' /lib/apk/db/installed > /out/version.txt
ls -l /bin/%s > /out/archivos.txt
echo "binario copiado"
''' % (PACKAGE, BINARY, BINARY, PACKAGE, BINARY)
    run(podman() + ['sh', '-c', script])


def collect():
    """Copia licencias, procedencia y checksums al directorio versionado."""
    source = WORK / BINARY
    if not source.is_file():
        raise SystemExit('El contenedor no dejo %s; revisa el paquete' % BINARY)
    BIN.mkdir(parents=True, exist_ok=True)
    target = BIN / BINARY
    target.write_bytes(source.read_bytes())
    target.chmod(0o755)
    licenses = BIN / 'licencias'
    licenses.mkdir(exist_ok=True)
    for name, url in LICENSES.items():
        path = licenses / name
        if not path.is_file():
            run(['curl', '-fsSL', '--connect-timeout', '10', '--max-time', '60',
                 '-o', str(path), url])
    version = (WORK / 'version.txt').read_text(encoding='utf-8').strip()
    (BIN / 'ORIGEN.md').write_text(
        ORIGEN.format(version=ALPINE, package=PACKAGE, binary=BINARY), encoding='utf-8')
    files = (WORK / 'archivos.txt')
    if files.is_file():
        (BIN / 'archivos.txt').write_bytes(files.read_bytes())
    report = {
        'alpine': ALPINE,
        'paquete': version,
        'archivos': {
            BINARY: {'bytes': target.stat().st_size, 'sha256': sha256(target)},
        },
    }
    STAMP.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def status():
    binary = BIN / BINARY
    print('binarios Linux: %s' % BIN)
    for path in (binary, STAMP):
        if path.is_file():
            print('  OK  %-16s %8.1f MiB' % (path.name, path.stat().st_size / 1048576))
        else:
            print('  --  %-16s ausente' % path.name)
    return binary.is_file()


def main():
    parser = argparse.ArgumentParser(description='Descarga el BusyBox estatico de Alpine')
    parser.add_argument('--force', action='store_true', help='vuelve a descargar')
    parser.add_argument('--check', action='store_true', help='solo informa del estado')
    args = parser.parse_args()
    if args.check:
        raise SystemExit(0 if status() else 1)
    if status() and not args.force:
        print('Binarios ya aprovisionados; usa --force para volver a descargarlos')
        return
    WORK.mkdir(parents=True, exist_ok=True)
    print('[1/2] descargando %s de Alpine %s' % (PACKAGE, ALPINE))
    fetch()
    print('[2/2] recopilando binario, licencia y checksums')
    collect()
    status()


if __name__ == '__main__':
    main()

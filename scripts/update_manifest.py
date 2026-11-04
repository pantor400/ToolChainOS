#!/usr/bin/env python3
"""Regenera MANIFEST_SHA256.json y SHA256SUMS con el contenido publicado.

El alcance es el arbol entregable: los ficheros de la raiz, las carpetas
src/, include/, apps/, scripts/, docs/, vendor/ y artifacts/, mas el kernel y
la ISO ya construidos. Quedan fuera build/ (salvo esos dos binarios), entrega/,
las caches de Python y la configuracion local del IDE.

Uso: python3 scripts/update_manifest.py
"""

import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TREES = ('src', 'include', 'apps', 'scripts', 'docs', 'vendor', 'artifacts')
FILES = (
    'Makefile', 'linker.ld', 'limine.conf', 'README.md', 'LEEME_ENTREGA.md',
    'LEEME_PRIMERO.txt', 'CHANGELOG.md', 'LICENSE',
    'INICIAR_DEMO_LINUX.sh', 'INICIAR_DEMO_WINDOWS.bat',
    'build/kernel.elf', 'build/toolchain-os.iso',
)
IGNORED = ('__pycache__', ':Zone.Identifier')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def skipped(path):
    return any(token in path.parts or token in path.name for token in IGNORED)


def main():
    entries = {}
    for tree in TREES:
        for path in sorted((ROOT / tree).rglob('*')):
            if path.is_file() and not skipped(path):
                entries[str(path.relative_to(ROOT))] = sha(path)
    for name in FILES:
        path = ROOT / name
        if path.is_file():
            entries[name] = sha(path)
    manifest = dict(sorted(entries.items()))
    (ROOT / 'MANIFEST_SHA256.json').write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (ROOT / 'SHA256SUMS').write_text(
        ''.join('%s  %s\n' % (digest, name) for name, digest in manifest.items()),
        encoding='utf-8')
    print('Manifiesto actualizado: %d archivos' % len(manifest))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Empaqueta las imagenes de images/ en un blob RGB565 para el kernel.

El kernel compila con -mgeneral-regs-only y no lleva ningun decodificador JPEG
ni PNG: la forma didactica de mostrar imagenes es convertir los ficheros en el
anfitrion (Pillow) y enlazar el resultado como datos de solo lectura. El blob
se incrusta en el kernel con objcopy (ver Makefile) y src/images.c lo recorre
para el fondo del escritorio y para el visor de imagenes.

Formato (little endian, sin relleno):

    offset 0   char  magic[8]   "TCOSIMG1"
    8          u32   count
    12         u32   reserved
    16         entry[count] x 48 bytes:
                   char name[32]   nombre visible, sin ruta
                   u16  width
                   u16  height
                   u32  offset     desde el inicio del blob
                   u32  size       bytes de pixeles
                   u32  reserved
    ...        u16   pixels[width * height]  RGB565 fila a fila

Uso:
    python3 scripts/build_images.py                 # build/assets/images.bin
    python3 scripts/build_images.py --size 1280x720 # tamano del wallpaper
    python3 scripts/build_images.py --check         # solo valida Pillow

Requiere: Pillow. Las imagenes son JPEG o PNG del directorio images/.
"""

import argparse
import pathlib
import struct
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'images'
OUTPUT = ROOT / 'build' / 'assets' / 'images.bin'
MAGIC = b'TCOSIMG1'
ENTRY_SIZE = 48
# El nombre debe caber en name[32] del formato y en FS_NAME_MAX del sistema de
# archivos; se recorta sin extension si hace falta.
NAME_MAX = 32
MAX_WIDTH = 1920
MAX_HEIGHT = 1200


def parse_size(text):
    try:
        width, height = text.lower().split('x')
        return int(width), int(height)
    except ValueError:
        raise argparse.ArgumentTypeError('formato esperado ANCHOxALTO, por ejemplo 1280x720')


def collect():
    """Imagenes de images/, con wallpaper primero y orden alfabetico despues."""
    supported = ('.jpg', '.jpeg', '.png')
    files = [p for p in SOURCE.iterdir() if p.is_file() and p.suffix.lower() in supported]
    files.sort(key=lambda p: (p.stem.lower() != 'wallpaper', p.name.lower()))
    return files


def load(path, wallpaper_size):
    from PIL import Image

    image = Image.open(path)
    image = image.convert('RGB')
    if path.stem.lower() == 'wallpaper' and wallpaper_size:
        width, height = wallpaper_size
        # Recorte centrado al aspecto pedido: el fondo cubre la pantalla sin
        # deformar la imagen original.
        target = width / height
        source = image.width / image.height
        if source > target:
            new_width = int(image.height * target)
            left = (image.width - new_width) // 2
            image = image.crop((left, 0, left + new_width, image.height))
        elif source < target:
            new_height = int(image.width / target)
            top = (image.height - new_height) // 2
            image = image.crop((0, top, image.width, top + new_height))
        image = image.resize((width, height), Image.LANCZOS)
    elif image.width > MAX_WIDTH or image.height > MAX_HEIGHT:
        image.thumbnail((MAX_WIDTH, MAX_HEIGHT), Image.LANCZOS)
    return image


def pixels_rgb565(image):
    """Convierte a RGB565 sin numpy: 3 bytes de Pillow por pixel."""
    data = image.tobytes()
    out = bytearray(image.width * image.height * 2)
    index = 0
    for position in range(0, len(data), 3):
        red = data[position] >> 3
        green = data[position + 1] >> 2
        blue = data[position + 2] >> 3
        value = (red << 11) | (green << 5) | blue
        out[index] = value & 0xff
        out[index + 1] = value >> 8
        index += 2
    return bytes(out)


def verify(path):
    """Relee el blob generado: el kernel lo valida igual y un desajuste de
    tamano de entrada dejaria las imagenes inservibles."""
    data = path.read_bytes()
    if data[:8] != MAGIC:
        raise SystemExit('ERROR: magic del blob incorrecto')
    count = struct.unpack_from('<I', data, 8)[0]
    if 16 + count * ENTRY_SIZE > len(data):
        raise SystemExit('ERROR: la tabla de imagenes no cabe en el blob')
    for index in range(count):
        entry = data[16 + index * ENTRY_SIZE:16 + (index + 1) * ENTRY_SIZE]
        width, height, offset, size = struct.unpack_from('<HHII', entry, 32)
        if offset < 16 or offset + size > len(data) or size != width * height * 2:
            raise SystemExit(f'ERROR: entrada {index} incoherente')
    print(f'Blob verificado: {count} imagenes')


def build(size, output):
    files = collect()
    if not files:
        print(f'ERROR: no hay imagenes en {SOURCE}/', file=sys.stderr)
        return 1
    images = []
    for path in files:
        image = load(path, size)
        name = path.stem
        if len(name.encode('utf-8')) >= NAME_MAX:
            name = name.encode('utf-8')[:NAME_MAX - 1].decode('utf-8', 'ignore')
        images.append((name, image, pixels_rgb565(image)))
        print(f'  {path.name}: {image.width}x{image.height} ({len(images[-1][2])} B RGB565)')
    header = bytearray()
    header += MAGIC
    header += struct.pack('<II', len(images), 0)
    offset = 16 + ENTRY_SIZE * len(images)
    entries = bytearray()
    payload = bytearray()
    for name, image, data in images:
        raw = name.encode('utf-8')[:NAME_MAX - 1]
        entries += raw + b'\0' * (32 - len(raw))
        entries += struct.pack('<HHIII', image.width, image.height, offset, len(data), 0)
        payload += data
        offset += len(data)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(bytes(header) + bytes(entries) + bytes(payload))
    verify(output)
    # Con "make BUILD=/tmp/..." la salida queda fuera del repositorio.
    try:
        shown = output.relative_to(ROOT)
    except ValueError:
        shown = output
    print(f'Imagenes: {len(images)} en {shown} ({offset} B)')
    return 0


def check():
    try:
        import PIL  # noqa: F401
    except ImportError:
        print('ERROR: falta Pillow (python3-pillow); el blob no se puede generar.', file=sys.stderr)
        return 1
    files = collect()
    if not files:
        print(f'ERROR: no hay imagenes en {SOURCE}/', file=sys.stderr)
        return 1
    print('OK: Pillow disponible e imagenes encontradas: ' + ', '.join(p.name for p in files))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--size', type=parse_size, default=(1280, 720),
                        help='resolucion del wallpaper (por defecto 1280x720)')
    parser.add_argument('--output', type=pathlib.Path, default=OUTPUT)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    if args.check:
        return check()
    return build(args.size, args.output.resolve())


if __name__ == '__main__':
    sys.exit(main())

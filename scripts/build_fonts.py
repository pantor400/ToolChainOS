#!/usr/bin/env python3
"""Hornea tipografias reales en tablas C de cobertura 4bpp.

El kernel compila con -mgeneral-regs-only: no puede usar coma flotante ni SSE,
asi que no puede rasterizar TrueType en caliente. La solucion didactica es
pre-hornear los tamanos que necesita la interfaz en el anfitrion (Pillow) y
enlazarlos como datos de solo lectura. "Escalable" se cumple en dos sentidos:
se regeneran a cualquier tamano con este script y la GUI elige el perfil que
corresponde a la resolucion real del framebuffer.

Uso:
    python3 scripts/build_fonts.py            # genera include/font_ui.h y src/font_ui.c
    python3 scripts/build_fonts.py --check    # solo verifica que Pillow y las fuentes existan

Requiere: Pillow y una fuente TrueType del sistema (Liberation Sans o Noto Sans).
"""

import argparse
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
CANDIDATES_REG = [
    pathlib.Path('/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf'),
    pathlib.Path('/usr/share/fonts/google-noto/NotoSans-Regular.ttf'),
]
CANDIDATES_BOLD = [
    pathlib.Path('/usr/share/fonts/liberation-sans-fonts/LiberationSans-Bold.ttf'),
    pathlib.Path('/usr/share/fonts/google-noto/NotoSans-Bold.ttf'),
]

# ASCII imprimible + el repertorio espanol y simbolos de interfaz que ya usa
# la consola mas unos pocos signos utiles en botones y estados.
CHARSET = [chr(c) for c in range(32, 127)]
CHARSET += list('\u00e1\u00e9\u00ed\u00f3\u00fa\u00f1\u00fc'
                '\u00c1\u00c9\u00cd\u00d3\u00da\u00d1\u00dc'
                '\u00bf\u00a1\u00b0\u20ac\u00ab\u00bb\u2026\u2014\u2013\u00b7\u2022')

# (nombre C, tamano px, negrita)  pequeno -> grande
FONTS = [
    ('reg15', 15, False),
    ('reg21', 21, False),
    ('reg30', 30, False),
    ('reg40', 40, False),
    ('bold30', 30, True),
    ('bold40', 40, True),
]


def pick(candidates):
    for path in candidates:
        if path.exists():
            return path
    return None


def quantize(value):
    """Cobertura 8bpp -> 4bpp redondeando (0..255 -> 0..15)."""
    return min(15, max(0, (value + 8) // 17))


def bake(font, ch):
    """Devuelve (w, h, ox, oy, advance, filas_de_nibbles)."""
    advance = max(1, round(font.getlength(ch)))
    bbox = font.getbbox(ch, anchor='ls')
    left, top, right, bottom = bbox
    w = right - left
    h = bottom - top
    if w <= 0 or h <= 0:
        return 0, 0, 0, 0, advance, b''
    from PIL import Image, ImageDraw
    img = Image.new('L', (w, h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((-left, -top), ch, font=font, fill=255, anchor='ls')
    row_bytes = (w + 1) // 2
    out = bytearray(row_bytes * h)
    px = img.load()
    for y in range(h):
        base = y * row_bytes
        for x in range(w):
            nib = quantize(px[x, y])
            if nib:
                out[base + (x >> 1)] |= nib << (0 if (x & 1) == 0 else 4)
    return w, h, left, top, advance, bytes(out)


def emit(fonts_data, header_path, source_path):
    lines = ['/* Generado por scripts/build_fonts.py: no editar a mano. */',
             '#ifndef FONT_UI_H', '#define FONT_UI_H', '#include "gui_font.h"']
    for name, _size, _bold in FONTS:
        lines.append('extern const gui_font_t gui_font_%s;' % name)
    lines += ['#endif', '']
    header_path.write_text('\n'.join(lines), encoding='utf-8')

    src = ['/* Generado por scripts/build_fonts.py: no editar a mano.',
           ' * Cobertura 4bpp: nibble bajo = pixel par, nibble alto = pixel impar. */',
           '#include "font_ui.h"', '']
    for name, ascent, descent, height, line_gap, glyphs, bits in fonts_data:
        src.append('static const uint8_t %s_bits[] = {' % name)
        for i in range(0, len(bits), 24):
            src.append('    ' + ','.join('0x%02x' % b for b in bits[i:i + 24]) + ',')
        src.append('};')
        src.append('static const gui_glyph_t %s_glyphs[] = {' % name)
        for cp, w, h, ox, oy, advance, row_bytes, offset in glyphs:
            src.append('    {0x%04x,%u,%u,%d,%d,%u,%u,%u},' % (cp, w, h, ox, oy, advance, row_bytes, offset))
        src.append('};')
        src.append('const gui_font_t gui_font_%s = {%s_glyphs,%u,%s_bits,%d,%d,%d,%d};'
                   % (name, name, len(glyphs), name, ascent, descent, height, line_gap))
        src.append('')
    source_path.write_text('\n'.join(src), encoding='utf-8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()

    try:
        from PIL import ImageFont
    except ImportError:
        print('ERROR: falta Pillow. Instala python3-pillow.', file=sys.stderr)
        return 2
    reg = pick(CANDIDATES_REG)
    bold = pick(CANDIDATES_BOLD)
    if not reg or not bold:
        print('ERROR: no se encontro Liberation Sans ni Noto Sans.', file=sys.stderr)
        return 2
    print('regular:', reg)
    print('negrita:', bold)
    if args.check:
        return 0

    fonts_data = []
    total = 0
    for name, size, is_bold in FONTS:
        font = ImageFont.truetype(str(bold if is_bold else reg), size)
        ascent, descent = font.getmetrics()
        glyphs = []
        bits = bytearray()
        for ch in CHARSET:
            w, h, ox, oy, advance, rows = bake(font, ch)
            glyphs.append((ord(ch), w, h, ox, oy, advance, (w + 1) // 2, len(bits)))
            bits.extend(rows)
        glyphs.sort(key=lambda g: g[0])
        total += len(bits)
        fonts_data.append((name, ascent, descent, ascent + descent, max(0, size // 6), glyphs, bytes(bits)))
        print('%-7s %2dpx  %3d glifos  %6d bytes' % (name, size, len(glyphs), len(bits)))

    emit(fonts_data, ROOT / 'include' / 'font_ui.h', ROOT / 'src' / 'font_ui.c')
    print('total cobertura: %d bytes' % total)
    print('escrito: include/font_ui.h y src/font_ui.c')
    return 0


if __name__ == '__main__':
    sys.exit(main())

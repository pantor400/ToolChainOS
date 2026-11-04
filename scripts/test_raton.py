#!/usr/bin/env python3
"""Comprueba el puntero de la GUI con movimientos reales por QMP.

Dos fallos que se comprueban aqui:
- Seguimiento: una rafaga de eventos relativos debe dejar el puntero en la
  suma exacta del centro. Si se pierde o se mezcla un paquete PS/2, la
  posicion final no coincide.
- Rastro: con capturas PPM se comparan las regiones por las que paso el
  cursor. La zona abandonada debe volver a ser identica al fondo.

Uso: python3 scripts/test_raton.py
"""

import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import qemu_config
from test_gui import QMP, wait_for

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / 'build' / 'toolchain-os.iso'
# Forma del puntero en src/gui.c: con 1280 de ancho scale vale 1.
CURSOR_W, CURSOR_H = 13, 19
STEPS = 12
STEP_X, STEP_Y = 40, 20


def read_ppm(path):
    """Ancho, alto y pixeles RGB de un PPM binario (P6)."""
    data = path.read_bytes()
    if not data.startswith(b'P6'):
        raise ValueError('no es un PPM binario: %s' % path)
    fields = []
    index = 2
    while len(fields) < 3:
        while data[index:index + 1].isspace():
            index += 1
        if data[index:index + 1] == b'#':
            index = data.index(b'\n', index) + 1
            continue
        end = index
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(int(data[index:end]))
        index = end
    width, height, _ = fields
    while len(data) - index != width * height * 3 and data[index:index + 1].isspace():
        index += 1
    pixels = data[index:]
    if len(pixels) != width * height * 3:
        raise ValueError('PPM truncado: %d bytes para %dx%d' % (len(pixels), width, height))
    return width, height, pixels


def region(image, x, y, width, height):
    """Copia de un rectangulo como lista de filas de bytes RGB."""
    frame_w, frame_h, pixels = image
    x = max(0, min(x, frame_w - 1))
    y = max(0, min(y, frame_h - 1))
    width = min(width, frame_w - x)
    height = min(height, frame_h - y)
    rows = []
    for row in range(y, y + height):
        start = (row * frame_w + x) * 3
        rows.append(pixels[start:start + width * 3])
    return rows


def capture(client, path, delay=0.5):
    time.sleep(delay)
    if path.exists():
        path.unlink()
    client.call('screendump', {'filename': str(path), 'format': 'ppm'})
    end = time.monotonic() + 5
    while not path.exists():
        if time.monotonic() > end:
            raise RuntimeError('screendump no genero %s' % path)
        time.sleep(0.05)
    return read_ppm(path)


def burst(client, dx, dy, steps):
    """Todos los eventos relativos en una sola orden: no da respiro al kernel."""
    events = []
    for _ in range(steps):
        events.append({'type': 'rel', 'data': {'axis': 'x', 'value': dx}})
        events.append({'type': 'rel', 'data': {'axis': 'y', 'value': dy}})
    client.call('input-send-event', {'events': events})
    time.sleep(0.6)


def main():
    if not ISO.exists():
        print('Falta %s: ejecuta make iso.' % ISO)
        return 2
    with tempfile.TemporaryDirectory(prefix='raton-test-') as workdir:
        work = Path(workdir)
        serial_path = work / 'serial.log'
        socket_path = work / 'qmp.sock'
        command = (qemu_config.base(iso=ISO) + qemu_config.net()
                   + qemu_config.display('none') + qemu_config.serial_file(serial_path)
                   + qemu_config.qmp(socket_path))
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        client = None
        try:
            wait_for(serial_path, 'KERNEL_READY', 40, 'arranque del kernel')
            client = QMP(socket_path)
            wait_for(serial_path, 'OS> ', 10, 'prompt de la shell')
            client.ascii('gui\n')
            text = wait_for(serial_path, 'GUI_READY RES=', 15, 'escritorio interactivo')
            match = re.search(r'GUI_READY RES=(\d+)x(\d+)', text)
            width, height = int(match.group(1)), int(match.group(2))
            center_x, center_y = width // 2, height // 2
            target_x, target_y = center_x + STEPS * STEP_X, center_y + STEPS * STEP_Y
            print('OK  resolucion %dx%d, puntero inicial en (%d, %d)'
                  % (width, height, center_x, center_y))

            base = capture(client, work / 'base.ppm', 0.8)

            burst(client, STEP_X, STEP_Y, STEPS)
            moved = capture(client, work / 'movido.ppm')
            burst(client, -STEP_X, -STEP_Y, STEPS)
            back = capture(client, work / 'vuelto.ppm')

            cursor_at_center = region(base, center_x, center_y, CURSOR_W, CURSOR_H)
            center_after = region(moved, center_x, center_y, CURSOR_W, CURSOR_H)
            if cursor_at_center == center_after:
                print('FALLO: el puntero no dejo el centro al moverse')
                return 1
            print('OK  al moverse se repone el fondo del centro (sin estela)')

            if region(back, center_x, center_y, CURSOR_W, CURSOR_H) != cursor_at_center:
                print('FALLO: al volver el puntero no se redibuja igual en el centro')
                return 1
            print('OK  el puntero vuelve al centro con la misma forma')

            # La zona destino conserva el fondo original: no hay rastro del
            # puntero que estuvo alli durante la rafaga.
            if region(back, target_x, target_y, CURSOR_W, CURSOR_H) != \
                    region(base, target_x, target_y, CURSOR_W, CURSOR_H):
                print('FALLO: queda rastro del puntero en (%d, %d)' % (target_x, target_y))
                return 1
            if region(moved, target_x, target_y, CURSOR_W, CURSOR_H) == \
                    region(base, target_x, target_y, CURSOR_W, CURSOR_H):
                print('FALLO: el puntero no llego a (%d, %d)' % (target_x, target_y))
                return 1
            print('OK  sin rastro en (%d, %d) tras abandonar la zona' % (target_x, target_y))

            # Tramo asimetrico final: si se hubiera perdido movimiento, la ida
            # y la vuelta podrian cancelarse y ocultarlo. Aqui no hay vuelta.
            final_x = center_x + (STEPS // 2) * STEP_X
            final_y = center_y + (STEPS // 2) * STEP_Y
            burst(client, STEP_X, STEP_Y, STEPS // 2)

            before = serial_path.read_text(errors='replace')
            client.key('esc')
            wait_for(serial_path, 'SESION FINALIZADA', 15, 'salida del escritorio')
            text = wait_for(serial_path, 'GUI: PUNTERO', 10, 'traza del puntero al salir')
            trace = re.search(r'GUI: PUNTERO X=(-?\d+) Y=(-?\d+) PAQUETES=(\d+) DESCARTES=(\d+)',
                              text[len(before):] if before else text)
            if not trace:
                print('FALLO: falta la traza GUI: PUNTERO en la serie')
                return 1
            x, y, packets, discards = (int(value) for value in trace.groups())
            if (x, y) != (final_x, final_y):
                print('FALLO: el puntero quedo en (%d, %d), se esperaba (%d, %d)'
                      % (x, y, final_x, final_y))
                return 1
            print('OK  seguimiento exacto tras la rafaga: X=%d Y=%d' % (x, y))
            if packets <= 0:
                print('FALLO: el raton no recibio paquetes')
                return 1
            print('OK  paquetes atendidos: %d' % packets)
            if discards:
                print('FALLO: %d bytes descartados al resincronizar el PS/2' % discards)
                return 1
            print('OK  ningun byte descartado: paquetes PS/2 alineados')

            client.ascii('raton\n')
            text = wait_for(serial_path, 'RATON PS/2: ACTIVO', 10, 'estado del raton')
            if 'IRQ_PAQUETES=0' in text:
                print('FALLO: el raton no recibio paquetes')
                return 1
            print('RATON: TODAS LAS COMPROBACIONES SUPERADAS')
            return 0
        finally:
            if client:
                client.close()
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()


if __name__ == '__main__':
    sys.exit(main())

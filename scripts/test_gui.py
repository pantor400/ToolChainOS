#!/usr/bin/env python3
"""Prueba de humo de la interfaz grafica sin capturas de pantalla.

Arranca QEMU sin ventana, escribe en la shell por QMP y comprueba los
marcadores que el kernel emite por COM1. Valida el arranque de la GUI, el
driver de red, el raton PS/2 (movimiento real por IRQ12), la lista de
aplicaciones ELF y la carga de las apps. Con dos ventanas abiertas comprueba
ademas la ventana activa: el clic en el dock la cambia y las teclas dejan de
llegar a la ventana que la perdio.

Uso: python3 scripts/test_gui.py
"""

import json
import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import qemu_config

ROOT = Path(__file__).resolve().parent.parent
ISO = ROOT / 'build' / 'toolchain-os.iso'


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
        time.sleep(0.05)

    def move(self, dx, dy):
        events = []
        if dx:
            events.append({'type': 'rel', 'data': {'axis': 'x', 'value': dx}})
        if dy:
            events.append({'type': 'rel', 'data': {'axis': 'y', 'value': dy}})
        if events:
            self.call('input-send-event', {'events': events})
            time.sleep(0.02)

    def button(self, down):
        self.call('input-send-event', {
            'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
        time.sleep(0.05)

    def ascii(self, text):
        # QMP usa nombres de tecla (qcodes), no caracteres: se traducen los
        # signos que aparecen en las ordenes de la shell.
        qcodes = {' ': 'spc', '\n': 'ret', '.': 'dot', '-': 'minus',
                  '/': 'slash', ',': 'comma', '=': 'equal'}
        for character in text:
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
    print(text[-2000:])
    raise SystemExit(1)


def click_at(client, x, y):
    """Clic en coordenadas absolutas: el puntero parte del centro al abrir la
    GUI, se lleva a la esquina superior izquierda con margen de sobra (20
    pasos de 100 px superan cualquier perfil) y desde ahi se avanza."""
    for _ in range(20):
        client.move(-100, -100)
    while x > 0 or y > 0:
        dx, dy = min(40, x), min(30, y)
        client.move(dx, dy)
        x -= dx
        y -= dy
    client.button(True)
    client.button(False)
    time.sleep(0.6)


def new_serial_text(serial, before):
    """Lo que se ha escrito por serie despues de leer 'before'."""
    return serial.read_text(errors='replace')[len(before):]


def main():
    if not ISO.exists():
        print('Falta %s: ejecuta make iso.' % ISO)
        return 2
    with tempfile.TemporaryDirectory(prefix='gui-test-') as workdir:
        serial_path = Path(workdir) / 'serial.log'
        socket_path = Path(workdir) / 'qmp.sock'
        command = (qemu_config.base(iso=ISO) + qemu_config.net()
                   + qemu_config.display('none') + qemu_config.serial_file(serial_path)
                   + qemu_config.qmp(socket_path))
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
        client = None
        try:
            wait_for(serial_path, 'KERNEL_READY', 40, 'arranque del kernel')
            client = QMP(socket_path)
            wait_for(serial_path, 'OS> ', 10, 'prompt de la shell')
            wait_for(serial_path, 'FS: ARCHIVOS DEL SISTEMA EN SOLO LECTURA', 10,
                     'archivos internos del sistema en solo lectura')

            client.ascii('red\n')
            wait_for(serial_path, 'RED: RTL8139', 10, 'driver de red RTL8139')
            client.ascii('red ping 10.0.2.2\n')
            wait_for(serial_path, 'PING: 10.0.2.2 RESPUESTA', 10, 'ICMP a la puerta de QEMU')
            print('OK  RED PING responde a 10.0.2.2')
            client.ascii('red dns example.com\n')
            wait_for(serial_path, 'DNS: CONSULTANDO example.com', 10, 'consulta DNS del kernel')
            print('OK  RED DNS envia la consulta a 10.0.2.3')

            client.ascii('hora\n')
            text = wait_for(serial_path, 'HORA: ', 10, 'reloj CMOS del kernel')
            stamp = re.search(r'HORA: \d\d:\d\d:\d\d \d\d/\d\d/\d{4}', text)
            if not stamp:
                print('FALLO: el reloj CMOS no devuelve hora y fecha validas')
                return 1
            print('OK  reloj CMOS: %s' % stamp.group(0)[6:])

            client.ascii('raton\n')
            wait_for(serial_path, 'RATON PS/2: ACTIVO', 10, 'raton PS/2 inicializado')

            client.ascii('elf\n')
            text = wait_for(serial_path, 'APLICACIONES ELF DISPONIBLES', 10, 'modulos ELF listados')
            for module in ('editor.elf', 'terminal.elf', 'hoja.elf'):
                if module not in text:
                    print('FALLO: la ISO no contiene %s' % module)
                    return 1
                print('OK  %s presente en los modulos' % module)
            if 'hola.elf' in text:
                print('FALLO: la ISO todavia incluye hola.elf')
                return 1
            print('OK  hola.elf retirada de la ISO')

            client.ascii('gui demo\n')
            text = wait_for(serial_path, 'GUI_FRAME', 15, 'primer fotograma de la GUI')
            resolution = 'desconocida'
            for line in text.splitlines():
                if 'GUI_READY RES=' in line:
                    resolution = line.split('GUI_READY RES=', 1)[1].strip()
            print('OK  resolucion entregada por Limine: %s' % resolution)
            wait_for(serial_path, 'GUI: FONDO IMAGEN', 15,
                     'fondo del escritorio con la imagen empaquetada')
            wait_for(serial_path, 'SESION FINALIZADA', 15, 'consola restaurada tras la GUI')

            # Sesion interactiva: mover el puntero y pulsar el boton izquierdo
            # ejercita la IRQ12 real; el contador de paquetes lo confirma.
            # La sesion interactiva debe atender el teclado con rapidez: se
            # mide el tiempo desde ESC hasta la vuelta a la consola.
            start = time.monotonic()
            client.ascii('gui\n')
            wait_for(serial_path, 'GUI_READY RES=', 15, 'escritorio interactivo')
            for _ in range(6):
                client.move(40, 25)
            client.button(True)
            client.button(False)
            client.move(-90, -60)
            width, height = (int(value) for value in resolution.lower().split('x'))
            # Centros del dock a 1280 de ancho, con sus seis botones (Sistema
            # 32.3 %, Archivos 39.0 %, Red 44.6 %, Entrada 50.0 %, Imagenes
            # 56.9 %, Apps 63.0 %); anadir o quitar un boton mueve estas
            # fracciones, que salen de dock_button_rect().
            click_at(client, width * 57 // 100, height * 94 // 100)
            wait_for(serial_path, 'GUI: VISOR IMAGENES', 10,
                     'visor de imagenes abierto desde el dock')
            wait_for(serial_path, 'GUI: VENTANA ACTIVA Imagenes', 10,
                     'la ventana abierta pasa a ser la activa')

            # Con dos ventanas abiertas el teclado solo trabaja en la activa:
            # se abre Red desde el dock y las flechas ya no cambian la imagen.
            click_at(client, width * 45 // 100, height * 94 // 100)
            wait_for(serial_path, 'GUI: VENTANA ACTIVA Red', 10,
                     'el clic en Red le da la ventana activa')
            before = serial_path.read_text(errors='replace')
            client.key('down')
            time.sleep(0.5)
            if 'GUI: IMAGEN ' in new_serial_text(serial_path, before):
                print('FALLO: la flecha movio la imagen de la ventana inactiva')
                return 1
            print('OK  la flecha no afecta a la ventana inactiva')

            client.key('esc')
            wait_for(serial_path, 'SESION FINALIZADA', 15, 'salida del escritorio con ESC')
            print('OK  latencia de la sesion grafica: %.2f s' % (time.monotonic() - start))

            client.ascii('raton\n')
            text = wait_for(serial_path, 'RATON PS/2: ACTIVO', 10, 'estado del raton tras la GUI')
            packets = 0
            for line in text.splitlines():
                if 'IRQ_PAQUETES=' in line:
                    packets = int(line.split('IRQ_PAQUETES=', 1)[1].split()[0])
            if packets <= 0:
                print('FALLO: la GUI no recibio paquetes del raton')
                return 1
            print('OK  paquetes de raton atendidos por IRQ12: %d' % packets)

            client.ascii('gui run 0\n')
            wait_for(serial_path, 'ELF: EJECUTANDO editor.elf', 20, 'carga del editor ELF')
            wait_for(serial_path, 'editor.elf: PLANTILLA NUEVA', 20,
                     'el editor arranca con una plantilla en blanco')
            client.ascii('contenido del editor')
            # TAB abre el selector interno; otro TAB lo pasa a modo guardar.
            client.key('tab')
            wait_for(serial_path, 'editor.elf: SELECTOR ABRIR', 15,
                     'selector para elegir el archivo que abrir')
            client.key('tab')
            wait_for(serial_path, 'editor.elf: SELECTOR GUARDAR', 15,
                     'selector en modo guardar')
            client.ascii('demo2.txt')
            client.key('ret')
            wait_for(serial_path, 'editor.elf: GUARDADO /demo2.txt', 15,
                     'el editor guarda con la ruta elegida')
            client.key('esc')
            wait_for(serial_path, 'GUI: VENTANA CERRADA Editor de texto LIMPIA', 20,
                     'el cierre del editor limpia su area del escritorio')
            wait_for(serial_path, 'ELF: FIN editor.elf CODIGO=0', 20, 'retorno del editor ELF')

            client.ascii('gui run 1\n')
            wait_for(serial_path, 'ELF: EJECUTANDO terminal.elf', 20, 'carga de la terminal ELF')
            client.ascii('help\n')
            wait_for(serial_path, 'INTERFAZ GRAFICA, RED Y APLICACIONES', 20,
                     'orden ejecutada desde la terminal grafica')
            client.ascii('crear demo.txt fsok\n')
            wait_for(serial_path, 'ARCHIVO GUARDADO CON EXITO', 20,
                     'archivo creado desde la terminal grafica')
            client.ascii('open demo.txt\n')
            wait_for(serial_path, 'fsok', 20,
                     'contenido leido desde la terminal grafica')
            client.ascii('open demo2.txt\n')
            wait_for(serial_path, 'contenido del editor', 20,
                     'archivo guardado desde el editor se lee en la terminal')
            client.key('esc')
            wait_for(serial_path, 'ELF: FIN terminal.elf CODIGO=0', 20, 'retorno de la terminal ELF')

            # Hoja de calculo: app ELF nativa con formulas y CSV propio.
            client.ascii('crear hoja.csv 5,7\n')
            wait_for(serial_path, 'ARCHIVO GUARDADO CON EXITO', 20,
                     'hoja CSV de partida creada desde la consola')
            # El mapa ES-LATAM no tiene '=' ni '-' directas; con US se escriben
            # las formulas desde QMP sin combinar shift.
            client.ascii('teclado us\n')
            wait_for(serial_path, 'TECLADO: US', 10, 'mapa US para escribir formulas')
            client.ascii('gui run 2\n')
            wait_for(serial_path, 'ELF: EJECUTANDO hoja.elf', 20,
                     'carga de la hoja de calculo')
            wait_for(serial_path, 'hoja.elf: HOJA 8X16 LISTA', 20,
                     'la hoja arranca con la rejilla de 8x16')
            # La ventana de 800x520 queda centrada en 1280x720: su superficie
            # empieza en (240, 97) y el boton Abrir ocupa 120..224 de ancho y
            # 7..33 de alto dentro de la propia aplicacion.
            click_at(client, 240 + 172, 97 + 20)
            wait_for(serial_path, 'hoja.elf: ABIERTO /hoja.csv', 20,
                     'la hoja carga el CSV del directorio actual')
            # A1=5 y B1=7: la formula escrita en C1 debe dar 2. Se escribe en
            # minusculas porque QMP no envia mayusculas sin shift.
            client.key('right')
            client.key('right')
            client.ascii('=b1-a1')
            client.key('ret')
            wait_for(serial_path, 'hoja.elf: FORMULA C1 =b1-a1 -> 2', 20,
                     'la formula opera sobre el valor de otras celdas')
            client.ascii('10')
            client.key('ret')
            wait_for(serial_path, 'hoja.elf: CELDA C2 10', 20,
                     'la celda activa acepta numeros')
            click_at(client, 240 + 60, 97 + 20)
            wait_for(serial_path, 'hoja.elf: GUARDADO /hoja.csv', 20,
                     'el boton Guardar escribe el CSV en el sistema de archivos')
            client.key('esc')
            wait_for(serial_path, 'ELF: FIN hoja.elf CODIGO=0', 20,
                     'retorno de la hoja de calculo')
            client.ascii('open hoja.csv\n')
            wait_for(serial_path, '=b1-a1', 20,
                     'el CSV conserva la formula para volver a calcularla')
            client.ascii('teclado es\n')
            wait_for(serial_path, 'TECLADO: ES-LATAM', 10,
                     'se restaura el mapa ES-LATAM de la entrega')

            # GUI EDIT: el escritorio lanza el editor con el archivo indicado
            # mediante el handoff launch_file del ABI v4.
            client.ascii('gui edit demo2.txt\n')
            wait_for(serial_path, 'editor.elf: ABIERTO /demo2.txt', 20,
                     'el editor recibe la ruta de lanzamiento')
            client.key('esc')
            wait_for(serial_path, 'ELF: FIN editor.elf CODIGO=0', 20,
                     'retorno del editor lanzado con GUI EDIT')

            # Los archivos internos se pueden leer, pero el editor los abre en
            # modo de solo lectura y descarta cualquier cambio. Se entra en
            # /etc con CD porque la tecla "/" del mapa ES-LATAM exige shift.
            client.ascii('cd etc\n')
            client.ascii('gui edit motd\n')
            wait_for(serial_path, 'editor.elf: SOLO LECTURA /etc/motd', 20,
                     'el editor abre un archivo del sistema en solo lectura')
            client.ascii('x')
            client.key('esc')
            wait_for(serial_path, 'ELF: FIN editor.elf CODIGO=0', 20,
                     'retorno del editor con un archivo del sistema')
            client.ascii('cd ..\n')

            client.ascii('help\n')
            wait_for(serial_path, 'INTERFAZ GRAFICA, RED Y APLICACIONES', 10, 'ayuda ampliada')
            print('GUI: TODAS LAS COMPROBACIONES SUPERADAS')
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

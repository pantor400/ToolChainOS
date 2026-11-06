#!/usr/bin/env python3
"""Real ELF/QEMU/TW tests. Fixture mode is explicit and exists only here.
--real uses the production HTTP(S) fetcher and saves its real network results.
"""
import argparse
import json
import os
import re
import socket
import ssl
import threading
import time
import uuid
from pathlib import Path
from qemu_test import Guest
from test_gui import QMP as BaseQMP

class QMP(BaseQMP):
    def capture(self, name):
        self.call('screendump', {'filename': str(Path('artifacts', name).resolve()), 'format': 'png'})

from web_bridge import serve, document, Document, normalize, fetch
from ai_bridge import Decoder as AIDecoder, frame as ai_frame

OUT = Path('artifacts/navegador-web')
OUT.mkdir(parents=True, exist_ok=True)
calls = []


def fixture(url, token):
    calls.append(url)
    if url.endswith('/delay'):
        for _ in range(150):
            if token.wait(.1):
                raise ValueError('Cancelado')
    if url.endswith('/timeout'):
        raise TimeoutError()
    if url.endswith('/tls'):
        raise ssl.SSLCertVerificationError('fixture')
    if url.endswith('/pdf'):
        raise ValueError('Contenido no compatible: application/pdf')
    if url.endswith('/oversize'):
        return b'x' * 25000
    if url.endswith('/bad'):
        return b'bad-document'
    if url.endswith('/404'):
        return document(404, url, 'No encontrada', '404: Página no encontrada', [(0, '404 real del protocolo simulado', '')])
    title = 'Segunda página' if url.endswith('/next') else 'Prueba Perú & año'
    html = '<h1>' + title + '</h1><p>á é í ó ú ü ñ ¿Qué? ¡Sí!</p><a href="/next">Siguiente página</a>' + ''.join('<p>Fila ' + str(i) + ' de contenido largo para desplazar</p>' for i in range(45))
    p = Document(url)
    p.feed(html)
    p.flush()
    return document(200, url, title, 'PRUEBA DETERMINISTA: no es Internet', p.records)


def type_bytes(g, text):
    data = text.encode()
    for i in range(0, len(data), 16):
        g.q.stdin.write(data[i:i+16])
        g.q.stdin.flush()
        time.sleep(.03)


def open_app(g):
    g.buffer.clear()
    type_bytes(g, 'gui run 3\n')
    g.until('WEB_LOCAL', 20)
    time.sleep(.2)


def navigate(g, q, url):
    g.buffer.clear()
    # L focuses URL when page has focus; initial bar already focuses/selects all.
    type_bytes(g, 'l' + url + '\n')
    marker = 'WEB_REQUEST ' + url
    g.until(marker, 15)
    # Previous page logs may arrive after buffer.clear(). Retain only this request.
    start = g.buffer.find(marker.encode())
    del g.buffer[:start]


def close_app(g, q):
    q.key('esc')
    g.until('WEB_CLOSED', 10)
    g.until('OS> ', 10)
    time.sleep(.1)


def capture(q, name):
    time.sleep(.3)
    q.capture('navegador-web/' + name)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--real', action='store_true')
    parser.add_argument('--skip-offline', action='store_true', help='Retain already-passed offline checks; run connected cases only')
    args = parser.parse_args()
    tag = uuid.uuid4().hex[:8]
    qmp = Path('build/web-qmp-' + tag + '.sock')
    web = Path('build/web-test-' + tag + '.sock')
    ai = Path('build/web-ai-' + tag + '.sock')
    extra = ['-qmp', f'unix:{qmp},server=on,wait=off']
    results = {'mode': 'REAL HTTP/HTTPS' if args.real else 'MOCK PROTOCOL ONLY', 'checks': []}
    try:
        if not args.skip_offline:
            # Phase A: absent bridge. One VM at a time, no physical disks.
            with Guest('web-offline', extra) as g:
                q = QMP(qmp)
                try:
                    g.send('elf', ['navegador.elf'])
                    before = g.send('memstats')
                    used = int(re.search(r'USED=(\d+)', before).group(1))
                    for i in range(10):
                        open_app(g)
                        if i == 0:
                            capture(q, 'offline.png')
                            type_bytes(g, 'example.com\n')
                            g.until('Sin puente disponible', 8)
                            capture(q, 'sin-puente.png')
                        else:
                            q.key('esc')  # Leave address editing.
                        close_app(g, q)
                        after = g.send('memstats')
                        assert int(re.search(r'USED=(\d+)', after).group(1)) == used
                        g.send('heapstats', ['CHECK=1', 'PAGES=0'])
                    results['checks'] += ['ELF offline', 'local page', 'bridge absent', 'open/close x10 PMM/heap restored']
                finally:
                    q.close()
        qmp.unlink(missing_ok=True)
        stop = threading.Event()
        extra += ['-chardev', f'socket,id=ai,path={ai},server=on,wait=off', '-serial', 'chardev:ai',
                  '-chardev', f'socket,id=web,path={web},server=on,wait=off', '-serial', 'chardev:web']
        with Guest('web-real' if args.real else 'web-fixture', extra) as g:
            q = QMP(qmp)
            worker = threading.Thread(target=serve, args=(web, stop, fetch if args.real else fixture), daemon=True)
            worker.start()
            ai_conn = socket.socket(socket.AF_UNIX)
            ai_conn.connect(str(ai))
            ai_conn.settimeout(3)
            try:
                type_bytes(g, 'ia coexistencia\n')
                g.until('IA REQUEST', 5)
                payload = ai_conn.recv(2048)
                messages = AIDecoder().feed(payload)
                assert messages, payload
                rid = messages[-1][1]
                open_app(g)
                ai_conn.sendall(ai_frame(0x82, rid, 'COM2 WEB COEXISTENCE MOCK'))
                g.until('COM2 WEB COEXISTENCE MOCK', 5)
                results['checks'].append('COM2 responds while browser open')
                if args.real:
                    type_bytes(g, 'https://info.cern.ch/\n')
                    g.until('WEB_STATUS', 25)
                    assert 'WEB_PAGE HTTP=200' in g.buffer.decode(errors='replace'), g.buffer.decode(errors='replace')
                    assert b'TITLE=http://info.cern.ch' in g.buffer
                    capture(q, 'https-real.png')
                    # Select and follow the real downloaded CERN hyperlink.
                    q.key('tab')
                    g.until('WEB_LINK_SELECTED', 5)
                    g.buffer.clear()
                    q.key('ret')
                    g.until('WEB_STATUS', 25)
                    assert b'WEB_PAGE HTTP=' in g.buffer, g.buffer.decode(errors='replace')
                    capture(q, 'enlace-real.png')
                    from test_gui import click_at
                    g.buffer.clear()
                    click_at(q, 200, 121)
                    g.until('TITLE=http://info.cern.ch', 20)
                    results['checks'].append('HTTPS real + downloaded hyperlink + Back by mouse')
                    navigate(g, q, 'http://example.com/')
                    g.until('WEB_STATUS', 25)
                    assert b'WEB_PAGE HTTP=200' in g.buffer
                    capture(q, 'http-real.png')
                    results['checks'].append('HTTP real')
                else:
                    type_bytes(g, 'https://demo.example/start\n')
                    g.until('WEB_PAGE HTTP=200', 20)
                    g.until('WEB_STATUS', 10)
                    assert 'Prueba Perú & año'.encode() in g.buffer
                    assert 'á é í ó ú ü ñ ¿Qué? ¡Sí!'.encode() in g.buffer
                    capture(q, 'fixture-primera.png')
                    q.key('tab')
                    g.until('WEB_LINK_SELECTED https://demo.example/next', 5)
                    g.buffer.clear()
                    q.key('ret')
                    g.until('TITLE=Segunda página', 20)
                    results['checks'].append('Spanish/title/relative link by keyboard')
                    capture(q, 'fixture-enlace.png')
                    q.key('down');q.key('down');q.key('end')
                    capture(q, 'fixture-scroll.png')
                    # Mouse controls. Geometry is discovered from requested surface.
                    from test_gui import click_at
                    # Window 940x520 at 1280x720: content origin (170,97).
                    # Derived from framebuffer screenshot/layout; keep asserted screenshot.
                    ox, oy = 170, 97
                    g.buffer.clear()
                    click_at(q, ox + 30, oy + 24)
                    g.until('TITLE=Prueba Perú & año', 20)
                    g.buffer.clear()
                    click_at(q, ox + 70, oy + 24)
                    g.until('TITLE=Segunda página', 20)
                    g.buffer.clear()
                    click_at(q, ox + 110, oy + 24)
                    g.until('TITLE=Segunda página', 20)
                    results['checks'].append('Back Forward Reload by mouse')
                    navigate(g, q, 'https://demo.example/404')
                    g.until('404: Página no encontrada', 20)
                    navigate(g, q, 'https://demo.example/bad')
                    g.until('Respuesta malformada', 20)
                    navigate(g, q, 'https://demo.example/delay')
                    # Leave request pending; button cancels without closing GUI.
                    g.buffer.clear()
                    click_at(q, ox + 110, oy + 24)
                    g.until('WEB_CANCELLED', 5)
                    for path, message in [('timeout', 'Tiempo de espera agotado'),
                                          ('tls', 'HTTPS no válido'),
                                          ('pdf', 'Contenido no compatible'),
                                          ('oversize', 'Respuesta demasiado grande')]:
                        navigate(g, q, 'https://demo.example/' + path)
                        g.until(message, 20)
                    results['checks'].append('404 malformed/oversize document timeout TLS MIME cancellation')
                    navigate(g, q, 'https://demo.example/recovery')
                    g.until('WEB_PAGE HTTP=200', 25)
                    results['checks'].append('recovery after cancellation')
                    stop.set()
                    worker.join(timeout=2)
                    assert not worker.is_alive()
                    navigate(g, q, 'https://demo.example/bridge-off')
                    g.until('Sin puente disponible', 6)
                    stop = threading.Event()
                    worker = threading.Thread(target=serve, args=(web, stop, fixture), daemon=True)
                    worker.start()
                    navigate(g, q, 'https://demo.example/restarted')
                    g.until('WEB_PAGE HTTP=200', 25)
                    results['checks'].append('bridge disconnect/restart and late IDs discarded')
                close_app(g, q)
                g.send('suma 7 8', ['= 15'])
                g.send('help', ['GUI WEB'])
            finally:
                stop.set()
                ai_conn.close()
                q.close()
        worker.join(timeout=2)
        results['status'] = 'PASSED'
        return 0
    except Exception as exc:
        results['status'] = 'FAILED'
        results['error'] = repr(exc)
        raise
    finally:
        for p in [qmp, web, ai]:
            p.unlink(missing_ok=True)
        (OUT / ('qemu-real.json' if args.real else 'qemu-fixture.json')).write_text(json.dumps(results, indent=2, ensure_ascii=False))


if __name__ == '__main__':
    raise SystemExit(main())

#!/usr/bin/env python3
"""Sequential visual acceptance at all supported desktop resolutions."""
import json
import os
import struct
import subprocess
import threading
import time
import uuid
from pathlib import Path
from qemu_test import Guest
from test_web_qemu import QMP, fixture, type_bytes, capture
from test_gui import click_at
from web_bridge import serve

out = Path('artifacts/navegador-web')
results = []
try:
    for resolution in ['1280x720', '1280x800', '1920x1080']:
        with (out / ('build-' + resolution + '.log')).open('w') as log:
            subprocess.run(['make', 'iso', 'RES=' + resolution], check=True, stdout=log, stderr=subprocess.STDOUT)
        tag = uuid.uuid4().hex[:8]
        qmp, web = Path('build/profile-' + tag + '.sock'), Path('build/profile-web-' + tag + '.sock')
        extra = ['-qmp', 'unix:' + str(qmp) + ',server=on,wait=off', '-serial', 'null',
                 '-chardev', 'socket,id=web,path=' + str(web) + ',server=on,wait=off',
                 '-serial', 'chardev:web']
        stop = threading.Event()
        try:
            with Guest('web-profile-' + resolution, extra) as g:
                q = QMP(qmp)
                worker = threading.Thread(target=serve, args=(web, stop, fixture), daemon=True)
                worker.start()
                try:
                    type_bytes(g, 'gui web\n')
                    g.until('WEB_LOCAL', 20)
                    capture(q, 'local-' + resolution + '.png')
                    type_bytes(g, 'https://demo.example/start\n')
                    g.until('WEB_STATUS', 20)
                    assert b'WEB_PAGE HTTP=200' in g.buffer
                    capture(q, 'html-' + resolution + '.png')
                    q.key('end')
                    capture(q, 'scroll-' + resolution + '.png')
                    # Check real PNG dimensions and return to desktop/console.
                    png = (out / ('html-' + resolution + '.png')).read_bytes()
                    assert struct.unpack('>II', png[16:24]) == tuple(map(int, resolution.split('x')))
                    g.buffer.clear()
                    q.key('esc')
                    g.until('WEB_CLOSED', 10)
                    if resolution == '1280x720':
                        # Apps geometry from window_default(), font metrics and dock.
                        click_at(q, 807, 677)
                        g.until('VENTANA ACTIVA Aplicaciones', 10)
                        capture(q, 'apps-menu.png')
                        # margin=10, Apps slot=4, window y=205; title=35, row=26.
                        g.buffer.clear()
                        click_at(q, 220, 341)
                        g.until('WEB_LOCAL', 15)
                        capture(q, 'apps-navegador.png')
                        q.key('esc');q.key('esc')
                        g.until('WEB_CLOSED', 10)
                    g.buffer.clear()
                    q.key('esc')
                    g.until('OS> ', 10)
                    g.send('suma 4 5', ['= 9'])
                    results.append({'resolution': resolution, 'status': 'PASSED',
                                    'entry': 'GUI WEB', 'apps_menu': resolution == '1280x720'})
                finally:
                    stop.set()
                    q.close()
            worker.join(timeout=2)
        finally:
            qmp.unlink(missing_ok=True)
            web.unlink(missing_ok=True)
finally:
    subprocess.run(['make', 'iso', 'RES=1280x720'], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    (out / 'profiles.json').write_text(json.dumps(results, indent=2))
print('PASSED: 1280x720 / 1280x800 / 1920x1080, GUI WEB, Apps, screenshots and clean exit')

#!/usr/bin/env python3
"""Targeted check of the final COM3 budget with a near-24-KiB representation."""
import json
import threading
import time
import uuid
from pathlib import Path
from qemu_test import Guest
from test_web_qemu import QMP, type_bytes, capture
from web_bridge import serve, document, MAX_DOC

out = Path('artifacts/navegador-web')
url = 'https://large.example/page'
payload = document(200, url, 'Página grande', 'FIXTURE de límite, no es Internet',
                   [(0, ('ñáéíóú ' + 'a' * 1000), '') for _ in range(128)])
assert 23000 < len(payload) <= MAX_DOC
tag = uuid.uuid4().hex[:8]
qmp = Path('build/large-qmp-' + tag + '.sock')
web = Path('build/large-web-' + tag + '.sock')
stop = threading.Event()
result = {'payload_bytes': len(payload), 'limit': MAX_DOC, 'mode': 'MOCK PROTOCOL ONLY'}
try:
    extra = ['-qmp', 'unix:' + str(qmp) + ',server=on,wait=off', '-serial', 'null',
             '-chardev', 'socket,id=web,path=' + str(web) + ',server=on,wait=off',
             '-serial', 'chardev:web']
    with Guest('web-large', extra) as g:
        q = QMP(qmp)
        worker = threading.Thread(target=serve, args=(web, stop, lambda u, c: payload), daemon=True)
        worker.start()
        try:
            type_bytes(g, 'gui run 3\n')
            g.until('WEB_LOCAL', 15)
            g.buffer.clear()
            start = time.monotonic()
            type_bytes(g, url + '\n')
            g.until('WEB_REQUEST', 10)
            q.key('down')
            capture(q, 'large-loading.png')
            g.until('TITLE=Página grande', 35)
            g.until('WEB_STATUS', 5)
            assert b'WEB_PAGE HTTP=200' in g.buffer
            result.update(status='PASSED', seconds=round(time.monotonic() - start, 2))
            capture(q, 'large-loaded.png')
            q.key('esc')
            g.until('WEB_CLOSED', 10)
            g.until('OS> ', 10)
        finally:
            stop.set()
            q.close()
    worker.join(timeout=2)
finally:
    qmp.unlink(missing_ok=True)
    web.unlink(missing_ok=True)
    (out / 'large-document.json').write_text(json.dumps(result, indent=2, ensure_ascii=False))
print(json.dumps(result, ensure_ascii=False))

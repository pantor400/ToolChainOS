#!/usr/bin/env python3
"""Exercise actual run_vm profiles, serial slots, permissions and cleanup."""
import json
import os
import select
import signal
import stat
import subprocess
import time
from pathlib import Path

OUT = Path('artifacts/navegador-web')
OUT.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, QEMU_ACCEL='tcg')
results = []
for flags in [[], ['--web'], ['--ai', '--web']]:
    printed = subprocess.check_output(['python3', 'scripts/run_vm.py', '--headless', '--print-command', *flags], text=True, env=env)
    if '--web' in flags:
        assert 'id=web' in printed and '-serial chardev:web' in printed
        assert ('-serial chardev:ai' if '--ai' in flags else '-serial null') in printed
    else:
        assert 'id=web' not in printed and 'web.sock' not in printed
    p = subprocess.Popen(['python3', '-u', 'scripts/run_vm.py', '--headless', *flags],
                         stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    data = bytearray()
    def until(marker, timeout=35):
        end = time.monotonic() + timeout
        while marker not in data:
            if p.poll() is not None:
                raise RuntimeError(data.decode(errors='replace'))
            if time.monotonic() > end:
                raise TimeoutError(data.decode(errors='replace'))
            if select.select([p.stdout], [], [], .1)[0]:
                data.extend(os.read(p.stdout.fileno(), 4096))
    children = []
    try:
        until(b'OS> ')
        children = Path('/proc/' + str(p.pid) + '/task/' + str(p.pid) + '/children').read_text().split()
        if '--web' in flags:
            info = Path('build/web.sock').lstat()
            assert stat.S_ISSOCK(info.st_mode) and stat.S_IMODE(info.st_mode) == 0o600
        else:
            assert not Path('build/web.sock').exists()
        if '--ai' in flags:
            p.stdin.write(b'ia status\n');p.stdin.flush()
            until(b'IA END', 10)
        p.stdin.write(b'gui run 3\n');p.stdin.flush()
        until(b'WEB_LOCAL', 20)
    finally:
        p.send_signal(signal.SIGINT)
        try:
            p.wait(timeout=8)
        except subprocess.TimeoutExpired:
            p.kill();p.wait()
        tag = 'plain' if not flags else '-'.join(f.lstrip('-') for f in flags)
        (OUT / ('launcher-' + tag + '.log')).write_bytes(data)
    assert not Path('build/web.sock').exists()
    assert not Path('build/ai.sock').exists()
    assert not any(Path('/proc/' + pid).exists() for pid in children), children
    results.append({'flags': flags, 'status': 'PASSED', 'command': printed.strip(),
                    'private_socket': '--web' in flags, 'children_reaped': True})
(OUT / 'launcher.json').write_text(json.dumps(results, indent=2))
print('PASSED: launcher plain / --web / --ai --web; COM slots, 0600, cleanup and no hidden web service')

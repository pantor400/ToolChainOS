#!/usr/bin/env python3
"""Check the actual launcher, its safe image guard, and child cleanup."""
import os,select,signal,subprocess,time,uuid
from pathlib import Path
from run_vm import image_path
from create_test_disk import create
for path in ['/dev/sda','/tmp/unrelated.img']:
    try:image_path(path)
    except ValueError:pass
    else:raise AssertionError('unsafe path accepted')
image=create('build/launcher-test-'+uuid.uuid4().hex[:10]+'.img')
q=subprocess.Popen(['python3','scripts/run_vm.py','--ata','--headless','--disk',str(image)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
output=bytearray()
def until(needle):
    end=time.monotonic()+30
    while needle not in output:
        if q.poll() is not None:raise RuntimeError(output.decode(errors='replace'))
        if time.monotonic()>end:raise TimeoutError(output.decode(errors='replace'))
        if select.select([q.stdout],[],[],.1)[0]:output.extend(os.read(q.stdout.fileno(),4096))
try:
    until(b'OS> ')
    output.clear();q.stdin.write(b'disk info\n');q.stdin.flush()
    until(b'WRITE=AUTHORIZED')
    q.stdin.write(b'suma 5 6\n');q.stdin.flush();until(b'= 11')
    print('PASSED: lanzador ATA real, guardia de rutas y shell')
finally:
    q.send_signal(signal.SIGINT)
    try:q.wait(timeout=5)
    except subprocess.TimeoutExpired:q.kill();q.wait()
    Path('artifacts/launcher.serial.txt').write_bytes(output)
    image.unlink(missing_ok=True)

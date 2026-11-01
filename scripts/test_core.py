#!/usr/bin/env python3
import re,time
from qemu_test import Guest
with Guest('core') as g:
    before=g.send('memstats',['PAGE=4096'])
    for _ in range(2):g.send('pmmtest',['PMMTEST PASSED'],timeout=60)
    after=g.send('memstats')
    assert re.search(r'FREE=(\d+)',before).group(1)==re.search(r'FREE=(\d+)',after).group(1)
    t=int(re.search(r'TICKS=(\d+)',g.send('uptime',['HZ=100'])).group(1))
    time.sleep(.2)
    assert int(re.search(r'TICKS=(\d+)',g.send('uptime')).group(1))>t
    g.send('tasks start',['COOPERATIVAS'])
    g.send('suma 4 5',['= 9'],async_output=True)
    time.sleep(.4)
    text=g.send('tasks',['TASK 1 DEAD STEPS=3','TASK 2 DEAD STEPS=3'])
    assert b'TASK 1 STEP 1' in g.transcript and b'TASK 2 STEP 1' in g.transcript
    end=g.send('memstats')
    assert re.search(r'FREE=(\d+)',before).group(1)==re.search(r'FREE=(\d+)',end).group(1)
    for _ in range(2):
        g.send('tasks start',['COOPERATIVAS']);time.sleep(.3)
        g.send('tasks',['TASK 1 DEAD STEPS=3','TASK 2 DEAD STEPS=3'])
print('PASSED: PMM agotamiento/reutilizacion x2, timer creciente, tareas con pilas x3 y shell')
for kind,vector in [('DE',0),('PF',14)]:
    with Guest('exception-'+kind) as g:
        g.send('extest '+kind,['EXCEPTION VECTOR='+str(vector),'RIP=','ERROR='],marker='FATAL: CPU DETENIDA')
print('PASSED: excepciones division por cero y page fault en arranques aislados')

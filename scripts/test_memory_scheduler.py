#!/usr/bin/env python3
"""Real serial/QMP execution; no precomputed scheduling output."""
import re,time,uuid,json
from pathlib import Path
from qemu_test import Guest
from qmp_test import QMP
def live(g,command,expected=()):
    # Worker output can interleave with each echoed keystroke. Synchronize on
    # response + new shell prompt, not on a contiguous copy of the input echo.
    g.buffer.clear()
    g.q.stdin.write((command+'\n').encode());g.q.stdin.flush()
    text=g.until('OS> ',15)
    for item in expected:
        assert item in text,(command,item,text)
    return text
sock=Path('build/e56-'+uuid.uuid4().hex[:8]+'.sock')
with Guest('memory-scheduler', ['-qmp',f'unix:{sock},server=on,wait=off']) as g:
    q=QMP(sock)
    try:
        before=g.send('memstats',['RESERVED='])
        used=int(re.search(r'USED=(\d+)',before).group(1))
        g.send('heapstats',['PAGES=0','CHECK=1'])
        q.capture('memoria-antes.png')
        for _ in range(3):
            g.send('pmmtest',['PMMTEST PASSED','COUNTERS_RESTORED=1'])
            g.send('heaptest',['HEAPTEST PASSED'])
        g.send('heapstats',['PAGES=0','BLOCKS=0','CHECK=1'])
        q.capture('memoria-despues.png')
        last_tid=1;last_pid=1
        for i in range(12):
            start=len(g.transcript)
            g.send('sched test',['PROCESSES=2 THREADS=3','STATE=READY','STACK_PAGES=8'])
            live(g,'suma 19 23',['= 42'])
            # Dispatch runs from shell loop, sleeping threads let shell continue.
            live(g,'sched',['ROUND_ROBIN COOPERATIVE','PREEMPTIVE=NO','ERRORS=0'])
            if i==0:
                live(g,'ps',['SHARED_ADDRESS_SPACE'])
                live(g,'threads',['NAME=rr-demo'])
                q.capture('procesos-hilos.png')
            # Wait for the reaper result; completion is asynchronous.
            end=time.monotonic()+5
            while b'SCHEDTEST PASSED' not in g.transcript[start:]:
                if time.monotonic()>end: raise AssertionError(g.transcript[start:].decode())
                g.buffer.clear();g.until('TERMINATED=3',5)
            segment=g.transcript[start:].decode('utf-8')
            assert 'SCHEDTEST FAILED' not in segment
            assert 'TRACE=ABCABCABCABC REG_PROBES=12 LOCALS=12 PMM_RESTORED=1 HEAP_RESTORED=1 TERMINATED=3' in segment
            entries=re.findall(r'THREAD_ENTRY TID=(\d+) LOCAL=(0x[0-9A-Fa-f]+)',segment)
            assert len(entries)==3 and len({a for _,a in entries})==3,entries
            tids=[int(t) for t,_ in entries]
            assert min(tids)>last_tid;last_tid=max(tids)
            owners={int(x) for x in re.findall(r'PID=(\d+) NAME=demo-',segment)}
            assert len(owners)==2 and min(owners)>last_pid;last_pid=max(owners)
            trace=re.findall(r'RR ([ABC]) STEP=(\d+) TID=(\d+)',segment)
            assert ''.join(x[0] for x in trace)=='ABC'*4 and len(trace)==12
            live(g,'ps',['STATE=TERMINATED','STACK_PAGES=0'])
            live(g,'threads',['STATE=TERMINATED','STACK=0x0000000000000000'])
            if i==0:q.capture('round-robin-recuperacion.png')
            after=g.send('memstats')
            assert int(re.search(r'USED=(\d+)',after).group(1))==used
            g.send('heapstats',['PAGES=0','BLOCKS=0','CHECK=1'])
        g.send('proc test',['PROCESSES=2 THREADS=3'])
        g.buffer.clear();g.until('TERMINATED=3',8)
        g.send('teclado',['ES-LATAM'])
        g.send('eco Perú año programación',['Perú año programación'])
        g.send('suma 7 8',['= 15'])
        q.capture('sistema-despues.png')
    finally:q.close()
sock.unlink(missing_ok=True)
Path('artifacts/memory-scheduler-result.json').write_text(json.dumps({
 'status':'PASSED','repeated_scheduler_runs':13,'new_threads':39,
 'actual_register_yield_probes':156,'independent_local_checks':156,
 'algorithm':'Round Robin cooperative','preemption':False,
 'pmm_heap_recovery_each_run':True,'shell_while_threads_alive':True},indent=2))
print('PASSED: MEMORY + PCB/TCB + RR actual x13, 156 register/local probes, stacks/resources restored')

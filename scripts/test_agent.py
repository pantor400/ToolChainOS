#!/usr/bin/env python3
"""Agente de bajo nivel: lista blanca del kernel y ejecucion real de ordenes.

No usa modelo: envia ordenes fijas para verificar el contrato TC/1 extendido
(tipos 4/5 y 0x84/0x85), el rechazo del kernel y la captura de salida.
"""
import queue, subprocess, sys, threading, time, uuid
from pathlib import Path
import ai_agent
from qemu_test import Guest

# 1. Espejo del cliente: lo que nunca debe salir del anfitrion.
assert ai_agent.allowed('pwd', False)
assert ai_agent.allowed('ls /', False)
assert ai_agent.allowed('open /x', False)
assert ai_agent.allowed('run cpp /programa', False)
assert not ai_agent.allowed('open', False)          # abriria entrada guiada
assert not ai_agent.allowed('halt', False)
assert not ai_agent.allowed('crear /a b', False)    # escritura bloqueada
assert ai_agent.allowed('crear /a b', True)
assert not ai_agent.allowed('ls; halt', False)
assert not ai_agent.allowed('x' * 200, False)
assert ai_agent.parse_reply('```\nls\n```') == ('cmd', 'ls')
assert ai_agent.parse_reply('FIN: todo listo') == ('fin', 'todo listo')

def session(guest, sock, script, steps, name):
    process = subprocess.Popen([sys.executable, 'scripts/ai_agent.py', '--attach',
                                '--socket', str(sock), '--script', script,
                                '--steps', str(steps), '--log', f'artifacts/{name}.log'],
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(line) for line in process.stdout],
                     daemon=True).start()
    text = ''
    deadline = time.monotonic() + 25
    while 'AGENTE LISTO' not in text:
        try:
            line = lines.get(timeout=max(.1, deadline - time.monotonic()))
        except queue.Empty:
            raise TimeoutError('ai_agent.py no conecto a COM2:\n' + text)
        text += line
    guest.q.stdin.write(b'agente inspecciona el sistema\n')
    guest.q.stdin.flush()
    process.wait(timeout=40)
    while not lines.empty():
        text += lines.get()
    if process.returncode:
        raise RuntimeError(f'ai_agent.py salio con {process.returncode}:\n{text}')
    return text

def run(name, script, steps, prepare=None, expected=()):
    sock = Path(f'build/ai-agent-{uuid.uuid4().hex[:8]}.sock')
    with Guest(name, ['-chardev', f'socket,id=ai,path={sock},server=on,wait=off',
                      '-serial', 'chardev:ai']) as g:
        if prepare:
            prepare(g)
        text = session(g, sock, script, steps, name)
        g.until('AGENTE FIN', 20)
        for item in expected:
            assert item in text, f'{item!r} no aparece en:\n{text}'
        # La sesion termino sin dejar la shell en modo guiado.
        g.send('suma 2 3', ['= 5'])
        return g.transcript.decode('utf-8', 'replace')

def enable_writes(g):
    g.send('agente escritura on', ['ESCRITURA HABILITADA'])

# 2. Solo lectura: `halt` no esta en la lista y no debe detener la maquina.
read_only = run('ai-agent-lock', 'pwd\nls\nhalt\nmem', 4,
                expected=['DIRECTORIO ACTUAL', 'FUERA DE LISTA BLANCA: halt',
                          'RAM MARCADA USABLE'])
assert 'AGENTE INICIADO' in read_only and 'AGENTE FIN' in read_only
assert 'AGENTE> ' in read_only
assert 'CPU DETENIDA' not in read_only

# 3. Escritura explicita: crear y leer un archivo desde la sesion del agente.
written = run('ai-agent-write', 'crear /nota hola agente\nopen /nota', 2,
              prepare=enable_writes,
              expected=['ARCHIVO GUARDADO CON EXITO', 'hola agente'])
assert 'ESCRITURA HABILITADA' in written

Path('artifacts').mkdir(exist_ok=True)
Path('artifacts/ai-agent-result.json').write_text(
    '{"protocolo":"TC/1 tipos 4/5 y 0x84/0x85","lista_blanca":"kernel y anfitrion",'
    '"lectura":"pwd ls mem","rechazo":"halt","escritura":"crear+open","resultado":"PASSED"}\n')
print('PASSED: sesion TC/1 del agente, lista blanca en kernel y anfitrion, '
      'rechazo de halt sin detener la VM, captura de salida y escritura explicita')

#!/usr/bin/env python3
"""Agente de bajo nivel: la IA ejecuta ordenes en la consola de ToolChainOS.

Flujo: el operador arranca `AGENTE <objetivo>` en el kernel; el kernel abre
una sesion TC/1 (tipo 4) por COM2; este programa pide una orden al modelo, la
envia (0x84) y devuelve al kernel la observacion real (tipo 5). El kernel es
quien valida cada orden con su lista blanca: aqui solo se filtra lo evidente.

Uso:
  python3 scripts/ai_agent.py --goal "revisa el sistema"   # arranca la VM
  python3 scripts/ai_agent.py --attach --socket build/ai.sock --script "pwd\\nls"
"""
import argparse, os, socket, sys, time, uuid
from pathlib import Path
from ai_bridge import Decoder, Ollama, frame

# Espejo de la lista blanca del kernel (agent.c): prefijo, exige argumento.
READ = [('ls', 0), ('dir', 0), ('tree', 0), ('pwd', 0), ('cd', 1), ('open', 1),
        ('info', 0), ('mem', 0), ('memstats', 0), ('memmap', 0), ('heapstats', 0),
        ('uptime', 0), ('ps', 0), ('threads', 0), ('sched', 0), ('tasks', 0),
        ('fsstatus', 0), ('disk info', 0), ('disk read', 1), ('disk mbr', 0),
        ('help', 0), ('teclado', 0), ('color', 1), ('fondo', 1),
        ('suma', 1), ('eco', 1), ('texto', 1), ('run cpp', 1), ('run_cpp', 1)]
WRITE = [('crear', 1), ('touch', 1), ('mkdir', 1), ('crear_carpeta', 1),
         ('rm', 1), ('rmdir', 1), ('ren', 1), ('copy', 1)]

def catalog(write):
    names = [prefix.upper() for prefix, _ in READ]
    if write:
        names += [prefix.upper() for prefix, _ in WRITE]
    return ', '.join(names)

def allowed(command, write):
    text = command.strip().lower()
    if not text or len(text.encode()) > 160:
        return False
    if any(ch in text for ch in ';|&`$<>'):
        return False
    for prefix, needs in READ + (WRITE if write else []):
        if text == prefix or text.startswith(prefix + ' '):
            return bool(text[len(prefix):].strip()) or not needs
    return False

def parse_reply(text):
    for raw in text.splitlines():
        line = raw.strip().strip('`').strip()
        if not line:
            continue
        if line.upper().startswith('FIN'):
            return ('fin', line.partition(':')[2].strip() or 'objetivo completado')
        return ('cmd', line)
    return ('fin', 'el modelo no propuso ninguna orden')

def prompt_for(goal, history, write):
    lines = [f'Historial (maximo 6):']
    for command, output in history[-6:]:
        lines.append('> ' + command)
        lines.append('  ' + output.strip()[:600].replace('\n', '\n  '))
    if not history:
        lines.append('(sin observaciones todavia)')
    return ('Eres el agente de bajo nivel de ToolChainOS, un kernel x86-64. '
            'Decides la siguiente orden de consola para cumplir el objetivo.\n'
            f'OBJETIVO: {goal}\n' + '\n'.join(lines) +
            '\nOrdenes permitidas: ' + catalog(write) +
            '\nResponde con UNA sola linea: o una orden permitida, o "FIN: resumen".'
            '\nNo expliques nada, no uses bloques de codigo.')

def recv_frame(sock, decoder, timeout):
    end = time.monotonic() + timeout
    while True:
        frames = decoder.feed(b'')
        if frames:
            return frames[0]
        remaining = end - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('sin marco TC/1 del kernel')
        sock.settimeout(min(remaining, 1.0))
        try:
            chunk = sock.recv(256)
        except socket.timeout:
            continue
        if not chunk:
            raise ConnectionError('COM2 cerrado')
        frames = decoder.feed(chunk)
        if frames:
            return frames[0]

def send(sock, kind, request, body):
    packet = frame(kind, request, body)
    for pos in range(0, len(packet), 8):
        sock.sendall(packet[pos:pos + 8])
        time.sleep(.003)

def run_session(sock, log, args):
    decoder = Decoder()
    kind, sid, body = recv_frame(sock, decoder, args.wait)
    if kind != 4:
        raise SystemExit(f'se esperaba tipo 4 (inicio de agente) y llego {kind}')
    goal = body.decode('utf-8', 'replace')
    print(f'SESION id={sid} OBJETIVO: {goal}', flush=True)
    log.write(f'SESION id={sid} OBJETIVO: {goal}\n')
    model = None
    if not args.script:
        model = Ollama(args.url, args.model)
        print(model.ready(), flush=True)
    script = [s for s in (args.script.splitlines() if args.script else []) if s.strip()]
    history = []
    for step in range(1, args.steps + 1):
        if script:
            command = script.pop(0)
        else:
            answer = model.generate(prompt_for(goal, history, args.allow_write))
            action, command = parse_reply(answer)
            if action == 'fin':
                print(f'FIN: {command}', flush=True)
                send(sock, 0x85, sid, command[:600])
                return 0
            if not allowed(command, args.allow_write):
                history.append((command, 'RECHAZADO EN EL ANFITRION: fuera de lista blanca.'))
                print(f'{step}. {command} -> rechazado por el cliente', flush=True)
                continue
        print(f'{step}. AGENTE> {command}', flush=True)
        send(sock, 0x84, sid, command)
        kind, _, observation = recv_frame(sock, decoder, args.wait)
        if kind != 5:
            raise SystemExit(f'se esperaba observacion (5) y llego {kind}')
        text = observation.decode('utf-8', 'replace')
        print(text, flush=True)
        log.write(f'>{command}\n{text}\n')
        history.append((command, text))
    send(sock, 0x85, sid, f'limite de {args.steps} pasos alcanzado')
    return 0

def connect(path, timeout=15):
    end = time.monotonic() + timeout
    while True:
        try:
            client = socket.socket(socket.AF_UNIX)
            client.connect(path)
            return client
        except OSError:
            if time.monotonic() > end:
                raise
            time.sleep(.1)

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--goal', default='Inspecciona el sistema y explica que encontraste')
    p.add_argument('--script', default='', help='ordenes fijas separadas por salto de linea')
    p.add_argument('--steps', type=int, default=6)
    p.add_argument('--model', default=os.getenv('OLLAMA_MODEL', ''))
    p.add_argument('--url', default=os.getenv('OLLAMA_URL', 'http://127.0.0.1:11434'))
    p.add_argument('--allow-write', action='store_true')
    p.add_argument('--attach', action='store_true', help='usar una VM ya arrancada')
    p.add_argument('--socket', default='')
    p.add_argument('--wait', type=float, default=60.0)
    p.add_argument('--log', default='artifacts/ai-agent.log')
    args = p.parse_args()
    args.steps = max(1, min(args.steps, 8))
    tag = uuid.uuid4().hex[:8]
    socket_path = args.socket or f'build/ai-agent-{tag}.sock'
    Path('artifacts').mkdir(exist_ok=True)
    log = Path(args.log).open('w')
    guest = None
    try:
        if not args.attach:
            from qemu_test import Guest
            guest = Guest('ai-agent', ['-chardev', f'socket,id=ai,path={socket_path},server=on,wait=off',
                                       '-serial', 'chardev:ai']).__enter__()
            print('VM ARRANCADA', flush=True)
        client = connect(socket_path)
        print('COM2 CONECTADO', flush=True)
        if guest:
            if args.allow_write:
                guest.q.stdin.write(b'agente escritura on\n'); guest.q.stdin.flush()
                guest.until('ESCRITURA HABILITADA', 10)
            guest.q.stdin.write(('agente ' + args.goal + '\n').encode()); guest.q.stdin.flush()
            guest.until('AGENTE INICIADO', 15)
        else:
            print('AGENTE LISTO; ESPERANDO SESION', flush=True)
        code = run_session(client, log, args)
        if guest:
            guest.until('AGENTE FIN', 15)
            print(guest.buffer.decode('utf-8', 'replace'))
        return code
    finally:
        log.close()
        if guest:
            guest.close()
        elif not args.socket:
            Path(socket_path).unlink(missing_ok=True)

if __name__ == '__main__':
    sys.exit(main())

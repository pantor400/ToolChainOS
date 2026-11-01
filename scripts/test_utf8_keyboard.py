#!/usr/bin/env python3
"""Serial UTF-8 and real emulated PS/2; sequential VMs, no physical host changes."""
import time,uuid
from pathlib import Path
from qemu_test import Guest
from qmp_test import QMP
count=0
def raw(g,data,expected):
    global count
    g.buffer.clear()
    for i in range(0,len(data),32):
        g.q.stdin.write(data[i:i+32]);g.q.stdin.flush();time.sleep(.005)
    text=g.until('OS> ')
    # Assert strict UTF-8; do not hide replacement errors in the test decoder.
    text=g.buffer.decode('utf-8')
    for item in expected:assert item in text,(item,text)
    count+=1;return text
def physical_echo(g,q,keys,expected):
    g.buffer.clear();q.ascii('eco ')
    for item in keys:q.key(*(item if isinstance(item,tuple) else (item,)))
    q.key('ret');text=g.until()
    assert ('ESCRIBISTE: '+expected) in text,text
def boot_layout(choice):
    path=Path('build/keyboard-'+uuid.uuid4().hex[:8]+'.sock')
    g=Guest('keyboard-boot-'+str(choice),['-qmp',f'unix:{path},server=on,wait=off'])
    q=None
    try:
        g.until('3 segundos.');time.sleep(.03);q=QMP(path)
        q.capture('seleccion-teclado.png')
        q.key(str(choice));q.key('ret');text=g.until()
        assert ('TECLADO: '+('ES-LATAM (PERU)' if choice==1 else 'US')) in text,text
        g.send('teclado',['TECLADO: '+('ES-LATAM' if choice==1 else 'US')])
        if choice==1:
            physical_echo(g,q,['a','semicolon','o'],'año')
            physical_echo(g,q,[('shift','p'),'e','r','bracket_left','u'],'Perú')
            physical_echo(g,q,['bracket_left','a',('shift','semicolon'),
                ('shift','bracket_left'),'u','equal',('shift','equal'),('shift','minus'),('shift','1')],'áÑü¿¡?!')
            physical_echo(g,q,[('alt_r','q')],'@')
            physical_echo(g,q,['semicolon','backspace','a'],'a')
            g.send('teclado us',['TECLADO: US'])
            physical_echo(g,q,['semicolon'],';')
            g.send('teclado es',['TECLADO: ES-LATAM'])
            physical_echo(g,q,['semicolon'],'ñ')
            g.send('limpiar')
            sample='á é í ó ú Á É Í Ó Ú ñ Ñ ü Ü ¿Qué? ¡Perú!'
            g.send('eco '+sample,['ESCRIBISTE: '+sample])
            q.capture('espanol-framebuffer.png')
            g.send('color rojo',['COLOR DE TEXTO ACTUALIZADO'])
            q.capture('espanol-redibujado.png')
        else:
            physical_echo(g,q,['semicolon',('shift','semicolon'),'bracket_left',('shift','3')],';:[#')
            # Serial text is already encoded by the host and never remapped by TECLADO.
            g.send('eco año Perú programación',['ESCRIBISTE: año Perú programación'])
        return True
    finally:
        if q:q.close()
        g.close();path.unlink(missing_ok=True)
boot_layout(1);boot_layout(2)
print('PASSED: seleccion ES y US con PS/2 real emulado, cambio TECLADO, deadkeys, AltGr y capturas')
start=time.monotonic()
with Guest('utf8-serial-default') as g:
    assert 'TECLADO: ES-LATAM' in g.transcript.decode('utf-8')
    assert time.monotonic()-start<15
    for sample in ['año','Perú','programación','¿Qué es un sistema operativo?',
                   'áéíóú ÁÉÍÓÚ ñÑ üÜ ¡Hola!','€ “texto” — …','Ω 汉字 🙂']:
        g.send('eco '+sample,['ESCRIBISTE: '+sample]);count+=1
    for data,expected in [
        ('eco añ\bño\n'.encode(),['ESCRIBISTE: año']),
        ('eco Perú\bú\n'.encode(),['ESCRIBISTE: Perú']),
        ('eco 🙂\bX\n'.encode(),['ESCRIBISTE: X']),
        (b'eco \xc3\bX\n',['ESCRIBISTE: X']),
        (b'eco \xc0\xaf\n',['UTF-8 INVALIDO']),
        (b'eco \xed\xa0\x80\n',['UTF-8 INVALIDO']),
        (b'eco \xf4\x90\x80\x80\n',['UTF-8 INVALIDO']),
        (b'eco \xc3\n',['UTF-8 INVALIDO']),
    ]:raw(g,data,expected)
    g.send('crear año Perú y programación',['ARCHIVO GUARDADO'])
    g.send('open año',['Perú y programación'])
    g.send('edit año',marker='EDIT> ')
    g.send(':clear',marker='EDIT> ')
    g.send('¿Qué es un sistema operativo?',marker='EDIT> ')
    raw_text='programacióx\bn'
    # Editor input Backspace uses the same scalar-aware line buffer.
    g.buffer.clear();g.q.stdin.write((raw_text+'\n').encode());g.q.stdin.flush()
    g.until('EDIT> ')
    g.buffer.clear();g.q.stdin.write('niñ\bÑ\n'.encode());g.q.stdin.flush();g.until('EDIT> ')
    g.send(':wq',['ARCHIVO GUARDADO'])
    g.send('open año',['¿Qué es un sistema operativo?','programación','niÑ'])
    g.send('edit año',marker='EDIT> ');g.send(':clear',marker='EDIT> ');g.send(':q',['CANCELADA'])
    g.send('open año',['programación'])
    g.send('edit límite',marker='EDIT> ');g.send('á'*511,marker='EDIT> ')
    g.send('ñ',['LIMITE 1023'],marker='EDIT> ');g.send(':wq',['ARCHIVO GUARDADO'])
    g.send('ls',['límite (1023 B)'])
    raw(g,('eco '+'á'*575+'\n').encode(),['LINEA DEMASIADO LARGA'])
    g.send('suma 1 2',['= 3'])
print(f'PASSED: opcion por defecto sin bloqueo; {count} casos seriales UTF-8, invalidez, Backspace, editor/FS y limite en bytes')

#!/usr/bin/env python3
"""Launch verified profiles; never attach a physical disk. No installation."""
import argparse,os,signal,stat,subprocess,sys
from pathlib import Path
from create_test_disk import create
import qemu_config
ROOT=Path(__file__).resolve().parents[1]
def image_path(value):
    path=Path(value)
    if not path.is_absolute():path=ROOT/path
    if path.parent.resolve()!=(ROOT/'build').resolve() or path.is_symlink():
        raise ValueError('La imagen debe estar directamente en build/, sin enlaces')
    if not path.exists():create(path)
    if not stat.S_ISREG(path.stat().st_mode):raise ValueError('Imagen no regular')
    if path.stat().st_size!=8*1024*1024:raise ValueError('Se requiere imagen de prueba de 8MiB')
    return path
def usb_path(value):
    """Pendrive de prueba: imagen cruda mayor que cero y multiplo de 512."""
    path=Path(value)
    if not path.is_absolute():path=ROOT/path
    if path.parent.resolve()!=(ROOT/'build').resolve() or path.is_symlink():
        raise ValueError('La imagen USB debe estar directamente en build/, sin enlaces')
    if not path.is_file():raise ValueError('Falta %s: crea la imagen antes de arrancar' % path)
    if not stat.S_ISREG(path.stat().st_mode):raise ValueError('Imagen USB no regular')
    size=path.stat().st_size
    if not size or size%512:raise ValueError('La imagen USB debe medir un multiplo de 512 bytes')
    return path
def stop(p):
    if p is None or p.poll() is not None:return
    p.terminate()
    try:p.wait(timeout=3)
    except subprocess.TimeoutExpired:p.kill();p.wait()
def main():
    p=argparse.ArgumentParser();p.add_argument('--ata',action='store_true');p.add_argument('--ai',action='store_true')
    p.add_argument('--headless',action='store_true');p.add_argument('--disk',default='build/data.img')
    p.add_argument('--fullscreen',action='store_true');p.add_argument('--no-net',action='store_true')
    p.add_argument('--linux-bin',action='store_true',help='ISO con BusyBox para la personalidad Linux (orden LINUX)')
    p.add_argument('--usb',metavar='IMAGEN',help='Pendrive de prueba en build/ (imagen cruda FAT)')
    p.add_argument('--print-command',action='store_true');a=p.parse_args()
    os.chdir(ROOT);os.umask(0o077)
    machine='pc' if a.ata else 'q35'
    if a.linux_bin:iso=qemu_config.ISO_BIN
    else:iso=qemu_config.ISO
    if not iso.is_absolute():iso=ROOT/iso
    if not iso.is_file():
        raise ValueError('Falta %s; ejecuta make %s'
                         % (iso.name,'linux-bin'))
    command=(qemu_config.base(machine=machine,memory='256M',iso=iso)
        + qemu_config.serial_stdio())
    if a.no_net:command+=['-nic','none']
    else:command+=qemu_config.net()
    if a.headless:command+=qemu_config.display('none')
    else:
        command+=qemu_config.display(os.environ.get('QEMU_DISPLAY','gtk,zoom-to-fit=off'))
        if a.fullscreen or os.environ.get('QEMU_FULLSCREEN')=='1':command+=['-full-screen']
    if a.ata:command+=qemu_config.disk(image_path(a.disk))
    if a.usb:command+=qemu_config.usb_controller()+qemu_config.usb_storage(usb_path(a.usb))
    sock=ROOT/'build/ai.sock'
    if a.ai:
        if sock.exists() or sock.is_symlink():raise ValueError('build/ai.sock ya existe; comprueba su propietario antes de retirarlo')
        command+=['-chardev','socket,id=ai,path=build/ai.sock,server=on,wait=off','-serial','chardev:ai']
    import shlex
    print(shlex.join(command),flush=True)
    if a.print_command:return
    q=None;bridge=None;log=None
    try:
        q=subprocess.Popen(command)
        if a.ai:
            import time
            end=time.monotonic()+3
            while not sock.exists():
                if q.poll() is not None:raise RuntimeError('QEMU no pudo crear socket')
                if time.monotonic()>end:raise RuntimeError('Timeout socket COM2')
                time.sleep(.02)
            Path('artifacts').mkdir(exist_ok=True);log=open('artifacts/ai-bridge-live.log','a')
            bridge=subprocess.Popen([sys.executable,'scripts/ai_bridge.py','--socket',str(sock)],stdout=log,stderr=log)
        q.wait()
    except KeyboardInterrupt:pass
    finally:
        stop(bridge);stop(q)
        if log:log.close()
        if a.ai and sock.exists() and stat.S_ISSOCK(sock.lstat().st_mode):sock.unlink()
if __name__=='__main__':
    try:main()
    except (ValueError,OSError,RuntimeError) as exc:raise SystemExit(str(exc))

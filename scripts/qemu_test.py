"""Bounded QEMU serial test harness. Every instance terminates its child."""
import os, select, subprocess, time
from pathlib import Path
import qemu_config
class Guest:
    def __init__(self, name, extra=(), machine='q35', iso='build/toolchain-os.iso'):
        self.name=name; self.buffer=bytearray(); self.transcript=bytearray()
        command=(qemu_config.base(machine=machine, iso=iso)
            + ['-nic','none'] + qemu_config.display('none') + qemu_config.serial_stdio()
            + list(extra))
        self.q=subprocess.Popen(command,
            stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    def until(self, marker='OS> ', timeout=35, start=0):
        end=time.monotonic()+timeout; needle=marker.encode()
        while needle not in self.buffer[start:]:
            if self.q.poll() is not None: raise RuntimeError(self.q.stderr.read().decode(errors='replace'))
            if time.monotonic()>end: raise TimeoutError(self.buffer.decode(errors='replace'))
            if select.select([self.q.stdout],[],[],.1)[0]:
                data=os.read(self.q.stdout.fileno(),4096)
                self.buffer.extend(data);self.transcript.extend(data)
        return self.buffer.decode(errors='replace')
    def send(self, command, expected=(), marker='OS> ', timeout=35, async_output=False):
        self.buffer.clear();data=command.encode()
        # UART input throttling also permits checking file-size boundaries.
        for i in range(0,len(data),32):
            self.q.stdin.write(data[i:i+32]);self.q.stdin.flush();time.sleep(.004)
        if not command.endswith('\n'): self.q.stdin.write(b'\n');self.q.stdin.flush()
        prefix=command[:24].rstrip('\n')
        if prefix and not async_output:
            self.until(prefix,timeout)
            start=self.buffer.find(prefix.encode())+len(prefix.encode())
        else:start=0
        text=self.until(marker,timeout,start)
        for item in expected:
            if item not in text: raise AssertionError(f'{command[:80]!r}: missing {item!r}\n{text}')
        return text
    def close(self):
        self.q.terminate()
        try:self.q.wait(timeout=3)
        except subprocess.TimeoutExpired:self.q.kill();self.q.wait()
        Path('artifacts').mkdir(exist_ok=True)
        Path('artifacts/'+self.name+'.serial.txt').write_bytes(self.transcript)
    def __enter__(self):
        try:self.until();return self
        except BaseException:self.close();raise
    def __exit__(self,*args):self.close()

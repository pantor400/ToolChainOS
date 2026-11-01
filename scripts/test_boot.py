#!/usr/bin/env python3
"""Single conservative BIOS boot gate before subsequent integration tests."""
from qemu_test import Guest
with Guest('final-first-boot') as guest:
    assert b'KERNEL_READY' in guest.transcript
    guest.send('info',['PAGINACION: ACTIVA','KERNEL FREESTANDING'])
    guest.send('suma 25 -8',['= 17'])
print('PASSED: primer arranque BIOS, TCG, VGA std, display none, 256MiB, sin disco ni red')

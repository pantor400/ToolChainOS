#!/usr/bin/env python3
import shutil,uuid
from pathlib import Path
from qemu_test import Guest
code=Path('/usr/share/OVMF/OVMF_CODE_4M.fd')
template=Path('/usr/share/OVMF/OVMF_VARS_4M.fd')
if not code.exists() or not template.exists():
    print('SKIPPED: OVMF no disponible');raise SystemExit(77)
variables=Path('build/ovmf-test-'+uuid.uuid4().hex[:8]+'.fd')
shutil.copyfile(template,variables)
try:
    with Guest('uefi',['-drive',f'if=pflash,format=raw,readonly=on,file={code}',
                      '-drive',f'if=pflash,format=raw,file={variables}']) as g:
        g.send('info',['PAGINACION: ACTIVA'])
        g.send('uptime',['HZ=100'])
        g.send('pmmtest',['PMMTEST PASSED'],timeout=60)
        g.send('suma 25 -8',['= 17'])
    print('PASSED: UEFI/OVMF arranque, PMM, timer y shell')
finally:variables.unlink(missing_ok=True)

@echo off
setlocal
set "QEMU_BIN=qemu-system-x86_64.exe"
where qemu-system-x86_64.exe >nul 2>nul
if errorlevel 1 (
    set "QEMU_BIN=C:\Program Files\qemu\qemu-system-x86_64.exe"
    if not exist "C:\Program Files\qemu\qemu-system-x86_64.exe" (
        echo No encuentro QEMU. Instala QEMU o agregalo al PATH.
        pause
        exit /b 1
    )
)
if not exist "%~dp0build\toolchain-os.iso" (
    echo Primero compila build/toolchain-os.iso mediante Ubuntu WSL. Consulta README.md.
    exit /b 1
)
"%QEMU_BIN%" -machine q35 -vga std -m 256M -smp 2 -cdrom "%~dp0build\toolchain-os.iso" -boot d -serial stdio -monitor none -no-reboot -no-shutdown
pause

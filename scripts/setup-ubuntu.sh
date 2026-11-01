#!/usr/bin/env bash
set -euo pipefail
[[ "$(uname -m)" == x86_64 ]] || { echo 'Se requiere Linux x86-64'; exit 1; }
packages=(build-essential binutils nasm xorriso qemu-system-x86 gdb ovmf git python3)
missing=()
for package in "${packages[@]}"; do
    dpkg-query -W -f='${Status}' "$package" 2>/dev/null | grep -q 'install ok installed' || missing+=("$package")
done
if (( ${#missing[@]} == 0 )); then echo 'Dependencias ya instaladas.'; exit 0; fi
printf 'Paquetes pendientes: %s\n' "${missing[*]}"
if [[ "${1:-}" != --install ]]; then
    echo 'Para instalar explícitamente: bash scripts/setup-ubuntu.sh --install'
    exit 0
fi
sudo apt-get update
sudo apt-get install -y "${missing[@]}"

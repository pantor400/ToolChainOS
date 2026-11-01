#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
make iso
exec python3 scripts/run_vm.py "$@"

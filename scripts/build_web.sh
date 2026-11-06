#!/bin/sh
# Uses project-local extracted Pillow when present; never installs system deps.
set -eu
cd "$(dirname "$0")/.."
if [ -d build/deps/usr/lib/python3/dist-packages ]; then
    export PYTHONPATH="$PWD/build/deps/usr/lib/python3/dist-packages${PYTHONPATH:+:$PYTHONPATH}"
    export LD_LIBRARY_PATH="$PWD/build/deps/usr/lib/x86_64-linux-gnu${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
[ "$#" -gt 0 ] || set -- iso
exec make "$@"

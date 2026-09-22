#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$root/tests/kernel/test_tty_device_read.py"
python3 "$root/tests/kernel/test_shell_read.py"

#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$root/tests/kernel/test_console_boot.py"
python3 -B "$root/tests/kernel/test_panic_output.py"
python3 -B "$root/tests/kernel/test_launch_trace.py"

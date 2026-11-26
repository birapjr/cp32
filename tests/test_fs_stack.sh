#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 -B "$root/tests/kernel/test_fs_stack.py"
python3 -B "$root/tests/kernel/test_stack_budgets.py"

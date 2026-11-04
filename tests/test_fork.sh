#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$root/tests/kernel/test_fork_broker.py"
python3 "$root/tests/kernel/test_fork_api.py"

python3 "$root/tests/kernel/test_wait_app.py"

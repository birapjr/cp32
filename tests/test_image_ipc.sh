#!/bin/sh
set -eu
python3 "$(dirname "$0")/kernel/test_image_ipc.py"
python3 -B "$(dirname "$0")/kernel/test_fork_ipc.py"

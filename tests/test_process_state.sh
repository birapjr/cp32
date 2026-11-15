#!/bin/sh
set -eu
python3 "$(dirname "$0")/kernel/test_process_state.py"

#!/bin/sh
set -eu
python3 "$(dirname "$0")/kernel/test_runtime_signals.py"

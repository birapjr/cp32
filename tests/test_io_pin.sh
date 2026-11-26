#!/bin/sh
set -eu
python3 "$(dirname "$0")/kernel/test_io_pin.py"

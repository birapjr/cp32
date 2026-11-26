#!/bin/sh
set -eu
python3 "$(dirname "$0")/kernel/test_fs_owner_app.py"

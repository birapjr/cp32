#!/bin/sh
set -eu
python3 "$(dirname "$0")/mm/test_process_runtime.py"
python3 "$(dirname "$0")/mm/test_image_copy.py"
python3 "$(dirname "$0")/kernel/test_runtime_fault.py"

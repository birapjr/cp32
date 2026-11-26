#!/bin/sh
set -eu
python3 "$(dirname "$0")/kernel/test_irq_stack_app.py"

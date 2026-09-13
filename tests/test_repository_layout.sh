#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

test -f "$root/agent.md"
test -f "$root/src/Makefile"
test -d "$root/src/kernel"
test -d "$root/src/include"

# Keep the firmware build independent from the host test runner.
grep -q -- '-ffreestanding' "$root/src/Makefile"
grep -q -- '-nostdlib' "$root/src/Makefile"
grep -q '^tests:' "$root/src/Makefile"

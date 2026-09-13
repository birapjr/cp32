#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cp32-irq.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -I"$root/src/include" -I"$root/src/kernel" \
  "$root/tests/test_irq_handoff_contract.c" -o "$tmp/test_irq_handoff_contract"
"$tmp/test_irq_handoff_contract"

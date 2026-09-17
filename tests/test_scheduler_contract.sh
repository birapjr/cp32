#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cp32-scheduler.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror "$root/tests/test_scheduler_contract.c" \
  -o "$tmp/test_scheduler_contract"
"$tmp/test_scheduler_contract"

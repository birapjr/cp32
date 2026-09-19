#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cp32-systimer.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror \
  "$root/tests/test_systimer.c" -o "$tmp/test_systimer"
"$tmp/test_systimer"

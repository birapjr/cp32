#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source="$root/src/kernel/klib.c"

# These functions are deliberately freestanding. Compile the source only;
# linking it as a hosted program would test the wrong ABI and runtime.
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cp32-tests.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

cc -std=c89 -Wall -Wextra -Werror -ffreestanding -fsyntax-only "$source"

# Verify the implementation contains the required primitives and does not
# accidentally delegate to a hosted libc implementation.
grep -q '^void \*memcpy' "$source"
grep -q '^void \*memset' "$source"
grep -q '^char \*strcpy' "$source"
grep -q '^int strcmp' "$source"
grep -q '^long strtol' "$source"
! grep -Eq '^[[:space:]]*(return[[:space:]]+)?(__builtin_)?(memcpy|memset|strcmp|strtol)[[:space:]]*\(' "$source"

#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
makefile="$root/src/Makefile"
for source in "$root"/src/kernel/*.c "$root"/src/lib/other/*.c; do
  grep -q "$(basename "$source")" "$makefile"
done
for source in "$root"/src/kernel/*.S; do
  grep -q "$(basename "$source" .S)" "$makefile"
done

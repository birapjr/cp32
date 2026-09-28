#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/src/mm" \
  "$root/tests/mm/test_image.c" "$root/src/mm/image.c" -o "$work/test"
"$work/test"
if [ -f "$root/src/build/hello.elf" ]; then
  "$work/test" "$root/src/build/hello.elf"
fi
cc -std=c99 -Wall -Wextra -Werror -I"$root/src/apps/hello" \
  "$root/tests/mm/test_hello.c" "$root/src/apps/hello/hello.c" -o "$work/hello"
"$work/hello"

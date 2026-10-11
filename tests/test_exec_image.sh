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
if [ -f "$root/src/build/echo.elf" ]; then
  "$work/test" "$root/src/build/echo.elf"
fi
if [ -f "$root/src/build/wc.elf" ]; then "$work/test" "$root/src/build/wc.elf"; fi
if [ -f "$root/src/build/ls.elf" ]; then "$work/test" "$root/src/build/ls.elf"; fi
if [ -f "$root/src/build/exec.elf" ]; then "$work/test" "$root/src/build/exec.elf"; fi
if [ -f "$root/src/build/cat.elf" ]; then "$work/test" "$root/src/build/cat.elf"; fi
cc -std=c99 -Wall -Wextra -Werror -I"$root/src/apps/hello" \
  "$root/tests/mm/test_hello.c" "$root/src/apps/hello/hello.c" "$root/src/apps/lib/heap.c" "$root/src/apps/lib/io.c" -o "$work/hello"
"$work/hello"
"$work/hello" errno
"$work/hello" line
"$work/hello" io
"$work/hello" read
"$work/hello" trim
"$work/hello" resize
"$work/hello" alloc
"$work/hello" malloc
"$work/hello" heap
"$work/hello" ppid
"$work/hello" pid
"$work/hello" env
"$work/hello" empty-env
for status in 0 7 255 256 65535; do "$work/hello" "$status"; done
for status in '' -1 +7 65536 999999999999999999999 7x; do
  "$work/hello" "$status" invalid
done
cc -std=c99 -Wall -Wextra -Werror -I"$root/src/apps/hello" \
  "$root/tests/mm/test_echo.c" "$root/src/apps/echo/echo.c" -o "$work/echo"
"$work/echo"
"$work/echo" empty
"$work/echo" failure

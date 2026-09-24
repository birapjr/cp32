#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cc -std=c89 -Wall -Wextra -Werror -ffreestanding -fsyntax-only "$root/src/kernel/klib.c"
for name in memcpy memset strcpy strcmp strtol; do
    source="$root/src/lib/ansi/$name.c"
    test -f "$root/minix-2.0.0/src/lib/ansi/$name.c"
    cc -std=c89 -Wall -Wextra -Werror -ffreestanding -fsyntax-only "$source"
    ! grep -Eq '^[[:space:]]*(return[[:space:]]+)?(__builtin_)?(memcpy|memset|strcmp|strtol)[[:space:]]*\(' "$source"
done

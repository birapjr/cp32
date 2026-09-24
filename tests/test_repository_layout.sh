#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

test -f "$root/AGENTS.md"
test -f "$root/src/Makefile"
test -d "$root/src/kernel"
test -d "$root/src/include"

# Keep the firmware build independent from the host test runner.
grep -q -- '-ffreestanding' "$root/src/Makefile"
grep -q -- '-nostdlib' "$root/src/Makefile"
grep -q '^tests:' "$root/src/Makefile"

# Keep portable FS responsibilities alongside the same MINIX reference files.
for name in misc.c filedes.c cache.c stadir.c super.c inode.c path.c open.c read.c utility.c fs.h const.h type.h super.h inode.h file.h proto.h; do
  test -f "$root/src/fs/$name"
  test -f "$root/minix-2.0.0/src/fs/$name"
done
test -f "$root/tests/fs/test_super.py"
test -f "$root/tests/fs/test_path.py"
test ! -e "$root/src/kernel/minix-dir.c"
test ! -e "$root/src/kernel/minix-super.c"
for name in main.c alloc.c mm.h proto.h; do
  test -f "$root/src/mm/$name"
  test -f "$root/minix-2.0.0/src/mm/$name"
done
test -f "$root/tests/mm/test_main.py"
test ! -e "$root/src/kernel/mm.c"

#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 -B "$root/tests/fs/test_fsck.py"
for image in "$root/src/build/minix-demo.img" "$root/src/build/sdcard.img"; do
  if [ -f "$image" ]; then python3 -B "$root/tools/fsck_minix.py" "$image"; fi
done

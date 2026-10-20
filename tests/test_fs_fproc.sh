#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 -B - "$root" "$work/disk" <<'PY'
from pathlib import Path
import sys
sys.path.insert(0,sys.argv[1]+'/tools')
from make_minix_demo import build_image
Path(sys.argv[2]).write_bytes(build_image(True))
PY
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/src/fs" -I"$root/src/apps/hello" \
 "$root/tests/fs/test_fproc.c" "$root"/src/fs/*.c -o "$work/test"
"$work/test" "$work/disk"

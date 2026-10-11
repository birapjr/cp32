#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 -B - "$root" "$work/volume" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0,sys.argv[1]+'/tools')
from make_minix_demo import build_image
Path(sys.argv[2]).write_bytes(build_image())
PY
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
 -I"$root/src/fs" "$root/tests/fs/test_create.c" "$root"/src/fs/*.c -o "$work/test"
"$work/test" "$work/volume"

cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
 -I"$root/src/fs" "$root/tests/fs/test_append.c" "$root"/src/fs/*.c -o "$work/append"
"$work/append" "$work/volume"

#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 -B - "$root" "$work/disk" <<'PY'
import sys
from pathlib import Path
sys.path.insert(0,sys.argv[1]+'/tools')
from make_minix_demo import build_image
Path(sys.argv[2]).write_bytes(build_image(True))
PY
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -I"$root/src/kernel" \
 "$root/tests/kernel/test_app_update.c" "$root/src/kernel/app-update.c" \
 "$root"/src/fs/*.c -o "$work/test"
"$work/test" "$work/disk"

# When cross-built applications exist, install the actual generated manifest.
if [ -f "$root/src/build/hello.elf" ]; then
 python3 "$root/tools/embed_apps.py" "$root/src/build" "$work/app-payload.h"
 cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -DTEST_PAYLOAD \
  -I"$work" -I"$root/src/kernel" "$root/tests/kernel/test_app_update.c" \
  "$root/src/kernel/app-update.c" "$root"/src/fs/*.c -o "$work/payload"
 "$work/payload" "$work/disk"
fi
python3 -B - "$root" <<'PY'
from pathlib import Path
import shutil,subprocess,sys
root=Path(sys.argv[1]);nm=shutil.which('xtensa-esp32s3-elf-nm')
if nm:
 for folder,updater in [('build',False),('build-updater',True)]:
  elf=root/'src'/folder/'cp32.elf'
  if not elf.exists():continue
  symbols={x.split()[-1]:int(x.split()[0],16) for x in subprocess.check_output([nm,'-n',str(elf)],text=True).splitlines() if len(x.split())==3}
  assert ('cp32_update_one' in symbols)==updater
  assert ('payload_hello' in symbols)==updater
  assert symbols['_heap_end']-symbols['_heap_start']==(160 if updater else 192)*1024
  assert symbols['_runtime_stack_end']<=0x3fce8000
  if updater:assert 0x40378000<=symbols['cp32_update_one']<symbols['_iram_ext_end']
print('updater build isolation, heap budget and internal placement pass')
PY

#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 -B - "$root" <<'PY'
from pathlib import Path
import re,shutil,subprocess,sys
root=Path(sys.argv[1]);objdump=shutil.which('xtensa-esp32s3-elf-objdump')
if not objdump:
 print('MM build check: cross-binutils unavailable; skipped');raise SystemExit
checked=0
for folder in ('build','build-updater','build-ram','build-sd-soft','build-sd-soft-updater'):
 obj=root/'src'/folder/'mm/main.o'
 if not obj.exists():continue
 text=subprocess.check_output([objdump,'-d',str(obj)],text=True)
 body=text.split('<mm_task>:',1)[1].split('\n\n',1)[0]
 # Preserve the previously hardware-tested task compilation until optimized
 # first-receive resumption is independently validated. Not a general ABI rule.
 assert re.search(r's32i(?:\.n)?\s+a15, a1,',body),folder
 assert re.search(r'(?:or\s+a15, a1, a1|mov\.n\s+a15, a1)',body),folder
 assert '<cp32_trace_mm_receive>:' in text,folder
 checked+=1
print(f'MM task build: preserved frame/trace boundaries in {checked} variants')
PY

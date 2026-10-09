#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
 -I"$root/src/drivers" "$root/tests/drivers/test_sd_spi.c" "$root/src/drivers/sd_spi.c" -o "$work/protocol"
"$work/protocol"
disk="$root/src/build/sdcard.img"
if [ ! -f "$disk" ]; then
 python3 "$root/tests/drivers/make_sd_fixture.py" "$work/disk.img"
 disk="$work/disk.img"
fi
 cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$root/src/kernel" "$root/tests/kernel/test_rootdisk.c" "$root/src/kernel/rootdisk.c" \
  "$root"/src/fs/*.c "$root/src/mm/image.c" -o "$work/root"
 "$work/root" "$disk"
python3 "$root/tests/kernel/test_storage_request.py"
python3 "$root/tests/drivers/test_sd_build.py"

#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
 -I"$root/src/drivers" "$root/tests/drivers/test_sd_spi.c" "$root/src/drivers/sd_spi.c" -o "$work/protocol"
"$work/protocol"
# Use -idirafter so host libc headers are not shadowed by MINIX headers.
cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
 -I"$root/src/drivers" -idirafter "$root/src/include" \
 "$root/tests/drivers/test_spi_fifo.c" "$root/src/drivers/spi_fifo.c" -o "$work/fifo"
"$work/fifo"
# Fault-injection offsets below deliberately use the compact 64KiB fixture.
# The expanded production image is exercised by test_sd_image.sh.
disk="$work/disk.img"
if [ -f "$root/src/build/minix-demo.img" ]; then
 python3 "$root/tools/make_sd_image.py" "$root/src/build/minix-demo.img" "$disk"
else
 python3 "$root/tests/drivers/make_sd_fixture.py" "$disk"
fi
 cc -std=c99 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
  -I"$root/src/kernel" "$root/tests/kernel/test_rootdisk.c" "$root/src/kernel/rootdisk.c" \
  "$root"/src/fs/*.c "$root/src/mm/image.c" -o "$work/root"
 "$work/root" "$disk"
python3 "$root/tests/kernel/test_storage_request.py"
python3 "$root/tests/kernel/test_patch_command.py"
python3 "$root/tests/drivers/test_sd_build.py"

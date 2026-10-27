#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 -B "$root/tests/kernel/test_fs_stack.py"
python3 -B - "$root" <<'PY'
from pathlib import Path
import sys, re
root=Path(sys.argv[1])
# A real exec-open cache-miss path, including the IPC exception frame and
# message address translation. Callback edges are explicit because GCC cannot
# infer the disk reader target. This is a regression budget, not a whole-kernel
# maximum-stack proof. Leave 1024 bytes for other dispatch/interrupt branches.
chain='''cp32_tty_read_client cp32_shell_launch cp32_application_run
application_replace cp32_shell_exec_open cp32_fproc_exec_open cp32_fd_open_mode
cp32_fd_open cp32_fs_resolve_path cp32_fs_open_directory cp32_fs_allocated
cp32_shell_disk_read cp32_cache_read cp32_shell_disk_uncached cp32_shell_disk_io
_sendrec cp32_user_trap_dispatch sys_call mini_send deliver_blocked_message copy_message mem_copy numap'''.split()
# Include the deeper directory-replacement path and canonical cwd traversal.
common='cp32_shell_disk_read cp32_cache_read cp32_shell_disk_uncached cp32_shell_disk_io _sendrec cp32_user_trap_dispatch sys_call mini_send deliver_blocked_message copy_message mem_copy numap'.split()
app='cp32_tty_read_client cp32_shell_launch cp32_application_run application_file_request cp32_shell_app_file'.split()
mutation='cp32_minix_move remove_node mutation_location$part$0 cp32_fs_resolve_path cp32_fs_open_directory cp32_fs_allocated'.split()
paths={'exec-open':chain,
       'app-read-time':app+'cp32_fproc_file cp32_fd_read cp32_minix_file_read cp32_fs_data_time cp32_fs_stamp cp32_shell_time_now cp32_shell_clock _sendrec cp32_user_trap_dispatch sys_call mini_send deliver_blocked_message copy_message mem_copy numap'.split(),
       'app-utime':app+'cp32_fproc_utime cp32_fs_resolve_path cp32_fs_open_directory cp32_fs_allocated'.split()+common,
       'shell-touch':['cp32_tty_read_client','cp32_shell_touch','cp32_fproc_utime','cp32_fs_resolve_path','cp32_fs_open_directory','cp32_fs_allocated']+common,
       'app-rename':app+['cp32_fproc_move']+mutation+common,
       'shell-rename':['cp32_tty_read_client','cp32_shell_rename','cp32_fproc_move']+mutation+common,
       'rename-cwd':app+'cp32_fproc_move cp32_minix_chdir cp32_minix_dir_open cp32_fs_resolve_path cp32_fs_open_directory cp32_fs_allocated'.split()+common}
stack_bytes=int(re.search(r'#define CP32_FS_STACK_BYTES (\d+)U', (root/'src/kernel/const.h').read_text()).group(1))
assert stack_bytes==8192
checked=0
for folder in ('build','build-updater','build-ram','build-sd-soft','build-sd-soft-updater'):
    build=root/'src'/folder
    if not (build/'cp32.elf').exists(): continue
    frames={}
    for report in build.rglob('*.su'):
        for line in report.read_text().splitlines():
            name,size,kind=line.split('\t')
            frames[name.rsplit(':',1)[-1]]=int(size)
    assert frames, f'{folder}: clean rebuild required for stack-usage reports'
    assert frames['cp32_tty_read_client']<=256, f'{folder}: command buffers in persistent shell frame'
    for label,path in paths.items():
        if folder=='build-ram' and label!='exec-open':continue
        # RAM's read-only open wrapper may be fully inlined.
        names=[n for n in path if n!='cp32_fd_open_mode' or n in frames]
        missing=set(names)-frames.keys()
        assert not missing, (folder,missing)
        total=128+sum(frames[n] for n in names)
        assert total<=stack_bytes-1024, f'{folder}: {label} stack {total} exceeds budget'
        print(f'{folder}: {label} stack budget {total}/{stack_bytes-1024} bytes')
    checked+=1
print(f'FS stack regression: {checked} compiled variants checked')
PY

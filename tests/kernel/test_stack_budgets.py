"""Budget production FS/MM/exception call chains using GCC stack reports.

IPC crosses to the exception stack, so never add IRQ frames to the caller's
FS/MM stack. Callback edges are explicit. These targeted regression budgets
are not a whole-kernel maximum-stack proof or a bound on application handlers.
Missing helpers count as inline only with positive compiler DWARF evidence.
"""
from pathlib import Path
import os
import re
import subprocess

root = Path(__file__).resolve().parents[2]
def words(text):
    return text.split()

fs = words('cp32_tty_read_client cp32_shell_launch cp32_application_run')
app = fs + words('application_file_request cp32_shell_app_file')
disk = words('cp32_shell_disk_read cp32_cache_read cp32_shell_disk_uncached cp32_shell_disk_io _sendrec')
lookup = words('cp32_fs_resolve_path cp32_fs_open_directory cp32_fs_allocated')
timestamp = words('cp32_fs_inode_time cp32_fs_stamp cp32_shell_time_now cp32_shell_clock _sendrec')
mutation = words('cp32_minix_move remove_node mutation_location$part$0') + lookup
fs_paths = {
    'exec-open': fs + words('application_stage cp32_shell_exec_open cp32_fproc_exec_open cp32_fd_open_mode cp32_fd_open') + lookup + disk,
    'exec-load': fs + words('application_stage cp32_image_load cp32_image_read cp32_shell_app_read cp32_fd_read cp32_minix_file_read') + disk,
    'clone': fs + words('cp32_shell_clone_process cp32_fproc_clone cp32_fd_dup'),
    'exit': fs + words('cp32_shell_drop_process cp32_fproc_exit cp32_fd_close'),
    'terminal-poll': fs + words('cp32_shell_app_poll cp32_shell_flush cp32_shell_tty_io _sendrec'),
    'rename-time': app + words('cp32_fproc_move cp32_minix_move cp32_minix_rename directory_store') + timestamp,
    'readdir-time': app + words('cp32_fproc_file cp32_fd_readdir cp32_fs_data_time') + timestamp,
    'app-read-time': app + words('cp32_fproc_file cp32_fd_read cp32_minix_file_read cp32_fs_data_time') + timestamp,
    'app-utime': app + ['cp32_fproc_utime'] + lookup + disk,
    'shell-touch': words('cp32_tty_read_client cp32_shell_touch cp32_fproc_utime') + lookup + disk,
    'app-rename': app + ['cp32_fproc_move'] + mutation + disk,
    'shell-rename': words('cp32_tty_read_client cp32_shell_rename cp32_fproc_move') + mutation + disk,
    'rename-cwd': app + words('cp32_fproc_move cp32_minix_chdir cp32_minix_dir_open') + lookup + disk,
}
mm = words('mm_task cp32_runtime_dispatch')
mm_paths = {
    'launch-files': mm + words('launch cp32_shell_clone_launch cp32_fproc_clone cp32_fd_dup'),
    'fork-allocate': mm + words('fork_process process_allocate memset'),
    'fork-frame': mm + words('fork_process cp32_proc_clone memcpy'),
    'fork-files': mm + words('fork_process fs_request _sendrec'),
    'exec-stage': mm + words('exec_process fs_request _sendrec'),
    'exec-frame': mm + words('exec_process cp32_exec_frame memset'),
    'exit-files': mm + words('exit_process fs_request _sendrec'),
    'exit-reap': mm + words('exit_process wake_waiters wait_result cp32_proc_release'),
    'exit-wakeup': mm + words('exit_process wake_waiters cp32_runtime_reply _send'),
    'read-retry': mm + words('cp32_runtime_tick fs_request _sendrec'),
    'signal-interrupt': mm + words('cp32_runtime_signal_request signal_queue cp32_runtime_interrupt cp32_runtime_reply _send'),
    'signal-return': mm + words('cp32_runtime_signal_request lock_ready ready proc_is_ready_queued'),
}
# Explicit assembly stack reservations precede each C entry; final restore
# resets a1 to exception_stack_top-16 rather than retaining IRQ temporaries.
irq_paths = {
    'ipc-send': (128, words('cp32_task_ipc_dispatch sys_call mini_send deliver_blocked_message copy_message mem_copy numap cp32_mem_owned')),
    'ipc-notify': (128, words('cp32_task_ipc_dispatch sys_call mini_rec interrupt_message numap cp32_mem_owned')),
    'clock-notify': (96, words('cp32_irq_dispatch cp32_timer_irq_dispatch clock_handler interrupt interrupt_message numap cp32_mem_owned')),
    'clock-select': (96, words('cp32_irq_dispatch cp32_timer_irq_dispatch sched pick_proc cp32_runtime_can_run cp32_runtime_is_app cp32_runtime_state')),
    'image-return': (16, words('cp32_prepare_return cp32_runtime_activate copy_application_image')),
    'signal-return': (16, words('cp32_prepare_return cp32_runtime_signal_return cp32_exec_frame memset')),
    'app-fault': (16, words('cp32_application_fault cp32_exec_frame memset')),
}
inline_objects = {
    'application_stage': 'application.o',
    'application_file_request': 'application.o',
    'cp32_fd_open_mode': 'fs/filedes.o',
    'launch': 'mm/process.o',
    'fork_process': 'mm/process.o',
    'exit_process': 'mm/process.o',
    'wake_waiters': 'mm/process.o',
    'signal_queue': 'mm/signal.o',
    'clock_handler': 'clock.o',
}

def inline_functions(obj):
    dump = subprocess.check_output(['xtensa-esp32s3-elf-readelf', '--debug-dump=info', str(obj)], text=True)
    found = set()
    for entry in re.split(r'\n (?=<\d+><)', dump):
        if '(DW_TAG_subprogram)' not in entry.splitlines()[0]:
            continue
        if not re.search(r'DW_AT_inline\s*:\s*[13]\s', entry):
            continue
        name = re.search(r'DW_AT_name\s*:\s*(?:\(indirect string[^\n]*?\):\s*)?(\S+)\s*$', entry, re.M)
        if name:
            found.add(name.group(1))
    return found

stack_bytes = int(re.search(r'#define CP32_FS_STACK_BYTES (\d+)U', (root/'src/kernel/const.h').read_text()).group(1))
assert stack_bytes == 8192
checked = 0
folders = os.environ.get('CP32_STACK_VARIANTS', 'build build-updater build-ram build-sd-soft build-sd-soft-updater').split()
for folder in folders:
    build = root/'src'/folder
    if not (build/'cp32.elf').exists():
        continue
    frames, inline_cache = {}, {}
    for report in build.rglob('*.su'):
        for line in report.read_text().splitlines():
            name, size, kind = line.split('\t')
            frames[name.rsplit(':', 1)[-1]] = int(size)
    assert frames, f'{folder}: clean rebuild required for stack-usage reports'
    assert frames['cp32_tty_read_client'] <= 256, f'{folder}: command buffers in persistent shell frame'
    def total(path, entry_bytes=16):
        amount = entry_bytes
        for name in path:
            if name in frames:
                amount += frames[name]
                continue
            assert name in inline_objects, f'{folder}: missing stack report for {name}'
            obj = inline_objects[name]
            if obj not in inline_cache:
                inline_cache[obj] = inline_functions(build/obj)
            assert name in inline_cache[obj], f'{folder}: {name} has no frame and no compiler inline evidence'
        return amount
    groups = {
        'FS': (stack_bytes-1024, {name: total(path) for name, path in fs_paths.items()
                                if folder != 'build-ram' or name in ('exec-open', 'exec-load', 'clone', 'exit', 'terminal-poll')}),
        'MM': (4096-512, {name: total(path) for name, path in mm_paths.items()}),
        'IRQ': (16384-1024, {name: total(path, entry) for name, (entry, path) in irq_paths.items()}),
        'INIT': (4096-512, {'wait': total(words('cp32_init_task _sendrec'))}),
    }
    for owner, (budget, paths) in groups.items():
        label = max(paths, key=paths.get)
        assert paths[label] <= budget, f'{folder}: {owner} {label} stack {paths[label]} exceeds {budget}'
        print(f'{folder}: {owner} {len(paths)} paths; peak {label} {paths[label]}/{budget} bytes')
    checked += 1
print(f'FS/MM/IRQ/INIT stack regression: {checked} compiled variants checked')

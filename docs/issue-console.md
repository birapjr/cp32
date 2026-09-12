# USB output corruption investigation

## Known-good baseline

Commit `da98aacd26fe2541e94f464986e38a274ef26304` produces clean USB output.
The important baseline checks are:

```text
.data sentinel: 0xC032DA7A (expected 0xC032DA7A)
.bss sentinel:  0x00000000 (expected 0x00000000)
vector start:  0x40370000
vector end:    0x40370400
```

## Initial regression

The first bad commit was associated with the later user/trap-frame work:

```text
72f6cd7 cp32: guard blocked syscall frame restore
5d20980 cp32: add guarded user trap frame entry
```

The `irq_user` changes in `5d20980` were removed first, but corruption remained.
All experimental changes were eventually removed and the tree was restored to
`da98aacd`; output became clean again.

## Incremental reintroduction results

The following features were reintroduced incrementally from the clean baseline.
Each individual step built successfully and was hardware-tested clean:

- Five blocked-frame fields in `struct proc`.
- Syscall/user frame contract declarations in `irq_frame.h`.
- `cp32_user_frame_contract_valid()` helper.
- Blocked-frame mismatch counter.
- Blocked-frame restore eligibility helper.
- Blocked-frame completion helper.
- Completion call in `mini_send()` receiver wakeup path.
- Completion call in `mini_rec()` sender wakeup path.
- Blocked-frame snapshot in `sys_call()`.
- `blocked_handoff_eligible()` restore check.
- Counter reset in `cp32_prepare_two_task_stress()`.
- Synthetic blocked-frame initialization in `cp32_probe_blocked_handoff()`.
- Diagnostics V52, V53, V54, V55, V56, V57, and V58.
- IPC blocked-frame snapshot checks and `IPC V22`.

## Critical observation

The failure was reproducibly tied to one output call in `test_ipc_mm()`.
The relevant sequence is:

```c
usbj_print("[IPC V22 blocked-frame-wake pass=");
usbj_print_u32(receiver_frame_saved && sender_frame_saved);
usbj_print("]\r\n");
```

The final `usbj_print("]\r\n");` was the trigger. Removing only that call made
the corruption disappear. Replacing its contents with a shorter string, such
as `"\r\n"` or `"foo"`, did not help. Therefore the string contents are not
the cause; adding the call/instruction is the trigger.

Observed failure examples include:

```text
.data sentinel: 0x09920F98 (expected 0xC032DA7A)
.data sentinel: 0x1FA809A9 (expected 0xC032DA7A)
.data sentinel: 0x30C112A1 (expected 0xC032DA7A)
```

The corruption is visible very early, around the WDT dump / initialized-memory
output, while later messages may appear readable. This may be actual memory or
image corruption, or a corrupted USB stream making the sentinel line appear
wrong. It is not yet proven that the sentinel memory itself was overwritten.

## Experiments that did not fix it

- Restoring the original `irq_user` assembly handler.
- Removing all V52–V58 diagnostics.
- Moving diagnostics to a separate `diagnostics.c` translation unit.
- Restoring diagnostics inline in `main.c`.
- Adding a delay after the WDT dump.
- Replacing `status_line()` with direct `usbj_print()`.
- Reworking decimal/hex numeric printers.
- Waiting indefinitely for USB endpoint space.
- Buffering USB output in 64-byte chunks. This was reverted because the
  hardware definition says EP1 queues one byte and `WR_DONE` commits it.
- Explicit linker `PT_LOAD` segments and page-aligned LMA experiments.

## Current USB implementation

The USB printer is restored to the known-good baseline implementation:

- `usbj_putc()` waits with a bounded timeout.
- It writes one byte to `USBJ_EP1`.
- It strobes `USBJ_WR_DONE` for each byte.
- `usbj_print_u32()` and `usbj_print_hex32()` use small stack buffers.

Do not reintroduce the attempted multi-byte buffering without verifying the
ESP32-S3 USB Serial/JTAG FIFO semantics.

## Linker/image investigation

The custom linker script originally used:

```ld
} > iram AT > irom
} > dram AT > irom
```

Several explicit `PHDRS` and LMA alignment variants were tested. A build-time
checker was added temporarily as `tools/check_image_layout.py`; it verified:

- two `PT_LOAD` segments;
- 4 KiB segment alignment;
- page-aligned LMAs;
- the `.data` sentinel bytes appearing in the generated `.bin`.

The ELF and generated `.bin` contained the correct sentinel bytes even when
the device later printed a wrong value. This means image generation was not
proven to be the corruption point. The linker experiments should be reviewed
before being kept as a permanent fix.

## Current state

The latest tree contains the incremental blocked-frame work through IPC V22,
and the V22 closing `usbj_print()` call is currently the known regression
trigger. The source tree was not fully returned to the baseline after the last
incremental step.

Before continuing, inspect `git status` and decide whether to:

1. preserve the incremental feature state and isolate the call/code-generation
   trigger; or
2. restore all source files to `da98aacd` and restart with a cleaner test plan.

## Best next experiments

1. Keep V22 logic but remove the closing `usbj_print()` call; verify clean boot.
2. Move the entire V22 print sequence into a dedicated helper function and
   compare the result. This tests stack-frame/register-spill pressure in
   `test_ipc_mm()`.
3. Replace the call with an inline direct character emission only if the
   USB primitive is made available safely.
4. Record the actual address of `cp32_data_sentinel` and capture it into a
   `.bss` checkpoint before any USB output. Print the checkpoint later.
5. Compare ELF program headers, section offsets, and stack symbols between the
   clean and failing images.
6. Add a linker map and stack-usage check before adding more features.

No further source changes should be made until the next investigation session.

## Stable long-term mitigation: section garbage collection

The most successful solution so far is compiling C code into individual
function/data sections and enabling linker garbage collection:

```make
-ffunction-sections -fdata-sections
--gc-sections
```

This preserves live Xtensa functions with their normal literal layout while
removing unreachable code and data. It reduced the IRAM image from roughly
`0x7FA4` bytes to `0x531C` bytes. Hardware testing remained clean with the
additional USB output and the `.data` sentinel stayed valid.

The linker now asserts the validated boundary:

```ld
ASSERT(_iram_end <= 0x40378000,
    "ERROR: IRAM image crossed the validated loader boundary; reduce or refactor code")
```

New features can be added incrementally. After each change, build with the
linker map, check `_iram_end` and program headers, confirm `image layout check:
PASS`, and flash-test the `.data` sentinel and early USB output.

Avoid relocating individual Xtensa functions unless their literal pools and
all image load metadata move together. Previous relocation attempts caused
invalid load address `0x00000000`, corrupted literal pools, or kernel panics
even when the image checksum was valid.

## Latest findings (2026-09-11)

The latest clean baseline was restored by removing the entire V22 addition from
`test_ipc_mm()`, not just its closing `usbj_print()` call. The clean output is:

```text
.data sentinel: 0xC032DA7A (expected 0xC032DA7A)
.bss sentinel:  0x00000000 (expected 0x00000000)
```

An unexecuted `receiver_frame_saved` local/check was enough to reproduce the
failure. This strongly indicates that final Xtensa code generation/layout,
literal-pool placement, or stack/register spills are involved—not only the
runtime execution of the new code or the string contents.

The following are currently reverted: startup probes, UART diagnostics,
watchdog isolation changes, custom IRAM/flash relocation, function-section
relocation, function-level `-Os`, and the artificial 32 KiB linker assertion.

Additional confirmed facts:

- Full flash erase plus verified flashing did not fix the issue.
- The earliest-start probe read the correct sentinel immediately on entry to
  `start()` and after `wdt_feed_super_wdt()`.
- The generated ESP image was inspected with `esptool image-info`; checksums
  and validation hashes were valid, and the `.data` payload contained the
  expected sentinel bytes.
- A larger fixed DRAM LMA improved visible USB output in one test but did not
  fix the sentinel; the linker was restored to normal `AT > irom` placement.
- Moving `.rodata` to flash created a segment at load address `0x00000000` and
  caused a `StoreProhibited` panic; reverted.
- Splitting diagnostic code into another IRAM/flash segment corrupted Xtensa
  literal pools and call targets; reverted.
- Function-level optimization and function/data-section relocation also made
  output worse or caused panics; reverted.
- Extending the USB enumeration wait to about two seconds resulted in no
  visible output and was reverted. The normal short wait is retained.

Next work should reintroduce V22 one small expression at a time without extra
USB output, while comparing clean/failing ELF disassembly, literal pools,
stack usage, section sizes, and program headers. Do not assume that
`0x40378000` is a real 32 KiB hardware boundary: the ROM successfully loaded
an IRAM segment of approximately `0x8098` bytes.

## Latest boundary finding and working workaround

The layout comparison tool showed the clean image ending at approximately:

```text
_iram_end: 0x40377FE0
boundary:  0x40378000
```

Adding the receiver-frame check increased the image to `0x40378024`, and the
result failed. Removing the redundant `[SCHED V2 baseline-handoffs]` and
`[STK V1]` diagnostic blocks reclaimed enough space. The receiver-frame check
then produced:

```text
_iram_end: 0x40377FA4
margin:    92 bytes
```

Hardware testing of that image was clean, including the `.data` sentinel:

```text
.data sentinel: 0xC032DA7A (expected 0xC032DA7A)
.bss sentinel:  0x00000000 (expected 0x00000000)
```

This is the current working workaround: retain the receiver-frame feature and
remove redundant diagnostics to keep the IRAM image below `0x40378000`.

The boundary is still not fully explained as a documented hardware limit. The
ROM can load an IRAM segment larger than 32 KiB, but this project’s current
image/literal layout becomes unstable when the boundary is crossed. Treat it
as an image-layout constraint until proven otherwise.

## Failed relocation approaches

- Moving `test_ipc_mm()` directly to flash produced a segment with load address
  `0x00000000` and a `StoreProhibited` panic.
- Moving the function to a second IRAM region without preserving all matching
  Xtensa literal pools corrupted calls and produced severe USB garbage/panics.
- Enabling function/data sections and relocating the function-specific section
  still corrupted literal/call addressing.
- Function-level `-Os` changed instruction/literal layout and worsened output.

These failures show that Xtensa code must be relocated together with every
literal pool and its valid ESP image load segment. Do not relocate a function
by section attribute alone.

## Recommended long-term solution

Do not permanently remove diagnostics just to fit the current boundary. Move
diagnostics into a dedicated translation unit, compile it with matching
Xtensa literal-section options, and create a correctly described executable
load segment containing the complete diagnostic text and literal sections.
Alternatively, provide a diagnostic-only linker/image layout with more room.
Keep the linker assertion and layout-report tool so crossing the boundary is
detected at build time.

## Session closeout

The incremental restoration completed with the diagnostic image stable on
hardware. The final tested boot output preserved both sentinels:

```text
.data sentinel: 0xC032DA7A (expected 0xC032DA7A)
.bss sentinel:  0x00000000 (expected 0x00000000)
```

The final build also passed the image-layout check:

```text
IRAM end:     0x403756EC
IRAM boundary: 0x40378000
IRAM margin:  10516 bytes
image layout check: PASS
```

Restored and present in the image:

- blocked syscall frame bookkeeping and wakeup validation;
- IPC, scheduler, context, stack, lock, and diagnostic validation groups;
- syscall and user-trap frame layout contracts;
- user-trap frame capture in `irq.S`;
- the guarded user-trap restore sequence, including `EPC1`, `PS`, register
  restoration, and `rfe`;
- linker IRAM boundary assertion, map generation, and ELF/bin layout check.

The live user-trap return path remains intentionally fail-closed. The gate is
initialized to zero and `cp32_user_trap_dispatch()` returns `EBADCALL`, so the
new `rfe` sequence is compiled and layout-validated but is not reachable during
normal boot. Enabling it is a separate implementation task requiring a real
user exception test and validation of ownership, frame lifetime, and privilege
state.

The root cause of the earlier USB garbage was image-layout instability near
the `0x40378000` IRAM boundary, not the contents of a particular diagnostic
message. The durable safeguards are section garbage collection, the linker
assertion, and checking both the generated ELF and `.bin` on every build.

## Confirmed solution: full IRAM image

The previous `0x40378000` working boundary was not a hardware limit. The
stable solution is to keep the kernel in the ESP32-S3's complete contiguous
instruction SRAM window and let the ROM image loader place the runtime
sections there:

- IRAM origin: `0x40370000`
- IRAM size: `448 KiB`
- enforced end: `0x403E0000`
- verified image: `IRAM end 0x40375938`, with `435912` bytes remaining

The startup path now performs only early CPU interrupt quiescing before
`start()`. The earlier manual flash-MMU/IROM page mapping experiment was
removed; it was unnecessary for an all-IRAM image and could cause the kernel
to freeze during early hardware initialization.

The build automatically checks the ELF layout and reports the remaining IRAM
margin. The clean build passed with:

```text
image layout check: PASS
```

This is the confirmed hardware-tested layout to keep going forward.

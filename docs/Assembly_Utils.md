# Utils commands

See the instructions on the elf file
```shell
xtensa-esp32s3-elf-objdump -d build/cp32.elf | head -40
```

Debug.

In one terminal:
```shell
openocd -f board/esp32s3-builtin.cfg
```

in another:
```shell
xtensa-esp32s3-elf-gdb build/cp32.elf \
    -ex "set remote hardware-breakpoint-limit 2" \
    -ex "target remote :3333" \
    -ex "monitor reset halt" \
    -ex "flushregs"
```

inside gbd
```text
(gdb) info registers        # dump all CPU registers
(gdb) break main            # set breakpoint at main()
(gdb) continue              # run until breakpoint
(gdb) step                  # step one C line
(gdb) next                  # next line, don't enter functions
(gdb) print g_boot_count    # print variable value
(gdb) x/10x 0x60008000     # examine 10 words at RTC base address
(gdb) monitor reg pc        # print program counter via OpenOCD
```
#*================*
#*    CP32 OS     *
#*================*

# Toolchain
CC      = xtensa-esp32s3-elf-gcc
AS      = xtensa-esp32s3-elf-gcc
LD      = xtensa-esp32s3-elf-gcc
OBJCOPY = xtensa-esp32s3-elf-objcopy

# Flags
CFLAGS  = -fstack-usage -ffreestanding -nostdlib -nostartfiles -g -O0 -ffunction-sections -fdata-sections -mlongcalls -mtext-section-literals -mabi=call0 -Iinclude -Ikernel
ASFLAGS = -ffreestanding -nostdlib -nostartfiles -mabi=call0
LDFLAGS = -T $(SRC_DIR)/esp32s3.ld -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--gc-sections -nostdlib -nostartfiles -ffreestanding -e CP32 -lgcc

# SD is the default root. RAM remains a separate recovery build.
STORAGE ?= sd
SD_SIZE_MIB ?= 100
SD_INODES ?= 4096
SD_SPI ?= hardware
UPDATE_APPS ?= 0
ifeq ($(UPDATE_APPS),1)
ifneq ($(STORAGE),sd)
$(error updater requires STORAGE=sd)
endif
CFLAGS += -DCP32_UPDATE_APPS=1
else ifneq ($(UPDATE_APPS),0)
$(error UPDATE_APPS must be 0 or 1)
endif
ifeq ($(SD_SPI),soft)
CFLAGS += -DCP32_SD_SOFT=1
else ifneq ($(SD_SPI),hardware)
$(error SD_SPI must be hardware or soft)
endif
ifeq ($(STORAGE),ram)
STORAGE_SRCS = minix-demo.c ramdisk.c
CFLAGS += -DCP32_ROOT_RAM=1
else ifeq ($(STORAGE),sd)
STORAGE_SRCS = sdcard.c rootdisk.c
LDFLAGS += -Wl,--defsym=__cp32_heap_bytes=$(if $(filter 1,$(UPDATE_APPS)),163840,196608)
else
$(error STORAGE must be sd or ram)
endif

# Files
C_SRCS = context.c application.c start.c hardware_init.c main.c cp32-shell.c misc.c proc.c serial.c wdt.c klib.c port.c clock.c system.c tty.c memory.c cardputer.c display.c $(STORAGE_SRCS)
MM_SRCS = main.c alloc.c exec.c image.c
MM_HEADERS = $(wildcard mm/*.h) include/minix/cp32_mm.h
FS_SRCS = timestamp.c time.c rename.c appfile.c fproc.c misc.c filedes.c cache.c stadir.c super.c inode.c path.c open.c read.c write.c create.c utility.c
# The recovery image exposes no SD mutation commands.
ifeq ($(STORAGE),ram)
FS_SRCS := $(filter-out create.c,$(FS_SRCS))
endif
FS_HEADERS = $(wildcard fs/*.h)
ANSI_SRCS = memcpy.c memset.c strcpy.c strcmp.c strtol.c
POSIX_SRCS = opendir.c readdir.c closedir.c rewinddir.c
LIB_SRCS = lib/other/printk.c lib/other/seekdir.c lib/other/telldir.c lib/other/errno.c
C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS)) $(patsubst %.c,$(BUILD_DIR)/fs/%.o,$(FS_SRCS)) $(patsubst %.c,$(BUILD_DIR)/mm/%.o,$(MM_SRCS))
LIB_OBJS = $(patsubst lib/other/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS)) $(patsubst %.c,$(BUILD_DIR)/lib/ansi/%.o,$(ANSI_SRCS)) $(patsubst %.c,$(BUILD_DIR)/lib/posix/%.o,$(POSIX_SRCS))

ifeq ($(STORAGE),sd)
C_OBJS += $(BUILD_DIR)/drivers/sd_spi.o
ifneq ($(SD_SPI),soft)
C_OBJS += $(BUILD_DIR)/drivers/spi_fifo.o
endif
endif

ifeq ($(UPDATE_APPS),1)
C_OBJS += $(BUILD_DIR)/app-update.o
endif
TARGET  = cp32

# Assembly objects
BUILD_MPX32_O = $(BUILD_DIR)/mpx32.o
BUILD_VECTORS_O = $(BUILD_DIR)/vectors.o
BUILD_IRQ_O = $(BUILD_DIR)/irq.o
BUILD_KLIB32_O = $(BUILD_DIR)/klib32.o

# Directories
SRC_DIR = kernel
BUILD_DIR = $(if $(filter ram,$(STORAGE)),build-ram,$(if $(filter soft,$(SD_SPI)),build-sd-soft,build))$(if $(filter 1,$(UPDATE_APPS)),-updater,)

# Default target
all: $(BUILD_DIR)/$(TARGET).bin $(BUILD_DIR)/sdcard.img

# Host-side tests do not require the Xtensa toolchain. Keep the test runner
# outside src/, while retaining a single project Makefile.
tests:
	@set -e; \
	status=0; \
	for test in $$(find ../tests -maxdepth 1 -type f -name 'test_*.sh' | sort); do \
		printf '%s\n' "==> $$test"; \
		if sh "$$test"; then \
			printf '%s\n' "PASS $$test"; \
		else \
			printf '%s\n' "FAIL $$test"; status=1; \
		fi; \
	done; \
	exit $$status

# Compile all .c files
$(BUILD_DIR)/drivers/%.o: drivers/%.c drivers/sd_spi.h drivers/spi_fifo.h include/esp32s3/sd_spi2.h | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(C_OBJS): kernel/rootdisk.h kernel/sdcard.h include/esp32s3/sd_gpio.h include/esp32s3/sd_spi2.h drivers/spi_fifo.h

$(BUILD_DIR)/lib/posix/%.o: lib/posix/%.c lib/posix/dirent.h $(FS_HEADERS) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/seekdir.o $(BUILD_DIR)/telldir.o $(BUILD_DIR)/cp32-shell.o: lib/posix/dirent.h
$(BUILD_DIR)/seekdir.o $(BUILD_DIR)/telldir.o: $(FS_HEADERS)

$(BUILD_DIR)/lib/ansi/%.o: lib/ansi/%.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

# Keep the blocking MM task at its previously hardware-tested -O0 setting.
# Image160 halted at PC0 on its first exec/MM request after optimizing this
# task. Pure allocator/image/stack helpers retain -Os and the SRAM budget.
# Optimized task resumption needs separate hardware validation.
$(BUILD_DIR)/mm/%.o: mm/%.c $(MM_HEADERS) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(if $(filter main.c,$(<F)),-O0,-Os) -c $< -o $@

# Filesystem C has no interrupt-entry/register-frame responsibilities.
# Optimize this bounded service for SRAM size; kernel/assembly remain -O0.
$(BUILD_DIR)/fs/%.o: fs/%.c $(FS_HEADERS) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -Os -c $< -o $@

# Shell/FS services are ordinary call0 C, with no IRQ frame construction.
# Use the same size policy as FS to retain heap/stacks as the service grows.
$(BUILD_DIR)/memory.o: CFLAGS += -Os
$(BUILD_DIR)/clock.o: CFLAGS += -Os
$(BUILD_DIR)/system.o: CFLAGS += -Os
$(BUILD_DIR)/rootdisk.o: CFLAGS += -Os
$(BUILD_DIR)/cp32-shell.o: CFLAGS += -Os
$(BUILD_DIR)/cp32-shell.o: $(FS_HEADERS)

$(BUILD_DIR)/minix-demo.h: ../tools/make_minix_demo.py $(BUILD_DIR)/hello.elf $(BUILD_DIR)/echo.elf $(BUILD_DIR)/cat.elf $(BUILD_DIR)/wc.elf $(BUILD_DIR)/ls.elf $(BUILD_DIR)/exec.elf | $(BUILD_DIR)
	python3 $< --image $(BUILD_DIR)/minix-demo.img --header $@ --hello $(BUILD_DIR)/hello.elf --echo $(BUILD_DIR)/echo.elf --cat $(BUILD_DIR)/cat.elf --wc $(BUILD_DIR)/wc.elf --ls $(BUILD_DIR)/ls.elf --exec $(BUILD_DIR)/exec.elf

$(BUILD_DIR)/minix-demo.o: CFLAGS += -I$(BUILD_DIR)
$(BUILD_DIR)/minix-demo.o: $(BUILD_DIR)/minix-demo.h

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Compile mpx32.S
$(BUILD_MPX32_O): $(SRC_DIR)/mpx32.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $(SRC_DIR)/mpx32.S -o $(BUILD_MPX32_O)

# Compile vectors.S - gets a special flag: no automatic literal pool injection
$(BUILD_VECTORS_O): $(SRC_DIR)/vectors.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -mno-text-section-literals -c $< -o $@

# Compile irq.S
$(BUILD_IRQ_O): $(SRC_DIR)/irq.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $(SRC_DIR)/irq.S -o $(BUILD_IRQ_O)

# Compile klib32.S
$(BUILD_KLIB32_O): $(SRC_DIR)/klib32.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $(SRC_DIR)/klib32.S -o $(BUILD_KLIB32_O)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Link
$(BUILD_DIR)/%.o: lib/other/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(SRC_DIR)/esp32s3.ld $(C_OBJS) $(LIB_OBJS) $(BUILD_MPX32_O) $(BUILD_VECTORS_O) $(BUILD_IRQ_O) $(BUILD_KLIB32_O)
	$(LD) $(LDFLAGS) $(C_OBJS) $(LIB_OBJS) $(BUILD_MPX32_O) $(BUILD_VECTORS_O) $(BUILD_IRQ_O) $(BUILD_KLIB32_O) -lgcc -o $@

# Convert to raw binary for flashing
$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	esptool --chip esp32s3 elf2image \
	  --flash-mode dio \
	  --flash-freq 40m \
	  --flash-size 8MB \
	  $(BUILD_DIR)/$(TARGET).elf
	python3 ../tools/check_image_layout.py $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).bin
	

# Flash to ESP32-S3
flash: $(BUILD_DIR)/$(TARGET).bin
	esptool --chip esp32s3 --port /dev/cu.usbmodem2101 write-flash 0x0 $(BUILD_DIR)/$(TARGET).bin

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)

# ---- ELF inspection targets ----

# Section sizes — how much each section actually uses
size: $(BUILD_DIR)/$(TARGET).elf
	xtensa-esp32s3-elf-size -A $(BUILD_DIR)/$(TARGET).elf

# Full symbol table sorted by address — see exactly where every
# function and variable lands in memory
nm: $(BUILD_DIR)/$(TARGET).elf
	xtensa-esp32s3-elf-nm -n $(BUILD_DIR)/$(TARGET).elf

# Disassembly — read the actual machine code with source interleaved
disasm: $(BUILD_DIR)/$(TARGET).elf
	xtensa-esp32s3-elf-objdump -d -S $(BUILD_DIR)/$(TARGET).elf | less

# Section headers — VMA, LMA, size, flags for every section
headers: $(BUILD_DIR)/$(TARGET).elf
	xtensa-esp32s3-elf-objdump -h $(BUILD_DIR)/$(TARGET).elf

# Full linker map — verbose version of the above, shows which .o
# file contributed each symbol and exactly how sections were placed
map: $(BUILD_DIR)/$(TARGET).elf
	xtensa-esp32s3-elf-nm --print-size --size-sort --radix=x \
	    $(BUILD_DIR)/$(TARGET).elf

# Dump all ELF segment headers (LMA vs VMA — critical for verifying
# the AT > irom placement is correct)
segments: $(BUILD_DIR)/$(TARGET).elf
	xtensa-esp32s3-elf-readelf -l $(BUILD_DIR)/$(TARGET).elf

# Dump all section headers with addresses
sections: $(BUILD_DIR)/$(TARGET).elf
	xtensa-esp32s3-elf-readelf -S $(BUILD_DIR)/$(TARGET).elf

# Separate trusted application artifact; never flashed by this target.
.PHONY: hello
hello: $(BUILD_DIR)/hello.elf
$(BUILD_DIR)/hello.elf: apps/hello/start.S apps/hello/hello.c apps/lib/heap.c apps/lib/heap.h apps/lib/io.c apps/lib/io.h lib/ansi/memcpy.c apps/hello/abi.h apps/hello/hello.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Os -mno-longcalls -nostdlib -Wl,--gc-sections -Wl,--build-id=none -Wl,-n -Wl,-s -Wl,-T,apps/hello/hello.ld -Wl,-Map,$(BUILD_DIR)/hello.map apps/hello/start.S apps/hello/hello.c apps/lib/heap.c apps/lib/io.c lib/ansi/memcpy.c -o $@

$(BUILD_DIR)/application.o: kernel/application.h mm/image.h mm/exec.h apps/hello/abi.h
$(BUILD_DIR)/cp32-shell.o: kernel/application.h mm/image.h mm/exec.h

.PHONY: echo
echo: $(BUILD_DIR)/echo.elf
$(BUILD_DIR)/echo.elf: apps/hello/start.S apps/echo/echo.c apps/hello/abi.h apps/hello/hello.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Os -mno-longcalls -nostdlib -Wl,--gc-sections -Wl,--build-id=none -Wl,-n -Wl,-s -Wl,-T,apps/hello/hello.ld -Wl,-Map,$(BUILD_DIR)/echo.map apps/hello/start.S apps/echo/echo.c -o $@

.PHONY: cat
cat: $(BUILD_DIR)/cat.elf
$(BUILD_DIR)/cat.elf: apps/hello/start.S apps/cat/cat.c apps/lib/io.c apps/lib/io.h lib/ansi/memcpy.c apps/hello/abi.h apps/hello/hello.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Os -mno-longcalls -nostdlib -Wl,--gc-sections -Wl,--build-id=none -Wl,-n -Wl,-s -Wl,-T,apps/hello/hello.ld -Wl,-Map,$(BUILD_DIR)/cat.map apps/hello/start.S apps/cat/cat.c apps/lib/io.c lib/ansi/memcpy.c -o $@

.PHONY: wc
wc: $(BUILD_DIR)/wc.elf
$(BUILD_DIR)/wc.elf: apps/lib/output.h apps/lib/stream.h apps/hello/start.S apps/wc/wc.c apps/lib/output.c apps/lib/stream.c apps/lib/io.c apps/lib/io.h lib/ansi/memcpy.c apps/hello/abi.h apps/hello/hello.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Os -mno-longcalls -nostdlib -Wl,--gc-sections -Wl,--build-id=none -Wl,-n -Wl,-s -Wl,-T,apps/hello/hello.ld -Wl,-Map,$(BUILD_DIR)/wc.map apps/hello/start.S apps/wc/wc.c apps/lib/output.c apps/lib/stream.c apps/lib/io.c lib/ansi/memcpy.c -o $@

.PHONY: ls
ls: $(BUILD_DIR)/ls.elf
$(BUILD_DIR)/ls.elf: apps/hello/start.S apps/ls/ls.c apps/lib/output.c apps/lib/output.h apps/lib/io.c apps/lib/io.h lib/ansi/memcpy.c apps/hello/abi.h apps/hello/hello.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Os -mno-longcalls -nostdlib -Wl,--gc-sections -Wl,--build-id=none -Wl,-n -Wl,-s -Wl,-T,apps/hello/hello.ld -Wl,-Map,$(BUILD_DIR)/ls.map apps/hello/start.S apps/ls/ls.c apps/lib/output.c apps/lib/io.c lib/ansi/memcpy.c -o $@

.PHONY: exec
exec: $(BUILD_DIR)/exec.elf
$(BUILD_DIR)/exec.elf: apps/hello/start.S apps/exec/exec.c apps/lib/io.c apps/lib/io.h lib/ansi/memcpy.c apps/hello/abi.h apps/hello/hello.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Os -mno-longcalls -nostdlib -Wl,--gc-sections -Wl,--build-id=none -Wl,-n -Wl,-s -Wl,-T,apps/hello/hello.ld -Wl,-Map,$(BUILD_DIR)/exec.map apps/hello/start.S apps/exec/exec.c apps/lib/io.c lib/ansi/memcpy.c -o $@

# Only regular host image files are produced; no card is written by make.
.PHONY: sdimage
sdimage: $(BUILD_DIR)/sdcard.img
$(BUILD_DIR)/.sd-size-$(SD_SIZE_MIB)-$(SD_INODES): | $(BUILD_DIR)
	rm -f $(BUILD_DIR)/.sd-size-*
	touch $@

$(BUILD_DIR)/sdcard.img: $(BUILD_DIR)/minix-demo.h ../tools/make_sd_image.py ../tools/fsck_minix.py $(BUILD_DIR)/.sd-size-$(SD_SIZE_MIB)-$(SD_INODES)
	python3 ../tools/make_sd_image.py $(BUILD_DIR)/minix-demo.img $@ --size-mib $(SD_SIZE_MIB) --inodes $(SD_INODES)

# Opt-in maintenance firmware: normal builds contain neither command nor payload.
.PHONY: updater flash-updater
updater:
	$(MAKE) UPDATE_APPS=1 all
flash-updater:
	$(MAKE) UPDATE_APPS=1 flash
ifeq ($(UPDATE_APPS),1)
$(BUILD_DIR)/app-payload.h: ../tools/embed_apps.py $(addprefix $(BUILD_DIR)/,hello.elf echo.elf cat.elf wc.elf ls.elf exec.elf)
	python3 $< $(BUILD_DIR) $@
$(BUILD_DIR)/cp32-shell.o: $(BUILD_DIR)/app-payload.h kernel/app-update.h
$(BUILD_DIR)/cp32-shell.o: CFLAGS += -I$(BUILD_DIR)
$(BUILD_DIR)/app-update.o: kernel/app-update.c kernel/app-update.h $(FS_HEADERS)
$(BUILD_DIR)/app-update.o: CFLAGS += -Os
endif

# Read-only host checker; alternate paths must be offline image files.
FSCK_IMAGE ?= $(BUILD_DIR)/sdcard.img
.PHONY: fsck
fsck: $(FSCK_IMAGE)
	python3 ../tools/fsck_minix.py "$(FSCK_IMAGE)"

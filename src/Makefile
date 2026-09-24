#*================*
#*    CP32 OS     *
#*================*

# Toolchain
CC      = xtensa-esp32s3-elf-gcc
AS      = xtensa-esp32s3-elf-gcc
LD      = xtensa-esp32s3-elf-gcc
OBJCOPY = xtensa-esp32s3-elf-objcopy

# Flags
CFLAGS  = -ffreestanding -nostdlib -nostartfiles -g -O0 -ffunction-sections -fdata-sections -mlongcalls -mtext-section-literals -mabi=call0 -Iinclude -Ikernel
ASFLAGS = -ffreestanding -nostdlib -nostartfiles -mabi=call0
LDFLAGS = -T $(SRC_DIR)/esp32s3.ld -Wl,-Map=$(BUILD_DIR)/$(TARGET).map,--gc-sections -nostdlib -nostartfiles -ffreestanding -e CP32 -lgcc

# Files
C_SRCS = start.c hardware_init.c main.c cp32-shell.c misc.c proc.c serial.c wdt.c klib.c port.c clock.c system.c tty.c memory.c minix-demo.c cardputer.c display.c ramdisk.c
MM_SRCS = main.c alloc.c
MM_HEADERS = $(wildcard mm/*.h) include/minix/cp32_mm.h
FS_SRCS = misc.c filedes.c cache.c stadir.c super.c inode.c path.c open.c read.c utility.c
FS_HEADERS = $(wildcard fs/*.h)
ANSI_SRCS = memcpy.c memset.c strcpy.c strcmp.c strtol.c
LIB_SRCS = lib/other/printk.c
C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS)) $(patsubst %.c,$(BUILD_DIR)/fs/%.o,$(FS_SRCS)) $(patsubst %.c,$(BUILD_DIR)/mm/%.o,$(MM_SRCS))
LIB_OBJS = $(patsubst lib/other/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS)) $(patsubst %.c,$(BUILD_DIR)/lib/ansi/%.o,$(ANSI_SRCS))

TARGET  = cp32

# Assembly objects
BUILD_MPX32_O = $(BUILD_DIR)/mpx32.o
BUILD_VECTORS_O = $(BUILD_DIR)/vectors.o
BUILD_IRQ_O = $(BUILD_DIR)/irq.o
BUILD_KLIB32_O = $(BUILD_DIR)/klib32.o

# Directories
SRC_DIR = kernel
BUILD_DIR = build

# Default target
all: $(BUILD_DIR)/$(TARGET).bin

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
$(BUILD_DIR)/lib/ansi/%.o: lib/ansi/%.c | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mm/%.o: mm/%.c $(MM_HEADERS) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/fs/%.o: fs/%.c $(FS_HEADERS) | $(BUILD_DIR)
	mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/cp32-shell.o: $(FS_HEADERS)

$(BUILD_DIR)/minix-demo.h: ../tools/make_minix_demo.py | $(BUILD_DIR)
	python3 $< --image $(BUILD_DIR)/minix-demo.img --header $@

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

$(BUILD_DIR)/$(TARGET).elf: $(C_OBJS) $(LIB_OBJS) $(BUILD_MPX32_O) $(BUILD_VECTORS_O) $(BUILD_IRQ_O) $(BUILD_KLIB32_O)
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

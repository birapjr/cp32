#*================*
#*    CP32 OS     *
#*================*

# Toolchain
CC      = xtensa-esp32s3-elf-gcc
AS      = xtensa-esp32s3-elf-gcc
LD      = xtensa-esp32s3-elf-gcc
OBJCOPY = xtensa-esp32s3-elf-objcopy

# Flags
CFLAGS  = -ffreestanding -nostdlib -nostartfiles -O0 -mlongcalls -mtext-section-literals -mabi=call0
ASFLAGS = -ffreestanding -nostdlib -nostartfiles -mabi=call0
LDFLAGS = -T $(SRC_DIR)/esp32s3.ld -nostdlib -nostartfiles -ffreestanding -e CP32

# Files
# Files
C_SRCS = main.c serial.c
C_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_SRCS))

BUILD_MPX32_O = $(BUILD_DIR)/mpx32.o
TARGET  = cp32

# Directories
SRC_DIR = kernel
BUILD_DIR = build

# Default target
all: $(BUILD_DIR)/$(TARGET).bin

# Compile all .c files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble mpx32.S
$(BUILD_MPX32_O): $(SRC_DIR)/mpx32.S | $(BUILD_DIR)
	$(AS) $(ASFLAGS) -c $(SRC_DIR)/mpx32.S -o $(BUILD_MPX32_O)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Link
$(BUILD_DIR)/$(TARGET).elf: $(C_OBJS) $(BUILD_MPX32_O)
	$(LD) $(LDFLAGS) $(C_OBJS) $(BUILD_MPX32_O) -o $@

# Convert to raw binary for flashing
$(BUILD_DIR)/$(TARGET).bin: $(BUILD_DIR)/$(TARGET).elf
	esptool --chip esp32s3 elf2image \
	  --flash-mode dio \
	  --flash-freq 40m \
	  --flash-size 4MB \
	  $(BUILD_DIR)/$(TARGET).elf
	

# Flash to ESP32-S3
flash: $(BUILD_DIR)/$(TARGET).bin
	esptool --chip esp32s3 --port /dev/cu.usbmodem101 write_flash 0x0 $(BUILD_DIR)/$(TARGET).bin

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR)
M5_SAMPLE_DIR ?= /private/tmp/m5cardputer-display-check
M5_PLATFORMIO_CORE_DIR ?= /private/tmp/cp32-platformio

.PHONY: m5-sample

m5-sample:
	@test -f "$(M5_SAMPLE_DIR)/platformio.ini" || { echo "M5 sample project not found: $(M5_SAMPLE_DIR)"; exit 1; }
	PLATFORMIO_CORE_DIR="$(M5_PLATFORMIO_CORE_DIR)" pio run -d "$(M5_SAMPLE_DIR)" -e m5stack-cardputer

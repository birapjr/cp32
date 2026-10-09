#ifndef CP32_SD_GPIO_H
#define CP32_SD_GPIO_H
/* ESP32-S3 gpio_reg.h/io_mux_reg.h (Espressif v5.4.2 register definitions).
 * W1TS/W1TC affect only selected pins. Bank 1 contains GPIO32..48.
 * GPIO output matrix selector 256 chooses GPIO_OUT; bit10 uses GPIO_ENABLE.
 * IO_MUX function 1 is GPIO; bit9 input enable, bit8 pull-up, bit7 pull-down.
 * M5Stack Cardputer-Adv pin map: CS12 MOSI14 CLK40 MISO39. Separate from LCD. */
#define SD_GPIO_REG(offset) (*(volatile uint32_t *)(0x60004000UL+(offset)))
#define SD_GPIO_MUX(pin) (*(volatile uint32_t *)(0x60009004UL+4*(pin)))
#define SD_GPIO_FUNC(pin) SD_GPIO_REG(0x554+4*(pin))
#define SD_GPIO_OUT_SET 0x08
#define SD_GPIO_OUT_CLEAR 0x0c
#define SD_GPIO_OUT1_SET 0x14
#define SD_GPIO_OUT1_CLEAR 0x18
#define SD_GPIO_ENABLE_SET 0x24
#define SD_GPIO_ENABLE1_SET 0x30
#define SD_GPIO_ENABLE1_CLEAR 0x34
#define SD_GPIO_IN1 0x40
#define SD_GPIO_CS 12U
#define SD_GPIO_MOSI 14U
#define SD_GPIO_CLK 40U
#define SD_GPIO_MISO 39U
#endif

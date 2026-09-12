#include "cardputer.h"
#include <stdint.h>

#define GPIO_BASE 0x60004000UL
#define GPIO_OUT_W1TS (GPIO_BASE + 0x08)
#define GPIO_OUT_W1TC (GPIO_BASE + 0x0C)
#define GPIO_ENABLE_W1TS (GPIO_BASE + 0x24)
#define GPIO_ENABLE_W1TC (GPIO_BASE + 0x28)
#define GPIO_IN (GPIO_BASE + 0x3C)
#define IO_MUX_BASE 0x60009000UL
#define IO_MUX_PIN(pin) (IO_MUX_BASE + 0x04 + ((pin) * 4))
#define IO_MUX_FUN_PU (1u << 8)
#define IO_MUX_FUN_IE (1u << 9)
#define KBD_SDA 8u
#define KBD_SCL 9u
#define KBD_INT 11u
#define KBD_ADDR 0x34u
static volatile unsigned kbd_trace_stage;

CP32_IRAM_EXT static void wait_i2c(void) { volatile unsigned n = 80; while (n--) ; }
CP32_IRAM_EXT static void release(unsigned pin) { *(volatile uint32_t *)GPIO_ENABLE_W1TC = 1u << pin; }
CP32_IRAM_EXT static void pull_low(unsigned pin) {
  *(volatile uint32_t *)GPIO_OUT_W1TC = 1u << pin;
  *(volatile uint32_t *)GPIO_ENABLE_W1TS = 1u << pin;
}
CP32_IRAM_EXT static int read_pin(unsigned pin) { return (*(volatile uint32_t *)GPIO_IN >> pin) & 1; }
CP32_IRAM_EXT static void configure_pads(void) {
  *(volatile uint32_t *)IO_MUX_PIN(KBD_SDA) |= IO_MUX_FUN_PU | IO_MUX_FUN_IE;
  *(volatile uint32_t *)IO_MUX_PIN(KBD_SCL) |= IO_MUX_FUN_PU | IO_MUX_FUN_IE;
  *(volatile uint32_t *)IO_MUX_PIN(KBD_INT) |= IO_MUX_FUN_PU | IO_MUX_FUN_IE;
}
CP32_IRAM_EXT static void start_i2c(void) {
  release(KBD_SDA); release(KBD_SCL); wait_i2c(); pull_low(KBD_SDA); wait_i2c(); pull_low(KBD_SCL);
}
CP32_IRAM_EXT static void restart_i2c(void) {
  /* A repeated START must release SDA while SCL is high before asserting it. */
  release(KBD_SDA); release(KBD_SCL); wait_i2c();
  pull_low(KBD_SDA); wait_i2c(); pull_low(KBD_SCL);
}
CP32_IRAM_EXT static void stop_i2c(void) {
  pull_low(KBD_SDA); release(KBD_SCL); wait_i2c(); release(KBD_SDA); wait_i2c();
}
CP32_IRAM_EXT static int write_byte(uint8_t value) {
  unsigned bit;
  for (bit = 0; bit < 8; bit++) {
    if (value & 0x80) release(KBD_SDA); else pull_low(KBD_SDA);
    release(KBD_SCL); wait_i2c(); pull_low(KBD_SCL); wait_i2c(); value <<= 1;
  }
  release(KBD_SDA); release(KBD_SCL); wait_i2c(); bit = !read_pin(KBD_SDA); pull_low(KBD_SCL); return (int)bit;
}

CP32_IRAM_EXT static uint8_t read_byte(int acknowledge) {
  unsigned bit;
  uint8_t value = 0;
  release(KBD_SDA);
  for (bit = 0; bit < 8; bit++) {
    value <<= 1; release(KBD_SCL); wait_i2c();
    if (read_pin(KBD_SDA)) value |= 1;
    pull_low(KBD_SCL); wait_i2c();
  }
  if (acknowledge) pull_low(KBD_SDA); else release(KBD_SDA);
  release(KBD_SCL); wait_i2c(); pull_low(KBD_SCL); release(KBD_SDA);
  return value;
}

CP32_IRAM_EXT static int write_register(uint8_t reg, uint8_t value)
{
  int ack;
  start_i2c();
  ack = write_byte((uint8_t)(KBD_ADDR << 1));
  if (ack) ack = write_byte(reg);
  if (ack) ack = write_byte(value);
  stop_i2c();
  return ack;
}

CP32_IRAM_EXT static int read_register(uint8_t reg, unsigned char *value)
{
  int ack;
  kbd_trace_stage = 1;
  start_i2c();
  ack = write_byte((uint8_t)(KBD_ADDR << 1));
  kbd_trace_stage = 2;
  if (ack) ack = write_byte(reg);
  kbd_trace_stage = 3;
  if (ack) {
    restart_i2c();
    kbd_trace_stage = 4;
    ack = write_byte((uint8_t)((KBD_ADDR << 1) | 1));
  }
  if (ack) {
    kbd_trace_stage = 5;
    *value = read_byte(0);
  }
  kbd_trace_stage = 6;
  stop_i2c();
  kbd_trace_stage = 7;
  return ack;
}

CP32_IRAM_EXT int cardputer_keyboard_read_status(unsigned char *status, unsigned char *count)
{
  if (status == (unsigned char *)0 || count == (unsigned char *)0) return -1;
  if (!read_register(0x02, status) || !read_register(0x03, count)) return -1;
  return 0;
}

CP32_IRAM_EXT int cardputer_keyboard_read_config(unsigned char *rows, unsigned char *cols0,
                                   unsigned char *cols1, unsigned char *cfg)
{
  if (!rows || !cols0 || !cols1 || !cfg) return -1;
  if (!read_register(0x1D, rows) || !read_register(0x1E, cols0) ||
      !read_register(0x1F, cols1) || !read_register(0x01, cfg)) return -1;
  return 0;
}

CP32_IRAM_EXT int cardputer_keyboard_read_event(unsigned char *event)
{
  int ack;
  if (event == (unsigned char *)0) return -1;
  /* TCA8418 KEY_EVENT_A (0x04); 0x03 is only the event counter. */
  start_i2c();
  ack = write_byte((uint8_t)(KBD_ADDR << 1));
  if (ack) ack = write_byte(0x04);
  if (ack) {
    restart_i2c();
    ack = write_byte((uint8_t)((KBD_ADDR << 1) | 1));
  }
  if (ack) {
    /* Auto-increment starts at KEY_EVENT_A after the register address. */
    if (ack) *event = read_byte(0);
  }
  stop_i2c();
  return ack ? (*event != 0) : -1;
}

CP32_IRAM_EXT int cardputer_keyboard_init(void)
{
  int ok = 1;
  /* Match M5Cardputer's TCA8418KeyboardReader: Cardputer Adv is a 7x8
   * matrix.  The remaining pins are GPIO inputs with falling-edge events. */
  ok &= write_register(0x23, 0x00);
  ok &= write_register(0x24, 0x00);
  ok &= write_register(0x25, 0x00);
  ok &= write_register(0x20, 0xFF);
  ok &= write_register(0x21, 0xFF);
  ok &= write_register(0x22, 0xFF);
  ok &= write_register(0x26, 0x00);
  ok &= write_register(0x27, 0x00);
  ok &= write_register(0x28, 0x00);
  ok &= write_register(0x1A, 0xFF);
  ok &= write_register(0x1B, 0xFF);
  ok &= write_register(0x1C, 0xFF);
  ok &= write_register(0x1D, 0x7F);
  ok &= write_register(0x1E, 0xFF);
  ok &= write_register(0x1F, 0x00);
  /* Clear stale GPIO/key interrupt state, then enable both sources. */
  ok &= write_register(0x02, 0x03);
  ok &= write_register(0x01, 0x03);
  return ok;
}

CP32_IRAM_EXT int cardputer_keyboard_probe(void)
{
  int ack;
  configure_pads();
  release(KBD_SDA); release(KBD_SCL);
  start_i2c();
  ack = write_byte((uint8_t)(KBD_ADDR << 1));
  stop_i2c();
  return ack;
}

CP32_IRAM_EXT int cardputer_keyboard_interrupt_asserted(void)
{
  /* TCA8418 INT is active low and open drain on Cardputer Adv G11. */
  release(KBD_INT);
  return read_pin(KBD_INT) == 0;
}

CP32_IRAM_EXT unsigned cardputer_keyboard_bus_idle(void)
{
  release(KBD_SDA);
  release(KBD_SCL);
  return (unsigned)read_pin(KBD_SDA) | ((unsigned)read_pin(KBD_SCL) << 1);
}

CP32_IRAM_EXT unsigned cardputer_keyboard_trace_stage(void)
{
  return kbd_trace_stage;
}

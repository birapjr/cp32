#include "kernel.h"
#include <termios.h>
#include <sys/ioctl.h>
#include "display.h"
#include "ramdisk.h"
#include "tty.h"
#include <string.h>

extern void cp32_tty_poll_keyboard(void);
extern int cp32_tty_read_char(char *out);
volatile char cp32_tty_user_byte;
static char cp32_tty_line[64];
static unsigned cp32_tty_line_len;

CP32_IRAM_EXT static void cp32_shell_print(const char *s)
{
  lock(); usbj_print(s); unlock();
  cardputer_display_write(s);
}

CP32_IRAM_EXT static void cp32_shell_print_u32(uint32_t value)
{
  char buf[11]; char *p = buf + sizeof(buf) - 1;
  *p = '\0';
  do { *--p = (char)('0' + value % 10); value /= 10; } while (value);
  cp32_shell_print(p);
}

CP32_IRAM_EXT static void cp32_shell_command(const char *line)
{
  if (strcmp(line, "ls") == 0) {
    cp32_shell_print("ramdisk\r\n");
    cp32_shell_print("boot\r\n");
    cp32_shell_print("README\r\n");
    cp32_shell_print("[CMD ls capacity=");
    cp32_shell_print_u32(cp32_ramdisk_capacity());
    cp32_shell_print(" formatted=");
    cp32_shell_print(cp32_ramdisk_is_formatted() ? "1" : "0");
    cp32_shell_print("]\r\n");
  } else if (strcmp(line, "ramdisk") == 0) {
    cp32_shell_print("[CMD ramdisk capacity=");
    cp32_shell_print_u32(cp32_ramdisk_capacity());
    cp32_shell_print("]\r\n");
  } else if (line[0] != '\0') {
    cp32_shell_print("[CMD unknown=");
    cp32_shell_print(line);
    cp32_shell_print("]\r\n");
  }
}

CP32_IRAM_EXT void cp32_tty_read_client(void)
{
  usbj_print("[TTY user-entry]\r\n");
  for (;;) {
    cp32_tty_poll_keyboard();
    if (cp32_tty_read_char((char *)&cp32_tty_user_byte) == 1) {
      usbj_print("[TTY user-char=");
      usbj_print_hex32((uint32_t)(unsigned char)cp32_tty_user_byte);
      usbj_print("]\r\n");
      if (cp32_tty_user_byte == '\b') {
        if (cp32_tty_line_len != 0) cp32_tty_line_len--;
      } else if (cp32_tty_user_byte == '\r' || cp32_tty_user_byte == '\n') {
        cp32_tty_line[cp32_tty_line_len] = '\0';
        usbj_print("[TTY line="); usbj_print(cp32_tty_line); usbj_print("]\r\n");
        cardputer_display_putc('\n');
        cp32_shell_command(cp32_tty_line);
        cardputer_display_write("$ ");
        cp32_tty_line_len = 0;
      } else if (cp32_tty_user_byte >= 0x20 &&
                 cp32_tty_user_byte <= 0x7E && cp32_tty_line_len < 63) {
        cp32_tty_line[cp32_tty_line_len++] = cp32_tty_user_byte;
        cardputer_display_putc((char)cp32_tty_user_byte);
      }
    }
  }
}

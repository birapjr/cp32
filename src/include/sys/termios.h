#ifndef _SYS_TERMIOS_H
#define _SYS_TERMIOS_H

/* Minimal termios interface used by the CP32 terminal task. */
typedef unsigned int tcflag_t;
typedef unsigned int speed_t;
typedef unsigned char cc_t;

#define NCCS 14
#define _POSIX_VDISABLE  ((unsigned char) 0xFF)

struct termios {
  tcflag_t c_iflag;
  tcflag_t c_oflag;
  tcflag_t c_cflag;
  tcflag_t c_lflag;
  speed_t c_ispeed;
  speed_t c_ospeed;
  cc_t c_cc[NCCS];
};

/* Control-character indexes. */
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define VSTART 7
#define VSTOP 8
#define VSUSP 9
#define VEOL 10
#define VREPRINT 11
#define VLNEXT 12
#define VDISCARD 13

/* Input flags. */
#define IGNBRK  0x0001
#define BRKINT  0x0002
#define IGNPAR  0x0004
#define PARMRK  0x0008
#define INPCK   0x0010
#define ISTRIP  0x0020
#define INLCR   0x0040
#define IGNCR   0x0080
#define ICRNL   0x0100
#define IXON    0x0200
#define IXOFF   0x0400
#define IXANY   0x0800

/* Output flags. */
#define OPOST   0x0001
#define ONLCR   0x0002
#define XTABS   0x0004

/* Control flags. */
#define CS5     0x0000
#define CS6     0x0010
#define CS7     0x0020
#define CS8     0x0030
#define CSIZE   0x0030
#define CSTOPB  0x0040
#define CREAD   0x0080
#define PARENB  0x0100
#define PARODD  0x0200
#define HUPCL   0x0400
#define CLOCAL  0x0800

/* Local flags. */
#define ISIG    0x0001
#define ICANON  0x0002
#define ECHO    0x0008
#define ECHOE   0x0010
#define ECHOK   0x0020
#define ECHONL  0x0040
#define NOFLSH  0x0080
#define TOSTOP  0x0100
#define IEXTEN  0x0200

/* Baud rates. */
#define B0      0
#define B50     50
#define B75     75
#define B110    110
#define B134    134
#define B200    200
#define B300    300
#define B600    600
#define B1200   1200
#define B1800   1800
#define B2400   2400
#define B4800   4800
#define B9600   9600
#define B19200  19200
#define B38400  38400
#define B57600  57600
#define B115200 115200

/* tcflush() queue selectors. */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* tcflow() actions. */
#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3

/* Defaults used by the kernel TTY task. */
#define TINPUT_DEF  (ICRNL | IXON)
#define TOUTPUT_DEF (OPOST | ONLCR)
#define TCTRL_DEF   (CREAD | CS8)
#define TLOCAL_DEF  (ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN)
#define TSPEED_DEF  B9600

#define TEOF_DEF      4    /* Ctrl-D */
#define TEOL_DEF      0
#define TERASE_DEF    127  /* DEL */
#define TINTR_DEF     3    /* Ctrl-C */
#define TKILL_DEF     21   /* Ctrl-U */
#define TMIN_DEF      1
#define TQUIT_DEF     28   /* Ctrl-\ */
#define TTIME_DEF     0
#define TSUSP_DEF     26   /* Ctrl-Z */
#define TSTART_DEF    17   /* Ctrl-Q */
#define TSTOP_DEF     19   /* Ctrl-S */
#define TREPRINT_DEF  18   /* Ctrl-R */
#define TLNEXT_DEF    22   /* Ctrl-V */
#define TDISCARD_DEF  15   /* Ctrl-O */

static speed_t cfgetispeed(const struct termios *t) { return t->c_ispeed; }
static speed_t cfgetospeed(const struct termios *t) { return t->c_ospeed; }
static int cfsetispeed(struct termios *t, speed_t s) { t->c_ispeed = s; return 0; }
static int cfsetospeed(struct termios *t, speed_t s) { t->c_ospeed = s; return 0; }

#endif

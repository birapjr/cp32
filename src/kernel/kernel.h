// kernel/kernel.h

/**
 * This is the master header for the kernel.  It includes some other files
 * and defines the principal constants.
 * 
 * Author: Ubirajara Cortes
 * Date: 22.05.2026
 */
#define _POSIX_SOURCE      1	/* tell headers to include POSIX stuff */
#define _CP32              1	/* tell headers to include CP32 stuff */
#define _SYSTEM            1	/* tell headers that this is the kernel */

#include <minix/config.h> /* must be first */
#include <ansi.h> /* must be second */
#include <sys/types.h>
#include <minix/const.h>
#include <minix/type.h>
#include <minix/syslib.h>

#include <stdint.h>
#include <errno.h>

#include <string.h>
#include <limits.h>

#include "serial.h"
#include "wdt.h"
#include "klib.h"

#include "const.h"
#include "type.h"
#include "proto.h"
#include "glo.h"

#if (CHIP == ESP32_S3)
#include <esp32s3/const.h>
#endif

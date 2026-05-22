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

#include <stdint.h>
#include "const.h"
#include "serial.h"
#include "wdt.h"
#include "klib.h"
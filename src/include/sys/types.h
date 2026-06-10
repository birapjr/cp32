#ifndef _TYPES_H
#define _TYPES_H

/* Let the toolchain provide the current libc/POSIX type definitions.
 * The local kernel headers should not shadow standard types like size_t,
 * time_t, clock_t, ssize_t, sigset_t, or off_t on ESP-IDF.
 */
#include_next <sys/types.h>

/* ESP-IDF / Xtensa libc does not always expose sigset_t through the same
 * include path this legacy kernel code expects, so provide a guarded fallback.
 */
#ifndef _SIGSET_T
#define _SIGSET_T
typedef unsigned long sigset_t;
#endif

/* Types used in disk, inode, etc. data structures.
 * Use the toolchain's POSIX types (dev_t, gid_t, ino_t, mode_t, nlink_t,
 * pid_t, uid_t, off_t) and keep only MINIX-specific aliases here.
 */
typedef unsigned long zone_t;	   /* zone number */
typedef unsigned long block_t;	   /* block number */
typedef unsigned long  bit_t;	   /* bit number in a bit map */
typedef unsigned short zone1_t;	   /* zone number for V1 file systems */
typedef unsigned short bitchunk_t; /* collection of bits in a bitmap */

typedef unsigned char   u8_t;	   /* 8 bit type */
typedef unsigned short u16_t;	   /* 16 bit type */
typedef unsigned long  u32_t;	   /* 32 bit type */

typedef char            i8_t;      /* 8 bit signed type */
typedef short          i16_t;      /* 16 bit signed type */
typedef long           i32_t;      /* 32 bit signed type */

/* The following types are needed because MINIX uses K&R style function
 * definitions (for maximum portability).  When a short, such as dev_t, is
 * passed to a function with a K&R definition, the compiler automatically
 * promotes it to an int.  The prototype must contain an int as the parameter,
 * not a short, because an int is what an old-style function definition
 * expects.  Thus using dev_t in a prototype would be incorrect.  It would be
 * sufficient to just use int instead of dev_t in the prototypes, but Dev_t
 * is clearer.
 */
typedef int            Dev_t;
typedef int 	       Gid_t;
typedef int 	     Nlink_t;
typedef int 	       Uid_t;
typedef int             U8_t;
typedef unsigned long  U32_t;
typedef int             I8_t;
typedef int            I16_t;
typedef long            I32_t;

/* ANSI C makes writing down the promotion of unsigned types very messy.  When
 * sizeof(short) == sizeof(int), there is no promotion, so the type stays
 * unsigned.  When the compiler is not ANSI, there is usually no loss of
 * unsignedness, and there are usually no prototypes so the promoted type
 * doesn't matter.  The use of types like Ino_t is an attempt to use ints
 * (which are not promoted) while providing information to the reader.
 */

#ifndef _ANSI_H
#include <ansi.h>
#endif

#if _EM_WSIZE == 2 || !defined(_ANSI)
typedef unsigned int      Ino_t;
typedef unsigned int    Zone1_t;
typedef unsigned int Bitchunk_t;
typedef unsigned int      U16_t;
typedef unsigned int  Mode_t;

#else /* _EM_WSIZE == 4, or _EM_WSIZE undefined, or _ANSI defined */
typedef int	          Ino_t;
typedef int 	        Zone1_t;
typedef int	     Bitchunk_t;
typedef int	          U16_t;
typedef int           Mode_t;

#endif /* _EM_WSIZE == 2, etc */
 
/* Signal handler type, e.g. SIG_IGN */
#if defined(_ANSI)
typedef void (*sighandler_t) (int);
#else
typedef void (*sighandler_t)();
#endif

#endif /* _TYPES_H */

#ifndef CP32_IRQ_FRAME_H
#define CP32_IRQ_FRAME_H

#include <stdint.h>

/* Must match irq_const.h and the save/restore sequence in irq.S. */
typedef struct cp32_irq_frame {
  uint32_t a2, a3, a4, a5, a6, a7, a8;
  uint32_t a9, a10, a11, a12, a13, a14, a15;
  uint32_t reserved[2];
} cp32_irq_frame_t;

typedef char cp32_irq_frame_size_must_be_64[
    sizeof(cp32_irq_frame_t) == 64 ? 1 : -1];

#endif

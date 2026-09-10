#ifndef CP32_IRQ_FRAME_H
#define CP32_IRQ_FRAME_H

#include <stdint.h>

/*
 * Must match CP32_IRQ_FRAME_BYTES and the save sequence in irq.S.
 *
 * irq_level1 builds this temporary frame before calling C:
 *   +0  interrupted a0 (from EXCSAVE1)
 *   +4  interrupted a1/SP, captured before frame allocation
 *   +8  interrupted a2
 *   ...
 *   +60 interrupted a15
 *   +64..79 reserved padding; the assembly frame is kept 80 bytes so its
 *        16-byte alignment is preserved across the call0 boundary.
 */
typedef struct cp32_irq_frame {
  uint32_t a0;
  uint32_t a1;
  uint32_t a2;
  uint32_t a3;
  uint32_t a4;
  uint32_t a5;
  uint32_t a6;
  uint32_t a7;
  uint32_t a8;
  uint32_t a9;
  uint32_t a10;
  uint32_t a11;
  uint32_t a12;
  uint32_t a13;
  uint32_t a14;
  uint32_t a15;
  uint32_t reserved[4];
} cp32_irq_frame_t;

typedef char cp32_irq_frame_size_must_be_80[
    sizeof(cp32_irq_frame_t) == 80 ? 1 : -1];
typedef char cp32_irq_frame_a15_offset_must_be_60[
    __builtin_offsetof(cp32_irq_frame_t, a15) == 60 ? 1 : -1];

#endif

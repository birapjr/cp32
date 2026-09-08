#include "kernel.h"
#include "proto.h"
#include "glo.h"
#include "type.h"

struct memory mem[3];
phys_clicks tot_mem_size;

void mem_init() {
  /* For now, we assume memory is already set up by the bootloader/linker.
   * In a real implementation, this would parse the memory map.
   */
  mem[0].base = 0;
  mem[0].size = 0;
  mem[1].base = 0;
  mem[1].size = 0;
  mem[2].base = 0;
  mem[2].size = 0;
  tot_mem_size = 0;
}

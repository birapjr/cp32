/* MINIX cache.c:get_block/invalidate responsibilities. Single FS client,
 * four clean 1 KiB blocks, FIFO replacement; no dirty/write-back buffers yet. */
#include "fs.h"

static struct {
  unsigned block, valid;
  char bytes[1024];
} blocks[4];
static unsigned victim, device_capacity;
static cp32_disk_reader device_reader;

CP32_IRAM_EXT void cp32_cache_invalidate(void)
{
  unsigned i;
  for(i=0;i<4;i++) blocks[i].valid=0;
  victim=0;
}

/* Parser transfers are at most 64 bytes. Stage even cross-block reads so
 * an I/O failure never publishes partial output or a partially filled block.
 * The backing reader must be uncached and must not reenter this cache. */
CP32_IRAM_EXT int cp32_cache_read(cp32_disk_reader read, unsigned capacity,
                                 unsigned offset, char *out, int count)
{
  char staged[64];
  unsigned done=0, i, slot, block, within, chunk, available;
  if(!read || count<0 || count>64 || (count && !out) || offset>capacity ||
      (unsigned)count>capacity-offset) return -CP32_SUPER_INVALID;
  if(!count) return 0;
  if(read!=device_reader || capacity!=device_capacity) {
    cp32_cache_invalidate(); device_reader=read; device_capacity=capacity;
  }
  while(done<(unsigned)count) {
    block=(offset+done)/1024; within=(offset+done)%1024;
    for(slot=0;slot<4;slot++)
      if(blocks[slot].valid && blocks[slot].block==block) break;
    if(slot==4) {
      slot=victim; victim=(victim+1)%4;
      blocks[slot].valid=0;
      available=capacity-block*1024;
      if(available>1024) available=1024;
      if(read(block*1024,blocks[slot].bytes,available)!=(int)available)
        return -CP32_SUPER_IO;
      blocks[slot].block=block; blocks[slot].valid=1;
    }
    chunk=(unsigned)count-done;
    if(chunk>1024-within) chunk=1024-within;
    for(i=0;i<chunk;i++) staged[done+i]=blocks[slot].bytes[within+i];
    done+=chunk;
  }
  for(i=0;i<done;i++) out[i]=staged[i];
  return count;
}

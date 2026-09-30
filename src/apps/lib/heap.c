/* MINIX lib/ansi/malloc.c grows a heap through sbrk and merges freed slots.
 * Small CP32 bootstrap adaptation: first-fit list, split/coalesce, no errno,
 * or threading. Metadata lives in the application's own heap. */
#include "heap.h"
#include <stdint.h>
struct block { unsigned size, free; struct block *next; };
#define HEADER ((sizeof(struct block)+15U)&~15U)
static struct block *first, *last;
static const struct cp32_app_services *api;
int cp32_heap_init(const struct cp32_app_services *services)
{
  void *base;
  if(api || !services || services->version<4 || !services->sbrk) return -1;
  base=services->sbrk(0);
  if(base==(void *)-1 || ((uintptr_t)base&15U)) return -1;
  api=services;
  return 0;
}
void *cp32_malloc(unsigned size)
{
  struct block *b;
  unsigned need;
  if(!api || !size || size>0x7fffffffU-HEADER-15U) return 0;
  need=(size+15U)&~15U;
  for(b=first;b;b=b->next) if(b->free && b->size>=need) break;
  if(!b) {
    b=api->sbrk((int)(HEADER+need));
    if(b==(void *)-1) return 0;
    b->size=need; b->next=0;
    if(last) last->next=b; else first=b;
    last=b;
  } else if(b->size-need>=HEADER+16U) {
    struct block *tail=(struct block *)((unsigned char *)b+HEADER+need);
    tail->size=b->size-need-HEADER; tail->free=1; tail->next=b->next;
    b->size=need; b->next=tail;
    if(last==b) last=tail;
  }
  b->free=0;
  return (unsigned char *)b+HEADER;
}
void cp32_free(void *pointer)
{
  struct block *b;
  if(!pointer) return;
  /* Locate exact payloads without dereferencing a caller-supplied pointer. */
  for(b=first;b;b=b->next)
    if((unsigned char *)b+HEADER==pointer) break;
  if(!b || b->free) return;
  b->free=1;
  for(b=first;b && b->next;) {
    struct block *next=b->next;
    if(b->free && next->free && (unsigned char *)b+HEADER+b->size==(unsigned char *)next) {
      b->size+=HEADER+next->size; b->next=next->next;
      if(last==next) last=b;
    } else b=next;
  }
}

void *cp32_calloc(unsigned count,unsigned size)
{
  unsigned total,i;
  unsigned char *p;
  if(!count || !size || count>~0U/size) return 0;
  total=count*size;
  p=cp32_malloc(total);
  if(p) for(i=0;i<total;i++) p[i]=0;
  return p;
}
void *cp32_realloc(void *pointer,unsigned size)
{
  struct block *b;
  unsigned char *p;
  unsigned i;
  if(!pointer) return cp32_malloc(size);
  for(b=first;b;b=b->next)
    if((unsigned char *)b+HEADER==pointer) break;
  if(!b || b->free) return 0;
  if(!size) { cp32_free(pointer); return 0; }
  if(size>0x7fffffffU-HEADER-15U) return 0;
  unsigned need=(size+15U)&~15U;
  struct block *next=b->next;
  /* Consume the neighbor only when it satisfies the entire request, so
   * failed fallback allocation leaves both block lists untouched. */
  if(need>b->size && next && next->free &&
     (unsigned char *)b+HEADER+b->size==(unsigned char *)next &&
     need<=b->size+HEADER+next->size) {
    b->size+=HEADER+next->size; b->next=next->next;
    if(last==next) last=b;
  }
  if(need<=b->size) {
    if(b->size-need>=HEADER+16U) {
      struct block *tail=(struct block *)((unsigned char *)b+HEADER+need);
      tail->size=b->size-need-HEADER; tail->free=0; tail->next=b->next;
      b->size=need; b->next=tail;
      if(last==b) last=tail;
      cp32_free((unsigned char *)tail+HEADER);
    }
    return pointer;
  }
  p=cp32_malloc(size);
  if(!p) return 0;
  for(i=0;i<b->size;i++) p[i]=((unsigned char *)pointer)[i];
  cp32_free(pointer);
  return p;
}

/* Explicit top-of-heap release. Never discard metadata until sbrk succeeds. */
int cp32_heap_trim(void)
{
  struct block *previous=0,*b;
  unsigned bytes;
  void *top;
  if(!api) return -1;
  if(!last || !last->free) return 0;
  for(b=first;b && b!=last;b=b->next) previous=b;
  bytes=HEADER+last->size;
  top=(unsigned char *)last+bytes;
  if(api->sbrk(0)!=top || api->sbrk(-(int)bytes)==(void *)-1) return -1;
  if(previous) previous->next=0; else first=0;
  last=previous;
  return (int)bytes;
}

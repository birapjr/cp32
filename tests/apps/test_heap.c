#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "heap.h"
static unsigned char arena[12288] __attribute__((aligned(16)));
static unsigned used;
static int reject_shrink;
static void *grow(int n) {
 if(n<0) {
   unsigned amount=(unsigned)(-(n+1))+1;
   if(reject_shrink || amount>used) return (void *)-1;
   void *old=arena+used;used-=amount;return old;
 }
 if((unsigned)n>sizeof(arena)-used) return (void *)-1;
 void *p=arena+used; used+=(unsigned)n; return p;
}
int main(void) {
 struct cp32_app_services s={4,0,0,0,0,grow,0};
 assert(!cp32_malloc(8)); assert(cp32_heap_init(0)==-1);
 assert(!cp32_heap_init(&s)); assert(cp32_heap_init(&s)==-1);
 unsigned char *a=cp32_malloc(100),*b=cp32_malloc(80),*c=cp32_malloc(33);
 assert(a && b && c && !((uintptr_t)a&15) && !((uintptr_t)c&15));
 memset(b,0x5a,80); unsigned before=used;
 cp32_free(a); unsigned char *small=cp32_malloc(16);
 assert(small==a && used==before); /* split */
 for(unsigned i=0;i<80;i++) assert(b[i]==0x5a);
 cp32_free(small); cp32_free(c); cp32_free(b);
 assert(cp32_malloc(240)==a && used==before); /* bidirectional merging */
 assert(!cp32_malloc(0) && !cp32_malloc(0xffffffffU) && used==before);
 cp32_free(a); cp32_free(0);
 void *blocks[128]; unsigned count=0;
 while(count<128 && (blocks[count]=cp32_malloc(128))) count++;
 assert(count>10 && count<128); unsigned full=used;
 assert(!cp32_malloc(12288) && used==full);
 for(unsigned i=0;i<count;i+=2) cp32_free(blocks[i]);
 for(unsigned i=1;i<count;i+=2) cp32_free(blocks[i]);
 assert(cp32_malloc(11000)==a && used==full);
 cp32_free(a);
 unsigned char *zero=cp32_calloc(4,8);
 assert(zero); for(unsigned i=0;i<32;i++) assert(!zero[i]);
 memset(zero,0x3c,32);
 unsigned char *guard=cp32_malloc(64); assert(guard);memset(guard,0x7a,64);
 unsigned char *larger=cp32_realloc(zero,96);assert(larger && larger!=zero);
 for(unsigned i=0;i<32;i++) assert(larger[i]==0x3c);
 for(unsigned i=0;i<64;i++) assert(guard[i]==0x7a);
 assert(!cp32_realloc(larger,12288) && !cp32_realloc(larger,~0U));
 for(unsigned i=0;i<32;i++) assert(larger[i]==0x3c);
 assert(cp32_realloc(larger,16)==larger);
 assert(!cp32_realloc(larger,0));cp32_free(guard);
 assert(!cp32_calloc(0x80000000U,2) && !cp32_calloc(0,8));
 zero=cp32_calloc(8,16);assert(zero);
 for(unsigned i=0;i<128;i++) assert(!zero[i]);
 cp32_free(zero);zero=cp32_realloc(0,33);assert(zero);cp32_free(zero);
 a=cp32_malloc(32);b=cp32_malloc(96);c=cp32_malloc(32);
 assert(a && b && c);memset(a,0x19,32);memset(c,0x71,32);
 before=used;cp32_free(b);
 assert(cp32_realloc(a,112)==a && used==before);
 for(unsigned i=0;i<32;i++) assert(a[i]==0x19 && c[i]==0x71);
 assert(cp32_realloc(a,16)==a);
 b=cp32_malloc(64);assert(b && used==before);
 cp32_free(a);cp32_free(b);cp32_free(c);
 before=used;reject_shrink=1;
 assert(cp32_heap_trim()==-1 && used==before);reject_shrink=0;
 assert(cp32_heap_trim()==(int)before && !used && !cp32_heap_trim());
 a=cp32_malloc(32);before=used;b=cp32_malloc(64);
 assert(a && b && !cp32_heap_trim());memset(a,0x2a,32);
 cp32_free(b);assert(cp32_heap_trim()>0 && used==before);
 for(unsigned i=0;i<32;i++) assert(a[i]==0x2a);
 cp32_free(a);assert(cp32_heap_trim()>0 && !used);
 return 0;
}

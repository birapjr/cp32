#include <assert.h>
#include <string.h>
#include "io.h"
static char source[600],result[600];
static unsigned done,calls;
static int mode;
static const char *line;static unsigned at;static int fail_at=-1;
static int output(const char *p,unsigned n) {
 assert(n<=256 && p==source+done);calls++;
 if(mode==1 && done>=100) return -1;
 if(mode==2) return 0;
 if(mode==3) return n+1;
 if(n>100) n=100;
 memcpy(result+done,p,n);done+=n;return n;
}
static int input(char *p,unsigned n) {
 if(line) {assert(n==1);if((int)at==fail_at)return -1; if(!line[at])return 0; *p=line[at++];return 1;}
 assert(n==64); if(mode==1)return -1;if(mode==2)return 0;if(mode==3)return 65;
 memcpy(p,"ok\n",3);return 3;
}
int main(void) {
 struct cp32_app_services s={5,output,0,0,0,0,input};char b[128];
 assert(cp32_write(1,source,1)==-1 && cp32_io_init(0)==-1);
 assert(!cp32_io_init(&s) && cp32_io_init(&s)==-1);
 memset(source,'a',sizeof(source));
 assert(cp32_write(1,source,600)==600 && calls==6 && !memcmp(source,result,600));
 calls=done=0;mode=1;assert(cp32_write(2,source,600)==100 && calls==2);
 done=0;mode=2;assert(cp32_write(1,source,600)==0);
 mode=3;assert(cp32_write(1,source,600)==-1);
 assert(cp32_write(0,source,1)==-1 && cp32_write(1,0,1)==-1);
 assert(cp32_write(1,source,~0U)==-1 && !cp32_write(1,0,0));
 mode=0;assert(cp32_read(0,b,sizeof(b))==3 && !memcmp(b,"ok\n",3));
 mode=1;assert(cp32_read(0,b,64)==-1);
 mode=2;assert(!cp32_read(0,b,64));mode=3;assert(cp32_read(0,b,64)==-1);
 assert(cp32_read(1,b,1)==-1 && cp32_read(0,0,1)==-1 && !cp32_read(0,0,0));
 line="abcdef\nnext\n";at=0;
 assert(cp32_readline(b,4)==3 && !strcmp(b,"abc"));
 assert(cp32_readline(b,8)==4 && !strcmp(b,"def\n"));
 assert(cp32_readline(b,8)==5 && !strcmp(b,"next\n"));
 assert(!cp32_readline(b,8) && !b[0]);
 line="\n";at=0;assert(cp32_readline(b,2)==1 && !strcmp(b,"\n"));
 line="partial";at=0;fail_at=3;
 assert(cp32_readline(b,16)==-1 && !strcmp(b,"par"));
 assert(cp32_readline(b,1)==-1 && cp32_readline(0,16)==-1);
 return 0;
}

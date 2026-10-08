#include <assert.h>
#include <string.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static const char *text="one two\nthree\tlast";
static unsigned at,used;static char output[128];static int mode;
static int input(char *p,unsigned n) {
 assert(n==64);if(mode==1)return -1;
 if(!text[at])return 0;*p=text[at++];return 1; /* every word crosses reads */
}
static int write_text(const char *p,unsigned n) {
 if(mode==2)return -1;
 if(n>2)n=2;
 assert(used+n<sizeof(output));memcpy(output+used,p,n);used+=n;return n;
}
int main(int argc,char **argv) {
 struct cp32_app_services s={5,write_text,0,0,0,0,input,0};
 char *args[]={"wc",0,0};unsigned count=1;
 if(argc>1)mode=argv[1][0]-'0';
 if(mode==3){args[1]="-cw";count=2;}
 if(mode==4){args[1]="-z";count=2;}
 if(mode==5)text="";
 if(mode==6)text=" \t\r\n\v\f ";
 int r=app_main(count,args,0,&s);
 if(mode==1 || mode==2)assert(r==1);
 else if(mode==4)assert(r==2 && !strcmp(output,"Usage: run /boot/wc [-lwc] [file ...]\n"));
 else {assert(!r);assert(!strcmp(output,mode==3 ? "4 18\n":mode==5 ? "0 0 0\n":mode==6 ? "1 0 7\n":"1 4 18\n"));}
 return 0;
}

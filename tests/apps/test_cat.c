#include <assert.h>
#include <string.h>
#include "io.h"
int app_main(unsigned,char **,char **,const struct cp32_app_services *);
static const char text[]="first line\nsecond line\n";
static char captured[128];static unsigned at,used;static int mode;
static int input(char *p,unsigned n) {
 assert(n==64);if(mode==1)return -1;
 if(!text[at])return 0;
 unsigned amount=strlen(text+at);if(amount>3)amount=3;
 memcpy(p,text+at,amount);at+=amount;return amount;
}
static int output(const char *p,unsigned n) {
 if(mode==2)return -1;
 if(n>2)n=2;assert(used+n<sizeof(captured));memcpy(captured+used,p,n);used+=n;return n;
}
int main(int argc,char **argv) {
 struct cp32_app_services s={5,output,0,0,0,0,input,0};
 if(argc>1)mode=argv[1][0]-'0';
 int result=app_main(mode==3 ? 2:1,argv,0,&s);
 if(mode==1 || mode==2)assert(result==1);
 else if(mode==3)assert(result==1 && !strcmp(captured,"cat: cannot open file\n"));
 else assert(!result && !strcmp(captured,text));
 return 0;
}

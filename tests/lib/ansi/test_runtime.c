#include <stdio.h>
#include "../../../src/lib/ansi/memcpy.c"
#include "../../../src/lib/ansi/memset.c"
#include "../../../src/lib/ansi/strcpy.c"
#include "../../../src/lib/ansi/strcmp.c"
#include "../../../src/lib/ansi/strtol.c"
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d\n", __FILE__, __LINE__); failures++; } } while (0)
static void signed_case(const char *text,int base,long expected,unsigned end,int error)
{
  char *stop;
  errno=123;
  CHECK(strtol(text,&stop,base)==expected);
  CHECK(stop==text+end);
  CHECK(errno==(error ? error : 123));
}
static void unsigned_case(const char *text,int base,unsigned long expected,unsigned end,int error)
{
  char *stop;
  errno=123;
  CHECK(strtoul(text,&stop,base)==expected);
  CHECK(stop==text+end);
  CHECK(errno==(error ? error : 123));
}
static unsigned encode(unsigned long long value,unsigned base,char *out)
{
  char reversed[80];
  unsigned length=0,i;
  do { reversed[length++]="0123456789abcdefghijklmnopqrstuvwxyz"[value%base]; value/=base; } while(value);
  for(i=0;i<length;i++) out[i]=reversed[length-i-1];
  out[length]='!'; out[length+1]=0;
  return length;
}
static void conversions(void)
{
  unsigned base,n;
  char text[84];
  signed_case("",0,0,0,0);
  signed_case(" \t\n-",0,0,0,0);
  signed_case(" +z",10,0,0,0);
  signed_case("0",0,0,1,0);
  signed_case("09",0,0,1,0);
  signed_case("0x",0,0,1,0);
  signed_case("-0Xg",16,0,2,0);
  signed_case(" \t\n\r\f\v+0x2Az",0,42,11,0);
  signed_case("12\xff",10,12,2,0);
  signed_case("0b11",0,0,1,0); /* MINIX predates binary-prefix extensions. */
  signed_case("0000000000000000000000000000000000000001x",10,1,40,0);
  signed_case("999999999999999999999999999999999999999x",10,LONG_MAX,39,ERANGE);
  unsigned_case("-4294967296!",10,ULONG_MAX,11,ERANGE);
  unsigned_case("-1!",10,ULONG_MAX,2,0);
  unsigned_case("+",0,0,0,0);
  for(base=2;base<=36;base++) {
    n=encode((unsigned long long)LONG_MAX,base,text);
    signed_case(text,base,LONG_MAX,n,0);
    n=encode((unsigned long long)LONG_MAX+1,base,text);
    signed_case(text,base,LONG_MAX,n,ERANGE);
    text[0]='-'; n=encode((unsigned long long)LONG_MAX+1,base,text+1)+1;
    signed_case(text,base,LONG_MIN,n,0);
    n=encode((unsigned long long)LONG_MAX+2,base,text+1)+1;
    signed_case(text,base,LONG_MIN,n,ERANGE);
    n=encode((unsigned long long)ULONG_MAX,base,text);
    unsigned_case(text,base,ULONG_MAX,n,0);
    n=encode((unsigned long long)ULONG_MAX+1,base,text);
    unsigned_case(text,base,ULONG_MAX,n,ERANGE);
    text[0]='-'; n=encode((unsigned long long)ULONG_MAX,base,text+1)+1;
    unsigned_case(text,base,1,n,0);
  }
  signed_case("7",1,0,0,EINVAL);
  signed_case("7",37,0,0,EINVAL);
  unsigned_case("7",-1,0,0,EINVAL);
  errno=ERANGE;
  CHECK(strtoul("-1",0,10)==ULONG_MAX && errno==ERANGE);
  CHECK(strtol("-2147483648",0,10)==LONG_MIN);
}
int main(void)
{
  char dst[16], copy[16], *end;
  memset(dst, 'x', sizeof(dst)); CHECK(dst[0] == 'x' && dst[15] == 'x');
  strcpy(dst, "cp32"); CHECK(strcmp(dst, "cp32") == 0);
  memcpy(copy, dst, 5); CHECK(strcmp(copy, "cp32") == 0);
  CHECK(strcmp("abc", "abd") < 0); CHECK(strcmp("abd", "abc") > 0);
  CHECK(strcmp("same", "same") == 0);
  CHECK(strtol("42", &end, 10) == 42 && *end == '\0');
  CHECK(strtol(" -17x", &end, 10) == -17 && *end == 'x');
  CHECK(strtol("0x2a", &end, 0) == 42 && *end == '\0');
  CHECK(strtol("077", &end, 0) == 63 && *end == '\0');
  CHECK(strtol("0Xff", &end, 16) == 255 && *end == '\0');
  CHECK(strtol("z", &end, 10) == 0 && *end == 'z');
  conversions();
  return failures != 0;
}

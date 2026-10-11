"""Run the production patch command parser with a recording filesystem boundary."""
from pathlib import Path
import subprocess, sys, tempfile
sys.dont_write_bytecode=True
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tests'))
from test_idle_handoff import extract_function
pre=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CP32_IRAM_EXT
#define CP32_MINIX_PATH_MAX 255
#define CP32_SUPER_IO 4
static char cp32_shell_cwd[]="/",output[512],path_seen[256],text_seen[65];
static unsigned calls,offset_seen;static int result=5;
static void cp32_shell_print(const char *p){strcat(output,p);}
static void cp32_shell_print_u32(uint32_t v){char p[16];snprintf(p,sizeof(p),"%u",v);strcat(output,p);}
static int cp32_minix_abspath(const char *cwd,const char *p,char *out){(void)cwd;strcpy(out,p);return 0;}
static int cp32_shell_disk_read(unsigned o,char *p,int n){(void)o;(void)p;return n;}
static int cp32_shell_disk_write(unsigned o,const char *p,int n){(void)o;(void)p;return n;}
static unsigned cp32_root_capacity(void){return 65536;}
static int cp32_minix_patch(int (*read)(unsigned,char *,int),int (*write)(unsigned,const char *,int),
 unsigned capacity,const char *path,unsigned offset,const char *text,unsigned n){
 assert(read==cp32_shell_disk_read && write==cp32_shell_disk_write && capacity==65536);
 calls++;strcpy(path_seen,path);memcpy(text_seen,text,n);text_seen[n]=0;offset_seen=offset;return result;
}
static int cp32_minix_create_root(int (*read)(unsigned,char *,int),int (*write)(unsigned,const char *,int),
 unsigned capacity,const char *name,const char *text,unsigned n){
 assert(read==cp32_shell_disk_read && write==cp32_shell_disk_write && capacity==65536);
 calls++;strcpy(path_seen,name);memcpy(text_seen,text,n);text_seen[n]=0;return result;
}
static int cp32_minix_append(int (*read)(unsigned,char *,int),int (*write)(unsigned,const char *,int),
 unsigned capacity,const char *name,const char *text,unsigned n){
 return cp32_minix_create_root(read,write,capacity,name,text,n);
}
static int cp32_minix_truncate(int (*read)(unsigned,char *,int),int (*write)(unsigned,const char *,int),
 unsigned capacity,const char *name,unsigned length){
 assert(read==cp32_shell_disk_read && write==cp32_shell_disk_write && capacity==65536);
 calls++;strcpy(path_seen,name);offset_seen=length;return result;
}
'''
pre += r'''
static int cp32_minix_link_root(int (*read)(unsigned,char *,int),int (*write)(unsigned,const char *,int),
 unsigned capacity,const char *path,const char *name){
 assert(read==cp32_shell_disk_read && write==cp32_shell_disk_write && capacity==65536);
 calls++;strcpy(path_seen,path);strcpy(text_seen,name);return result;
}
'''
pre += r'''
static int cp32_minix_unlink(int (*read)(unsigned,char *,int),int (*write)(unsigned,const char *,int),
 unsigned capacity,const char *path){
 assert(read==cp32_shell_disk_read && write==cp32_shell_disk_write && capacity==65536);
 calls++;strcpy(path_seen,path);return result;
}
'''
post=r'''
int main(void){
 cp32_shell_patch("/readme 0 SD125");
 assert(calls==1 && !strcmp(path_seen,"/readme") && !strcmp(text_seen,"SD125") && !offset_seen);
 assert(!strcmp(output,"Patch verified: 5 bytes\r\n"));
 const char *bad[]={"","/readme","/readme -1 x","/readme 2147483648 x","/readme 1x y","/readme 0 ",
 "/readme 0 12345678901234567890123456789012345678901234567890123456789012345"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
   output[0]=0;cp32_shell_patch(bad[i]);assert(calls==1 && strstr(output,"Usage:"));
 }
 output[0]=0;result=-4;cp32_shell_patch("\t/readme\t17\t two words");
 assert(calls==2 && offset_seen==17 && !strcmp(text_seen," two words"));
 assert(strstr(output,"Patch failed: 4") && strstr(output,"media may have changed"));
 calls=0;output[0]=0;result=5;
 cp32_shell_create("/note hello");
 assert(calls==1 && !strcmp(path_seen,"note") && !strcmp(text_seen,"hello"));
 assert(!strcmp(output,"Created /note: 5 bytes\r\n"));
 const char *bad_create[]={"","note hello","/parent/note hello","/123456789012345 x","/note", "/note ",
 "/note 12345678901234567890123456789012345678901234567890123456789012345"};
 for(unsigned i=0;i<sizeof(bad_create)/sizeof(*bad_create);i++) {
   output[0]=0;cp32_shell_create(bad_create[i]);assert(calls==1 && strstr(output,"Usage:"));
 }
 output[0]=0;result=-4;cp32_shell_create(" /note  hello");
 assert(calls==2 && !strcmp(text_seen," hello") && strstr(output,"inspect after reboot"));
 calls=0;output[0]=0;result=5;cp32_shell_append("/note hello");
 assert(calls==1 && !strcmp(path_seen,"/note") && !strcmp(text_seen,"hello"));
 assert(!strcmp(output,"Appended: 5 bytes\r\n"));
 const char *bad_append[]={"","/note","/note ",
 "/note 12345678901234567890123456789012345678901234567890123456789012345"};
 for(unsigned i=0;i<sizeof(bad_append)/sizeof(*bad_append);i++) {
   output[0]=0;cp32_shell_append(bad_append[i]);assert(calls==1 && strstr(output,"Usage:"));
 }
 output[0]=0;result=-4;cp32_shell_append("note  two words");
 assert(calls==2 && !strcmp(text_seen," two words") && strstr(output,"inspect after reboot"));
 calls=0;output[0]=0;result=0;cp32_shell_truncate("/note 0");
 assert(calls==1 && !strcmp(path_seen,"/note") && !offset_seen);
 assert(!strcmp(output,"Truncated to 0 bytes\r\n"));
 const char *bad_truncate[]={"","/note","/note -1","/note 2147483648","/note 0x10","/note 1 extra"};
 for(unsigned i=0;i<sizeof(bad_truncate)/sizeof(*bad_truncate);i++) {
   output[0]=0;cp32_shell_truncate(bad_truncate[i]);assert(calls==1 && strstr(output,"Usage:"));
 }
 output[0]=0;result=-4;cp32_shell_truncate(" note\t7 ");
 assert(calls==2 && offset_seen==7 && strstr(output,"inspect after reboot"));
 calls=0;output[0]=0;result=0;cp32_shell_link("note /alias");
 assert(calls==1 && !strcmp(path_seen,"note") && !strcmp(text_seen,"alias"));
 assert(!strcmp(output,"Linked /alias\r\n"));
 const char *bad_link[]={"","note","note alias","note /","note /a/b","note /123456789012345","note /a extra"};
 for(unsigned i=0;i<sizeof(bad_link)/sizeof(*bad_link);i++) {
   output[0]=0;cp32_shell_link(bad_link[i]);assert(calls==1 && strstr(output,"Usage:"));
 }
 output[0]=0;result=-4;cp32_shell_link(" /note\t/alias ");
 assert(calls==2 && strstr(output,"inspect after reboot"));
 calls=0;output[0]=0;result=0;cp32_shell_unlink(" /alias ");
 assert(calls==1 && !strcmp(path_seen,"/alias") && !strcmp(output,"Removed /alias\r\n"));
 const char *bad_rm[]={""," ","/alias extra","/alias/",".","..","/boot/.","/boot/.."};
 for(unsigned i=0;i<sizeof(bad_rm)/sizeof(*bad_rm);i++) {
   output[0]=0;cp32_shell_unlink(bad_rm[i]);assert(calls==1 && strstr(output,"Usage:"));
 }
 output[0]=0;result=-4;cp32_shell_unlink("alias");
 assert(calls==2 && strstr(output,"inspect after reboot"));
 puts("unlink/link/patch/create/append/truncate commands: strict offsets, length, whitespace, literal text and error reporting pass");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(pre+extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_patch')+extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_create')+extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_append')+extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_truncate')+extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_link')+extract_function(root/'src/kernel/cp32-shell.c','cp32_shell_unlink')+post)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)

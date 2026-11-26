"""ABI37 standard streams preserve interrupted-I/O errno and short progress."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
source=r'''
#include <assert.h>
#include "io.h"
static int result=-CP32_EINTR,partial,writes;
static int read_cb(char *p,unsigned n){(void)p;(void)n;return result;}
static int write_cb(const char *p,unsigned n){(void)p;(void)n;writes++;return partial && writes==1 ? 2:result;}
int main(void){
 struct cp32_app_services s={.version=37,.read=read_cb,.write=write_cb};char data[8]={0};
 assert(!cp32_io_init(&s));
 assert(cp32_read(0,data,1)==-1 && cp32_errno==CP32_EINTR);
 assert(cp32_write(1,data,1)==-1 && cp32_errno==CP32_EINTR);
 writes=0;partial=1;assert(cp32_write(2,data,4)==2 && writes==2 && cp32_errno==CP32_EINTR);
 partial=0;s.version=36;
 assert(cp32_read(0,data,1)==-1 && cp32_errno==CP32_EIO);
 assert(cp32_write(1,data,1)==-1 && cp32_errno==CP32_EIO);
 s.version=37;result=-CP32_EFAULT;
 assert(cp32_read(0,data,1)==-1 && cp32_errno==CP32_EFAULT);
 result=2;assert(cp32_read(0,data,1)==-1 && cp32_errno==CP32_EIO);
 assert(cp32_write(1,data,1)==-1 && cp32_errno==CP32_EIO);
 result=0;assert(!cp32_read(0,data,1) && !cp32_write(1,data,1));
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=undefined',
  '-I'+str(root/'src/apps/lib'),str(p/'test.c'),str(root/'src/apps/lib/io.c'),
  '-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('runtime I/O: EINTR/error propagation, partial write progress and legacy compatibility PASS')

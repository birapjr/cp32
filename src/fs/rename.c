/* Client cwd strings are canonical. Rebase every live client on a successful
 * directory move; validate all resulting lengths before changing disk state. */
#include "fproc.h"
#include <string.h>
#if !CP32_ROOT_RAM
CP32_FS_TEXT static int rebase(char *cwd,const char *old,const char *next,int commit)
{
  unsigned a=0,b=0,n=0,i;
  if(!cwd)return 0;
  while(old[a])a++;while(next[b])b++;while(cwd[n])n++;
  if(n<a || (cwd[a] && cwd[a]!='/'))return 0;
  for(i=0;i<a;i++)if(cwd[i]!=old[i])return 0;
  if(b+n-a>255)return -CP32_FILE_NAME;
  if(commit) {
    if(b>a)for(i=n+1;i>a;i--)cwd[b+i-a-1]=cwd[i-1];
    else for(i=a;i<=n;i++)cwd[b+i-a]=cwd[i];
    for(i=0;i<b;i++)cwd[i]=next[i];
  }
  return 0;
}
CP32_FS_TEXT int cp32_fproc_move(struct cp32_fproc *p,struct cp32_fproc *other,
    char *previous,cp32_disk_reader read,cp32_disk_writer write,unsigned capacity,
    const char *source,const char *destination)
{
  char old[256],next[256],parent[256];struct cp32_minix_stat st,target,cwd;
  unsigned i,start=0;int r,isdir;
  if(!source || !destination)return -CP32_SUPER_INVALID;
  if(!*source || !*destination)return -CP32_FILE_NOT_FOUND;
  if(!write)return -CP32_FILE_READONLY;
  r=cp32_minix_abspath(p->cwd,source,old);if(r)return r;
  r=cp32_minix_abspath(p->cwd,destination,next);if(r)return r;
  r=cp32_minix_stat(read,capacity,old,&st);if(r)return r;
  isdir=(st.mode&0170000)==0040000;
  if(isdir) {
    /* Reject dot/final slash before normalization could hide them. */
    for(i=0;old[i];i++)if(old[i]=='/')start=i+1;
    if(!old[start] || !strcmp(old+start,".") || !strcmp(old+start,".."))return -CP32_SUPER_INVALID;
    parent[0]='/';parent[1]=0;
    r=cp32_minix_chdir(read,capacity,parent,old);if(r)return r;
    strcpy(old,parent);start=0;
    for(i=0;next[i];i++)if(next[i]=='/')start=i+1;
    if(!next[start] || !strcmp(next+start,".") || !strcmp(next+start,".."))return -CP32_SUPER_INVALID;
    for(i=0;i<start;i++)parent[i]=next[i];parent[start]=0;
    /* Reuse local temporary only after preserving the final component. */
    { char name[15];unsigned n=0;
      while(next[start+n]){if(n==14)return -CP32_FILE_NAME;name[n]=next[start+n];n++;}name[n]=0;
      next[0]='/';next[1]=0;r=cp32_minix_chdir(read,capacity,next,parent);if(r)return r;
      r=cp32_minix_abspath(next,name,parent);if(r)return r;strcpy(next,parent);
    }
    r=cp32_minix_stat(read,capacity,next,&target);
    if(!r && target.inode!=st.inode) {
      r=cp32_minix_stat(read,capacity,p->cwd,&cwd);if(r)return r;
      if(cwd.inode==target.inode)return -CP32_FILE_BUSY;
      if(other && other->pid) {
        r=cp32_minix_stat(read,capacity,other->cwd,&cwd);if(r)return r;
        if(cwd.inode==target.inode)return -CP32_FILE_BUSY;
      }
    } else if(r && r!=-CP32_FILE_NOT_FOUND)return r;
    r=rebase(p->cwd,old,next,0);if(r)return r;
    if(other && other->pid){r=rebase(other->cwd,old,next,0);if(r)return r;}
    r=rebase(previous,old,next,0);if(r)return r;
  }
  r=cp32_minix_move(read,write,capacity,old,next);if(r || !isdir)return r;
  rebase(p->cwd,old,next,1);
  if(other && other->pid)rebase(other->cwd,old,next,1);
  rebase(previous,old,next,1);return 0;
}

#else
CP32_FS_TEXT int cp32_fproc_move(struct cp32_fproc *p,struct cp32_fproc *other,
    char *previous,cp32_disk_reader read,cp32_disk_writer write,unsigned capacity,
    const char *source,const char *destination)
{ (void)p;(void)other;(void)previous;(void)read;(void)write;(void)capacity;
  (void)source;(void)destination;return -CP32_FILE_READONLY; }
#endif

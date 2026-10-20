/* MINIX super.c:alloc_bit, inode.c:alloc_inode, open.c:new_node.
 * Single FS owner; synchronous writes, inode/data durable before directory
 * publication. An interrupted operation may leak reservations; never reclaim
 * them after uncertain I/O. No rollback or power-loss atomicity is promised. */
#include "fs.h"
CP32_FS_TEXT static void put16(unsigned char *p,unsigned n,unsigned swap)
{ p[swap ? 1:0]=(unsigned char)n;p[swap ? 0:1]=(unsigned char)(n>>8); }
CP32_FS_TEXT static void put32(unsigned char *p,unsigned n,unsigned swap)
{
  put16(p,swap ? n>>16:n,swap);
  put16(p+2,swap ? n:n>>16,swap);
}
CP32_FS_TEXT static int store(cp32_disk_writer write,unsigned at,
                               const unsigned char *p,unsigned n)
{
  int r;
  cp32_cache_invalidate();r=write(at,(const char *)p,n);cp32_cache_invalidate();
  return r==(int)n ? 0:-CP32_SUPER_IO;
}
/* Skip reserved bit zero, scan only real inode/zone bits, not map padding. */
CP32_FS_TEXT static int find_free_bit(cp32_disk_reader read,unsigned map,
                                  unsigned first,unsigned last,unsigned swap,unsigned *found)
{
  unsigned bit,word=0;unsigned char raw[2];
  for(bit=first;bit<=last;bit++) {
    if(bit==first || !(bit%16)) {
      if(read(map+(bit/16)*2,(char *)raw,2)!=2)return -CP32_SUPER_IO;
      word=cp32_fs_u16(raw,swap);
    }
    if(!(word&(1U<<(bit%16)))) {*found=bit;return 0;}
  }
  return -CP32_FILE_NOSPACE;
}
CP32_FS_TEXT static int reserve(cp32_disk_reader read,cp32_disk_writer write,
                                 unsigned map,unsigned bit,unsigned swap)
{
  unsigned char raw[2];unsigned at=map+(bit/16)*2,word;
  if(read(at,(char *)raw,2)!=2)return -CP32_SUPER_IO;
  word=cp32_fs_u16(raw,swap);
  if(word&(1U<<(bit%16)))return -CP32_SUPER_INVALID;
  put16(raw,word|(1U<<(bit%16)),swap);return store(write,at,raw,2);
}
/* Resolve a mutation target in an existing one-block directory. slot is either
 * the matching entry, the first empty slot, or EOF; found is zero if absent. */
CP32_FS_TEXT static int mutation_location(cp32_disk_reader read,unsigned capacity,
    const char *path,struct cp32_minix_dir *dir,unsigned *parent_inode,
    char name[15],unsigned *slot,unsigned *found)
{
  char parent[CP32_MINIX_PATH_MAX+1];unsigned char raw[16];
  unsigned length,start=0,n,i,at,ino;int r;
  if(!path)return -CP32_FILE_NAME;
  for(length=0;path[length];length++) {
    if(length==CP32_MINIX_PATH_MAX || path[length]<0x20 || path[length]>0x7e)
      return -CP32_FILE_NAME;
    if(path[length]=='/')start=length+1;
  }
  n=length-start;
  if(!n || n>14 || (n==1 && path[start]=='.') ||
     (n==2 && path[start]=='.' && path[start+1]=='.'))return -CP32_FILE_NAME;
  for(i=0;i<n;i++)name[i]=path[start+i];name[n]=0;
  if(start) {for(i=0;i<start;i++)parent[i]=path[i];parent[start]=0;}
  else {parent[0]='/';parent[1]=0;}
  r=cp32_fs_resolve_path(read,capacity,parent,&dir->super,parent_inode);if(r)return r;
  r=cp32_fs_open_directory(read,&dir->super,*parent_inode,dir);if(r)return r;
  if(dir->super.log_zone_size || dir->size>1024)return -CP32_SUPER_UNSUPPORTED;
  *slot=dir->size;*found=0;
  for(at=32;at<dir->size;at+=16) {
    if(read(dir->zones[0]*1024+at,(char *)raw,16)!=16)return -CP32_SUPER_IO;
    ino=cp32_fs_u16(raw,dir->super.swapped);
    if(!ino) {if(*slot==dir->size)*slot=at;continue;}
    for(i=0;i<n && raw[2+i]==(unsigned char)name[i];i++){}
    if(i==n && (n==14 || !raw[2+n])) {*slot=at;*found=ino;return 0;}
  }
  return 0;
}

/* MINIX open.c:new_node resolves the parent before allocating and publishes
 * the initialized inode before entering its name. Root and nested paths share
 * the bounded directory lookup also used by rename; no new directory zones. */
CP32_FS_TEXT static int create_node(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,const char *bytes,unsigned n,int directory,unsigned mode)
{
  struct cp32_minix_dir dir;
  struct cp32_minix_super *s=&dir.super;
  unsigned char raw[64],entry[16],parent[64];char name[15];
  unsigned length,i,at,slot,ino,zbit,zone,imap=2048,zmap,itable,base,parent_inode,found;
  int r;
  if(!read || !write || !path || (!directory && n && !bytes) || n>64)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=mutation_location(read,capacity,path,&dir,&parent_inode,name,&slot,&found);
  if(r)return r;
  if(found)return -CP32_FILE_EXISTS;
  if(n>s->max_size)return -CP32_SUPER_UNSUPPORTED;
  for(length=0;name[length];length++){}
  base=dir.zones[0]*1024;
  if(slot==1024 || (slot==dir.size && dir.size+16>s->max_size))
    return -CP32_FILE_NOSPACE;
  zmap=(2+s->imap_blocks)*1024;
  itable=(2+s->imap_blocks+s->zmap_blocks)*1024;
  if(read(itable+(parent_inode-1)*64,(char *)parent,64)!=64)return -CP32_SUPER_IO;
  if(directory && cp32_fs_u16(parent+2,s->swapped)>=127)
    return -CP32_SUPER_UNSUPPORTED;
  r=find_free_bit(read,imap,1,s->ninodes,s->swapped,&ino);if(r)return r;
  zone=zbit=0;
  if(n) {
    r=find_free_bit(read,zmap,1,s->zones-s->first_data_zone,s->swapped,&zbit);if(r)return r;
    zone=s->first_data_zone+zbit-1;
  }
  /* Complete capacity/name/mapping checks precede all mutations. Clear stale
   * inode fields while unallocated, then reserve inode and zone separately. */
  for(i=0;i<64;i++)raw[i]=0;
  r=store(write,itable+(ino-1)*64,raw,64);if(r)return r;
  r=reserve(read,write,imap,ino,s->swapped);if(r)return r;
  if(n) {r=reserve(read,write,zmap,zbit,s->swapped);if(r)return r;}
  for(at=0;n && at<1024;at+=64) {
    r=store(write,zone*1024+at,raw,64);if(r)return r;
  }
  if(directory) {
    /* MINIX do_mkdir: dot names reference this inode and its actual parent.
     * Fully initialize them before exposing the new directory to lookup. */
    put16(raw,ino,s->swapped);raw[2]='.';
    put16(raw+16,parent_inode,s->swapped);raw[18]=raw[19]='.';
    r=store(write,zone*1024,raw,32);
    for(i=0;i<64;i++)raw[i]=0;
  } else r=n ? store(write,zone*1024,(const unsigned char *)bytes,n):0;
  if(r)return r;
  put16(raw,directory ? (0040000|(mode&0777)):0100644,s->swapped);
  put16(raw+2,directory ? 2:1,s->swapped);
  put32(raw+8,n,s->swapped);put32(raw+24,zone,s->swapped);
  /* uid/gid and unavailable wall-clock timestamps remain zero. */
  r=store(write,itable+(ino-1)*64,raw,64);if(r)return r;
  if(directory) {
    /* Count the child's dot-dot before publication. On failure an overcount
     * may remain; do not risk recycling resources after uncertain I/O. */
    put16(parent+2,cp32_fs_u16(parent+2,s->swapped)+1,s->swapped);
    r=store(write,itable+(parent_inode-1)*64,parent,64);if(r)return r;
  }
  for(i=0;i<16;i++)entry[i]=0;
  put16(entry,ino,s->swapped);
  for(i=0;i<length;i++)entry[2+i]=(unsigned char)name[i];
  r=store(write,base+slot,entry,16);if(r)return r;
  if(slot==dir.size) {
    put32(parent+8,dir.size+16,s->swapped);
    r=store(write,itable+(parent_inode-1)*64,parent,64);if(r)return r;
  }
  return (int)n;
}

CP32_FS_TEXT int cp32_minix_create(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,const char *bytes,unsigned n)
{ return create_node(read,write,capacity,path,bytes,n,0,0644); }

/* MINIX open.c:do_mkdir, fixed0755 without a process umask/credential model. */
CP32_FS_TEXT int cp32_minix_mkdir(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path)
{ int r=create_node(read,write,capacity,path,0,32,1,0755);return r<0 ? r:0; }

/* Mode is set before publishing the directory entry. */
CP32_FS_TEXT int cp32_minix_mkdir_mode(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,unsigned mode)
{ int r=create_node(read,write,capacity,path,0,32,1,mode);return r<0 ? r:0; }

/* Compatibility helper for existing root-only callers. */
CP32_FS_TEXT int cp32_minix_create_root(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *name,const char *bytes,unsigned n)
{
  unsigned i;
  if(!name)return -CP32_SUPER_INVALID;
  for(i=0;name[i];i++)if(i==14 || name[i]=='/')return -CP32_FILE_NAME;
  return cp32_minix_create(read,write,capacity,name,bytes,n);
}

/* MINIX read.c:read_write/rw_chunk + zone allocation. Append new data before
 * publishing the larger inode size. At most one data zone and one indirect
 * table are needed for a <=64-byte append; no dynamic staging allocations. */
CP32_FS_TEXT int cp32_minix_append(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,const char *bytes,unsigned n)
{
  struct cp32_minix_file file;struct cp32_minix_stat info;
  struct cp32_minix_super *s=&file.super;
  unsigned char raw[64],zero[64],pointer[4];
  unsigned used,next,size,oldzone=0,newzone=0,table=0,zbit=0,tbit=0;
  unsigned zmap,itable,i,at,done,chunk,position,zone;
  int r;
  if(!read || !write || !path || !bytes || !n || n>64)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_minix_stat(read,capacity,path,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  if(info.mode&0111)return -CP32_SUPER_UNSUPPORTED;
  r=cp32_minix_file_open(read,capacity,path,&file);if(r)return r;
  if(s->log_zone_size || file.size>263*1024 || n>263*1024-file.size ||
     file.size>s->max_size || n>s->max_size-file.size)return -CP32_SUPER_UNSUPPORTED;
  size=file.size+n;used=(file.size+1023)/1024;next=(size+1023)/1024;
  zmap=(2+s->imap_blocks)*1024;
  itable=(2+s->imap_blocks+s->zmap_blocks)*1024+(file.inode-1)*64;
  if(read(itable,(char *)raw,64)!=64)return -CP32_SUPER_IO;
  if(file.size%1024) {
    r=cp32_fs_read_map(&file,used-1,&oldzone);if(r)return r;
    if(!oldzone)return -CP32_SUPER_UNSUPPORTED; /* sparse EOF filling follows */
  }
  if(next>used) {
    /* Nonzero pointers beyond EOF may be an interrupted earlier append.
     * Refuse to overwrite/reclaim them without a recovery policy. */
    if(used<7) {
      if(file.zones[used])return -CP32_SUPER_UNSUPPORTED;
    } else if(used>7) {
      table=file.indirect;
      if(!table)return -CP32_SUPER_UNSUPPORTED;
      if(read(table*1024+(used-7)*4,(char *)pointer,4)!=4)return -CP32_SUPER_IO;
      if(cp32_fs_u32(pointer,s->swapped))return -CP32_SUPER_UNSUPPORTED;
    }
    if(used==7 && file.indirect)return -CP32_SUPER_UNSUPPORTED;
    r=find_free_bit(read,zmap,1,s->zones-s->first_data_zone,s->swapped,&zbit);
    if(r)return r;
    newzone=s->first_data_zone+zbit-1;
    if(used==7) {
      r=find_free_bit(read,zmap,zbit+1,s->zones-s->first_data_zone,s->swapped,&tbit);
      if(r)return r;
      table=s->first_data_zone+tbit-1;
    }
  }
  for(i=0;i<64;i++)zero[i]=0;
  /* All geometry/space checks above finish before the first mutation. */
  if(zbit) {
    r=reserve(read,write,zmap,zbit,s->swapped);if(r)return r;
    if(tbit) {r=reserve(read,write,zmap,tbit,s->swapped);if(r)return r;}
    for(at=0;at<1024;at+=64) {
      r=store(write,newzone*1024+at,zero,64);if(r)return r;
      if(tbit) {r=store(write,table*1024+at,zero,64);if(r)return r;}
    }
  }
  done=0;
  while(done<n) {
    position=file.size+done;
    zone=position/1024<used ? oldzone:newzone;
    chunk=n-done;if(chunk>512-position%512)chunk=512-position%512;
    r=store(write,zone*1024+position%1024,
            (const unsigned char *)bytes+done,chunk);if(r)return r;
    done+=chunk;
  }
  if(zbit) {
    if(used<7)put32(raw+24+used*4,newzone,s->swapped);
    else {
      put32(pointer,newzone,s->swapped);
      r=store(write,table*1024+(used-7)*4,pointer,4);if(r)return r;
      if(tbit)put32(raw+52,table,s->swapped);
    }
  }
  put32(raw+8,size,s->swapped);
  r=store(write,itable,raw,64);return r ? r:(int)n;
}

/* Bounded single-owner snapshot of direct/single-indirect mappings. Kept out
 * of the FS call0 stack. The SD-only object is excluded from RAM recovery. */
static unsigned shrink_zones[264];
CP32_FS_TEXT static int release_zone(cp32_disk_reader read,cp32_disk_writer write,
    const struct cp32_minix_super *s,unsigned zone)
{
  unsigned bit=zone-s->first_data_zone+1;
  unsigned at=(2+s->imap_blocks)*1024+(bit/16)*2,word;
  unsigned char raw[2];
  if(read(at,(char *)raw,2)!=2)return -CP32_SUPER_IO;
  word=cp32_fs_u16(raw,s->swapped);
  if(!(word&(1U<<(bit%16))))return -CP32_SUPER_INVALID;
  put16(raw,word&~(1U<<(bit%16)),s->swapped);
  return store(write,at,raw,2);
}
/* Validate and snapshot every direct/single-indirect allocation before a
 * destructive mutation, including pointers beyond EOF. Single FS owner. */
CP32_FS_TEXT static int snapshot_zones(cp32_disk_reader read,
    const struct cp32_minix_super *s,const unsigned char *inode)
{
  unsigned i,j,zone,table,at;unsigned char raw[64];int r;
  if(cp32_fs_u32(inode+56,s->swapped) || cp32_fs_u32(inode+60,s->swapped))
    return -CP32_SUPER_UNSUPPORTED;
  for(i=0;i<264;i++)shrink_zones[i]=0;
  for(i=0;i<7;i++)shrink_zones[i]=cp32_fs_u32(inode+24+i*4,s->swapped);
  table=shrink_zones[263]=cp32_fs_u32(inode+52,s->swapped);
  if(table) {
    if(table<s->first_data_zone || table>=s->zones)return -CP32_SUPER_INVALID;
    r=cp32_fs_allocated(read,(2+s->imap_blocks)*1024,
                        table-s->first_data_zone+1,s->swapped);if(r)return r;
    for(at=0;at<1024;at+=64) {
      if(read(table*1024+at,(char *)raw,64)!=64)return -CP32_SUPER_IO;
      for(i=0;i<16;i++)shrink_zones[7+at/4+i]=cp32_fs_u32(raw+i*4,s->swapped);
    }
  }
  /* Include mappings beyond EOF (e.g. an interrupted append); reject corrupt
   * or duplicate mappings before any mutation or bitmap release. */
  for(i=0;i<264;i++) {
    zone=shrink_zones[i];if(!zone)continue;
    if(zone<s->first_data_zone || zone>=s->zones)return -CP32_SUPER_INVALID;
    for(j=0;j<i;j++)if(shrink_zones[j]==zone)return -CP32_SUPER_INVALID;
    r=cp32_fs_allocated(read,(2+s->imap_blocks)*1024,
                        zone-s->first_data_zone+1,s->swapped);if(r)return r;
  }
  return 0;
}
/* MINIX link.c:truncate + super.c:free_bit. Detach references durably before
 * releasing bitmap bits; failed reclamation leaks instead of reusing live
 * data. This is shrink-only, not ftruncate growth or a POSIX descriptor API. */
CP32_FS_TEXT int cp32_fs_truncate_inode(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,unsigned number,unsigned length)
{
  struct cp32_minix_file file;struct cp32_minix_stat info;
  struct cp32_minix_super *s=&file.super;
  unsigned char inode[64],zero[64];
  unsigned i,table,keep,itable,at,n,zone,changed;
  int r;
  if(!read || !write || !number)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_minix_super_read(read,capacity,s);if(r)return -r;
  r=cp32_fs_stat_inode(read,s,number,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  r=cp32_fs_file_inode(read,s,number,&file);if(r)return r;
  if(length>file.size)return -CP32_SUPER_INVALID;
  if(s->log_zone_size || file.size>263*1024)return -CP32_SUPER_UNSUPPORTED;
  itable=(2+s->imap_blocks+s->zmap_blocks)*1024+(file.inode-1)*64;
  if(read(itable,(char *)inode,64)!=64)return -CP32_SUPER_IO;
  r=snapshot_zones(read,s,inode);if(r)return r;
  table=shrink_zones[263];
  keep=(length+1023)/1024;changed=length!=file.size;
  for(i=keep;i<263;i++)if(shrink_zones[i])changed=1;
  if(table && keep<=7)changed=1;
  if(!changed)return 0;
  put32(inode+8,length,s->swapped);
  for(i=keep;i<7;i++)put32(inode+24+i*4,0,s->swapped);
  if(keep<=7)put32(inode+52,0,s->swapped);
  r=store(write,itable,inode,64);if(r)return r;
  for(i=0;i<64;i++)zero[i]=0;
  if(table && keep>7) {
    for(at=(keep-7)*4;at<1024;at+=n) {
      n=1024-at;if(n>64)n=64;if(n>512-at%512)n=512-at%512;
      r=store(write,table*1024+at,zero,n);if(r)return r;
    }
  }
  /* Zero the retained zone's discarded tail only after shorter EOF is durable. */
  if(length%1024 && (zone=shrink_zones[keep-1])!=0) {
    for(at=length%1024;at<1024;at+=n) {
      n=1024-at;if(n>64)n=64;if(n>512-at%512)n=512-at%512;
      r=store(write,zone*1024+at,zero,n);if(r)return r;
    }
  }
  for(i=keep;i<263;i++)if(shrink_zones[i]) {
    r=release_zone(read,write,s,shrink_zones[i]);if(r)return r;
  }
  if(table && keep<=7)return release_zone(read,write,s,table);
  return 0;
}

/* Descriptor resize extends with holes. Clear every mapped byte becoming
 * visible before publishing EOF, including mappings left by interrupted I/O. */
CP32_FS_TEXT int cp32_fs_resize_inode(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,unsigned number,unsigned length)
{
  struct cp32_minix_super s;struct cp32_minix_file f;
  unsigned char raw[64],zero[64];unsigned i,at,end,n,zone,itable;int r;
  if(!read || !write)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_minix_super_read(read,capacity,&s);if(r)return -r;
  if(length>s.max_size || length>263*1024)return -CP32_FILE_BIG;
  r=cp32_fs_file_inode(read,&s,number,&f);if(r)return r;
  if(length<=f.size)return cp32_fs_truncate_inode(read,write,capacity,number,length);
  if(s.log_zone_size)return -CP32_SUPER_UNSUPPORTED;
  itable=(2+s.imap_blocks+s.zmap_blocks)*1024+(number-1)*64;
  if(read(itable,(char *)raw,64)!=64)return -CP32_SUPER_IO;
  r=snapshot_zones(read,&s,raw);if(r)return r;
  for(i=0;i<64;i++)zero[i]=0;
  for(i=f.size/1024;i<(length+1023)/1024;i++) {
    zone=shrink_zones[i];if(!zone)continue;
    at=i==f.size/1024 ? f.size%1024:0;
    end=length-i*1024;if(end>1024)end=1024;
    while(at<end) {
      n=end-at;if(n>64)n=64;if(n>512-at%512)n=512-at%512;
      r=store(write,zone*1024+at,zero,n);if(r)return r;at+=n;
    }
  }
  put32(raw+8,length,s.swapped);return store(write,itable,raw,64);
}

CP32_FS_TEXT int cp32_minix_truncate(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,unsigned length)
{
  struct cp32_minix_stat info;int r;
  r=cp32_minix_stat(read,capacity,path,&info);if(r)return r;
  if(info.mode&0111)return -CP32_SUPER_UNSUPPORTED;
  return cp32_fs_truncate_inode(read,write,capacity,info.inode,length);
}

/* MINIX link.c:do_link. Single-volume destination parent; publish an increased
 * count before the new name. Interrupted publication can overcount links but
 * cannot expose a new name with an undercount. No rollback after uncertain I/O. */
CP32_FS_TEXT int cp32_minix_link(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,const char *destination)
{
  struct cp32_minix_dir root;struct cp32_minix_stat info;
  struct cp32_minix_super *s=&root.super;
  unsigned char entry[16],inode[64],parent[64];
  char name[15];unsigned length,i,slot,base,itable,parent_inode,found;int r;
  if(!read || !write || !path || !destination)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_minix_stat(read,capacity,path,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  /* MINIX V2 LINK_MAX is 127; retain this limit rather than wrapping u16. */
  if(!info.links || info.links>=127)return -CP32_FILE_LINK_LIMIT;
  r=mutation_location(read,capacity,destination,&root,&parent_inode,name,&slot,&found);
  if(r)return r;
  if(found)return -CP32_FILE_EXISTS;
  for(length=0;name[length];length++){}
  base=root.zones[0]*1024;
  if(slot==1024 || (slot==root.size && root.size+16>s->max_size))
    return -CP32_FILE_NOSPACE;
  itable=(2+s->imap_blocks+s->zmap_blocks)*1024;
  if(read(itable+(info.inode-1)*64,(char *)inode,64)!=64 ||
     read(itable+(parent_inode-1)*64,(char *)parent,64)!=64)return -CP32_SUPER_IO;
  put16(inode+2,info.links+1,s->swapped);
  r=store(write,itable+(info.inode-1)*64,inode,64);if(r)return r;
  for(i=0;i<16;i++)entry[i]=0;
  put16(entry,info.inode,s->swapped);
  for(i=0;i<length;i++)entry[2+i]=(unsigned char)name[i];
  r=store(write,base+slot,entry,16);if(r)return r;
  if(slot==root.size) {
    put32(parent+8,root.size+16,s->swapped);
    r=store(write,itable+(parent_inode-1)*64,parent,64);if(r)return r;
  }
  return 0;
}

/* Retain the root-name API for existing callers. */
CP32_FS_TEXT int cp32_minix_link_root(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,const char *name)
{
  char destination[16];unsigned n;
  if(!name)return -CP32_SUPER_INVALID;
  destination[0]='/';
  for(n=0;name[n];n++) {
    if(n==14 || name[n]=='/')return -CP32_FILE_NAME;
    destination[n+1]=name[n];
  }
  destination[n+1]=0;
  return cp32_minix_link(read,write,capacity,path,destination);
}


/* MINIX link.c:unlink_file and inode.c:put_inode/free_inode. Remove the name
 * before reducing references. Detach last-link inode before reclaiming storage;
 * uncertain writes may leak but must never recycle a still-referenced object.
 * Regular open inodes remain allocated until the final descriptor closes. */
CP32_FS_TEXT static int remove_node(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,const char *cwd)
{
  struct cp32_minix_dir dir;struct cp32_minix_stat info,working;
  struct cp32_minix_super *s=&dir.super;
  char name[15];unsigned char inode[64],entry[16],bits[2],parent_links[2];
  unsigned i,at,slot,itable,word,parent_inode,found,parent_at=0;int r,fd,held=0;
  if(!read || !write || !path)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=mutation_location(read,capacity,path,&dir,&parent_inode,name,&slot,&found);if(r)return r;
  if(!found)return -CP32_FILE_NOT_FOUND;
  r=cp32_minix_stat(read,capacity,path,&info);if(r)return r;
  if(cwd) {
    if((info.mode&0170000)!=0040000)return -CP32_FILE_NOT_DIR;
    r=cp32_minix_stat(read,capacity,cwd,&working);if(r)return r;
    if(info.inode==1 || info.inode==working.inode)return -CP32_FILE_BUSY;
    if(info.links!=2)return -CP32_FILE_NOT_EMPTY;
    if(info.size<32 || info.size%16)return -CP32_SUPER_INVALID;
    if(info.size>1024)return -CP32_SUPER_UNSUPPORTED;
  } else if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  if(!info.links)return -CP32_SUPER_INVALID;
  for(fd=0;fd<CP32_OPEN_MAX;fd++) {
    const struct cp32_minix_file *open=cp32_fd_file(fd);
    if(cwd && open && open->read==read && open->inode==info.inode)return -CP32_FILE_BUSY;
  }
  itable=(2+s->imap_blocks+s->zmap_blocks)*1024+(info.inode-1)*64;
  if(read(itable,(char *)inode,64)!=64)return -CP32_SUPER_IO;
  if(cwd || info.links==1) {
    if(info.size>263*1024)return -CP32_SUPER_UNSUPPORTED;
    r=snapshot_zones(read,s,inode);if(r)return r;
  }
  if(cwd) {
    /* MINIX remove_dir: only dot entries may remain. Verify their identities
     * before unlinking, including empty slots left by removed regular files. */
    if(!shrink_zones[0])return -CP32_SUPER_INVALID;
    for(at=0;at<info.size;at+=16) {
      if(read(shrink_zones[0]*1024+at,(char *)entry,16)!=16)return -CP32_SUPER_IO;
      word=cp32_fs_u16(entry,s->swapped);
      if(at>=32) {if(word)return -CP32_FILE_NOT_EMPTY;continue;}
      if(word!=(at ? parent_inode:info.inode) || entry[2]!='.' ||
         entry[3]!=(at ? '.':0))return -CP32_SUPER_INVALID;
      for(i=at ? 4:3;i<16;i++)if(entry[i])return -CP32_SUPER_INVALID;
    }
    parent_at=(2+s->imap_blocks+s->zmap_blocks)*1024+(parent_inode-1)*64+2;
    if(read(parent_at,(char *)parent_links,2)!=2)return -CP32_SUPER_IO;
    word=cp32_fs_u16(parent_links,s->swapped);
    if(word<3)return -CP32_SUPER_INVALID;
    put16(parent_links,word-1,s->swapped);
  }
  if(!cwd && info.links==1)held=cp32_fd_defer_unlink(read,write,info.inode);
  for(i=0;i<16;i++)entry[i]=0;
  r=store(write,dir.zones[0]*1024+slot,entry,16);if(r)return r;
  if(!cwd && (info.links>1 || held)) {
    put16(inode+2,info.links-1,s->swapped);
    return store(write,itable,inode,64);
  }
  for(i=0;i<64;i++)inode[i]=0;
  r=store(write,itable,inode,64);if(r)return r;
  /* Child dot-dot is detached before decrementing its parent's link count. */
  if(cwd) {r=store(write,parent_at,parent_links,2);if(r)return r;}
  for(i=0;i<264;i++)if(shrink_zones[i]) {
    r=release_zone(read,write,s,shrink_zones[i]);if(r)return r;
  }
  at=2048+(info.inode/16)*2;
  if(read(at,(char *)bits,2)!=2)return -CP32_SUPER_IO;
  word=cp32_fs_u16(bits,s->swapped);
  if(!(word&(1U<<(info.inode%16))))return -CP32_SUPER_INVALID;
  put16(bits,word&~(1U<<(info.inode%16)),s->swapped);
  return store(write,at,bits,2);
}

CP32_FS_TEXT int cp32_minix_unlink(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path)
{ return remove_node(read,write,capacity,path,0); }

/* The sole shell cwd is protected by inode, not string spelling. */
CP32_FS_TEXT int cp32_minix_rmdir(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,const char *cwd)
{ return cwd ? remove_node(read,write,capacity,path,cwd):-CP32_SUPER_INVALID; }

/* MINIX link.c:do_rename, no-replacement regular-file subset. Same-directory
 * rename rewrites one entry. Across directories publish the destination first,
 * with a temporary counted link, then remove the source and restore its count.
 * On uncertain I/O stop; both names/overcounts may remain, never retry/rollback. */
CP32_FS_TEXT int cp32_minix_rename(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *oldpath,const char *newpath)
{
  struct cp32_minix_dir olddir,newdir;struct cp32_minix_stat info;
  char oldname[15],newname[15];unsigned char entry[16],inode[64],parent[64];
  unsigned oldparent,newparent,oldslot,newslot,oldino,newino,i,itable,at;int r;
  if(!read || !write)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=mutation_location(read,capacity,oldpath,&olddir,&oldparent,oldname,&oldslot,&oldino);
  if(r)return r;
  if(!oldino)return -CP32_FILE_NOT_FOUND;
  r=cp32_minix_stat(read,capacity,oldpath,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  r=mutation_location(read,capacity,newpath,&newdir,&newparent,newname,&newslot,&newino);
  if(r)return r;
  if(newino==oldino)return 0; /* Same path or another hard link: MINIX no-op. */
  if(newino)return -CP32_FILE_EXISTS;
  for(i=0;i<16;i++)entry[i]=0;
  put16(entry,oldino,olddir.super.swapped);
  for(i=0;newname[i];i++)entry[2+i]=(unsigned char)newname[i];
  if(oldparent==newparent)
    return store(write,olddir.zones[0]*1024+oldslot,entry,16);
  if(newslot==1024 || (newslot==newdir.size && newdir.size+16>newdir.super.max_size))
    return -CP32_FILE_NOSPACE;
  if(!info.links || info.links>=127)return -CP32_FILE_LINK_LIMIT;
  itable=(2+olddir.super.imap_blocks+olddir.super.zmap_blocks)*1024;
  at=itable+(oldino-1)*64;
  if(read(at,(char *)inode,64)!=64 ||
     read(itable+(newparent-1)*64,(char *)parent,64)!=64)return -CP32_SUPER_IO;
  put16(inode+2,info.links+1,olddir.super.swapped);
  r=store(write,at,inode,64);if(r)return r;
  r=store(write,newdir.zones[0]*1024+newslot,entry,16);if(r)return r;
  if(newslot==newdir.size) {
    put32(parent+8,newdir.size+16,newdir.super.swapped);
    r=store(write,itable+(newparent-1)*64,parent,64);if(r)return r;
  }
  for(i=0;i<16;i++)entry[i]=0;
  r=store(write,olddir.zones[0]*1024+oldslot,entry,16);if(r)return r;
  put16(inode+2,info.links,olddir.super.swapped);
  return store(write,at,inode,64);
}

/* MINIX protect.c:do_chmod. Trusted shell, numeric rwx modes only; there is
 * no effective-UID/group permission model or ctime clock yet. Preserve inode
 * identity/type and all non-mode bytes, clear unsupported special-mode bits. */
CP32_FS_TEXT int cp32_fs_chmod_inode(cp32_disk_reader read,cp32_disk_writer write,
    const struct cp32_minix_super *super,unsigned number,unsigned mode)
{
  struct cp32_minix_stat info;unsigned at,next;unsigned char raw[2];int r;
  if(!read || !write || !super || mode>0777)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_fs_stat_inode(read,super,number,&info);if(r)return r;
  next=(info.mode&~07777U)|mode;if(next==info.mode)return 0;
  at=(2+super->imap_blocks+super->zmap_blocks)*1024+(number-1)*64;
  put16(raw,next,super->swapped);return store(write,at,raw,2);
}
CP32_FS_TEXT int cp32_minix_chmod(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,unsigned mode)
{
  struct cp32_minix_super super;unsigned number;int r;
  if(!read || !write || !path || mode>0777)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_fs_resolve_path(read,capacity,path,&super,&number);if(r)return r;
  return cp32_fs_chmod_inode(read,write,&super,number,mode);
}

/* MINIX commands/simple/cp.c streams read/write chunks. Until writable app
 * descriptors exist, the trusted shell uses bounded create/append helpers.
 * Keep partial destinations on errors: uncertain I/O must not trigger rollback. */
CP32_FS_TEXT int cp32_minix_copy(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *source,const char *destination)
{
  struct cp32_minix_file file;struct cp32_minix_stat info;
  char bytes[64];int r,n;
  if(!read || !write || !source || !destination)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_minix_stat(read,capacity,source,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  if(info.size>263U*1024)return -CP32_SUPER_UNSUPPORTED;
  r=cp32_minix_file_open(read,capacity,source,&file);if(r)return r;
  n=cp32_minix_file_read(&file,bytes,sizeof(bytes));if(n<0)return n;
  r=create_node(read,write,capacity,destination,bytes,(unsigned)n,0,0644);
  if(r<0)return r;
  while(file.position<file.size) {
    n=cp32_minix_file_read(&file,bytes,sizeof(bytes));
    if(n<=0)return n<0 ? n:-CP32_SUPER_IO;
    r=cp32_minix_append(read,write,capacity,destination,bytes,(unsigned)n);
    if(r<0)return r;
    if(r!=n)return -CP32_SUPER_IO;
  }
  /* Publish executable bits only after all data has been copied. */
  return cp32_minix_chmod(read,write,capacity,destination,info.mode&0777);
}

/* Regular descriptor writes. Work in verified sector fragments, publishing
 * initialized mappings before EOF. Never report an uncertain fragment as done. */
CP32_FS_TEXT int cp32_fs_write_inode(cp32_disk_reader read,cp32_disk_writer write,
    const struct cp32_minix_super *super,unsigned number,unsigned offset,
    const char *bytes,unsigned count)
{
  struct cp32_minix_file f;
  unsigned char raw[64],zero[64],ptr[4];
  unsigned done=0,at,zone,table,bit,tbit,index,chunk,position,itable,zmap,i;
  int r;
  if(!write || (!bytes && count) || count>64)return -CP32_SUPER_INVALID;
  if(super->log_zone_size)return -CP32_SUPER_UNSUPPORTED;
  if(offset>263U*1024 || count>263U*1024-offset ||
     offset>super->max_size || count>super->max_size-offset)return -CP32_FILE_BIG;
  if(!count)return 0;
  itable=(2+super->imap_blocks+super->zmap_blocks)*1024+(number-1)*64;
  zmap=(2+super->imap_blocks)*1024;
  for(i=0;i<64;i++)zero[i]=0;
  while(done<count) {
    cp32_cache_invalidate();
    r=cp32_fs_file_inode(read,super,number,&f);if(r)goto failed;
    if(f.size>263U*1024){r=-CP32_FILE_BIG;goto failed;}
    if(read(itable,(char *)raw,64)!=64){r=-CP32_SUPER_IO;goto failed;}
    r=snapshot_zones(read,super,raw);if(r)goto failed;
    position=offset+done;index=position/1024;
    zone=shrink_zones[index];table=shrink_zones[263];
    /* Clear any retained tail exposed by a seek beyond EOF. Unallocated
     * intervening zones remain holes and read as zero. */
    if(position>f.size && f.size%1024 && shrink_zones[f.size/1024]) {
      unsigned end=(f.size/1024==index) ? position%1024:1024;
      for(at=f.size%1024;at<end;at+=chunk) {
        chunk=end-at;if(chunk>64)chunk=64;
        if(chunk>512-at%512)chunk=512-at%512;
        r=store(write,shrink_zones[f.size/1024]*1024+at,zero,chunk);if(r)goto failed;
      }
    }
    if(!zone) {
      r=find_free_bit(read,zmap,1,super->zones-super->first_data_zone,super->swapped,&bit);if(r)goto failed;
      zone=super->first_data_zone+bit-1;
      tbit=0;
      if(index>=7 && !table) {
        r=find_free_bit(read,zmap,bit+1,super->zones-super->first_data_zone,super->swapped,&tbit);if(r)goto failed;
      }
      r=reserve(read,write,zmap,bit,super->swapped);if(r)goto failed;
      for(at=0;at<1024;at+=64){r=store(write,zone*1024+at,zero,64);if(r)goto failed;}
      if(index>=7 && !table) {
        table=super->first_data_zone+tbit-1;
        r=reserve(read,write,zmap,tbit,super->swapped);if(r)goto failed;
        for(at=0;at<1024;at+=64){r=store(write,table*1024+at,zero,64);if(r)goto failed;}
      }
    }
    chunk=count-done;if(chunk>512-position%512)chunk=512-position%512;
    r=store(write,zone*1024+position%1024,(const unsigned char *)bytes+done,chunk);if(r)goto failed;
    if(index<7)put32(raw+24+4*index,zone,super->swapped);
    else {
      put32(ptr,zone,super->swapped);
      r=store(write,table*1024+4*(index-7),ptr,4);if(r)goto failed;
      put32(raw+52,table,super->swapped);
    }
    if(position+chunk>f.size)put32(raw+8,position+chunk,super->swapped);
    r=store(write,itable,raw,64);if(r)goto failed;
    done+=chunk;
  }
  return done;
failed:
  return done ? (int)done:r;
}

/* MINIX put_inode: only a detached zero-link inode is reclaimable. The final
 * descriptor remains registered during validation, then close consumes it. */
CP32_FS_TEXT int cp32_fs_reap_inode(cp32_disk_reader read,cp32_disk_writer write,
    const struct cp32_minix_super *s,unsigned number)
{
  unsigned char raw[64],bits[2];unsigned at,i,word;int r;
  at=(2+s->imap_blocks+s->zmap_blocks)*1024+(number-1)*64;
  if(read(at,(char *)raw,64)!=64)return -CP32_SUPER_IO;
  if(cp32_fs_u16(raw+2,s->swapped))return 0; /* failed unlink left a link */
  if((cp32_fs_u16(raw,s->swapped)&0170000)!=0100000)return -CP32_SUPER_INVALID;
  r=snapshot_zones(read,s,raw);if(r)return r;
  for(i=0;i<64;i++)raw[i]=0;
  r=store(write,at,raw,64);if(r)return r;
  for(i=0;i<264;i++)if(shrink_zones[i]) {
    r=release_zone(read,write,s,shrink_zones[i]);if(r)return r;
  }
  at=2048+(number/16)*2;
  if(read(at,(char *)bits,2)!=2)return -CP32_SUPER_IO;
  word=cp32_fs_u16(bits,s->swapped);
  if(!(word&(1U<<(number%16))))return -CP32_SUPER_INVALID;
  put16(bits,word&~(1U<<(number%16)),s->swapped);
  return store(write,at,bits,2);
}

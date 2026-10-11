/* MINIX super.c:alloc_bit, inode.c:alloc_inode, open.c:new_node.
 * Single FS owner; synchronous writes, inode/data durable before directory
 * publication. An interrupted operation may leak reservations; never reclaim
 * them after uncertain I/O. No rollback or power-loss atomicity is promised. */
#include "fs.h"
CP32_IRAM_EXT static void put16(unsigned char *p,unsigned n,unsigned swap)
{ p[swap ? 1:0]=(unsigned char)n;p[swap ? 0:1]=(unsigned char)(n>>8); }
CP32_IRAM_EXT static void put32(unsigned char *p,unsigned n,unsigned swap)
{
  put16(p,swap ? n>>16:n,swap);
  put16(p+2,swap ? n:n>>16,swap);
}
CP32_IRAM_EXT static int store(cp32_disk_writer write,unsigned at,
                               const unsigned char *p,unsigned n)
{
  int r;
  cp32_cache_invalidate();r=write(at,(const char *)p,n);cp32_cache_invalidate();
  return r==(int)n ? 0:-CP32_SUPER_IO;
}
/* Skip reserved bit zero, scan only real inode/zone bits, not map padding. */
CP32_IRAM_EXT static int find_free_bit(cp32_disk_reader read,unsigned map,
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
CP32_IRAM_EXT static int reserve(cp32_disk_reader read,cp32_disk_writer write,
                                 unsigned map,unsigned bit,unsigned swap)
{
  unsigned char raw[2];unsigned at=map+(bit/16)*2,word;
  if(read(at,(char *)raw,2)!=2)return -CP32_SUPER_IO;
  word=cp32_fs_u16(raw,swap);
  if(word&(1U<<(bit%16)))return -CP32_SUPER_INVALID;
  put16(raw,word|(1U<<(bit%16)),swap);return store(write,at,raw,2);
}
CP32_IRAM_EXT int cp32_minix_create_root(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *name,const char *bytes,unsigned n)
{
  struct cp32_minix_dir root;
  struct cp32_minix_super *s=&root.super;
  unsigned char raw[64],entry[16],parent[64];
  unsigned length,i,at,slot,ino,zbit,zone,imap=2048,zmap,itable,base;
  int r;
  if(!read || !write || !name || !bytes || !n || n>64)return -CP32_SUPER_INVALID;
  for(length=0;name[length];length++)
    if(length==14 || name[length]=='/')return -CP32_FILE_NAME;
  if(!length || (length==1 && name[0]=='.') ||
     (length==2 && name[0]=='.' && name[1]=='.'))return -CP32_FILE_NAME;
  cp32_cache_invalidate();
  r=cp32_minix_root_open(read,capacity,&root);if(r)return r;
  /* First allocator milestone: one-block zones, root's existing first block.
   * Larger directories/zones are rejected before any writes. */
  if(s->log_zone_size || root.size>1024 || n>s->max_size)
    return -CP32_SUPER_UNSUPPORTED;
  base=root.zones[0]*1024;slot=root.size;
  for(at=0;at<root.size;at+=16) {
    if(read(base+at,(char *)entry,16)!=16)return -CP32_SUPER_IO;
    if(!cp32_fs_u16(entry,s->swapped)) {
      if(at>=32 && slot==root.size)slot=at;
    } else {
      for(i=0;i<length && entry[2+i]==(unsigned char)name[i];i++){}
      if(i==length && (length==14 || !entry[2+length]))return -CP32_FILE_EXISTS;
    }
  }
  if(slot==1024 || (slot==root.size && root.size+16>s->max_size))
    return -CP32_FILE_NOSPACE;
  zmap=(2+s->imap_blocks)*1024;
  itable=(2+s->imap_blocks+s->zmap_blocks)*1024;
  if(read(itable,(char *)parent,64)!=64)return -CP32_SUPER_IO;
  r=find_free_bit(read,imap,1,s->ninodes,s->swapped,&ino);if(r)return r;
  r=find_free_bit(read,zmap,1,s->zones-s->first_data_zone,s->swapped,&zbit);if(r)return r;
  zone=s->first_data_zone+zbit-1;
  /* Complete capacity/name/mapping checks precede all mutations. Clear stale
   * inode fields while unallocated, then reserve inode and zone separately. */
  for(i=0;i<64;i++)raw[i]=0;
  r=store(write,itable+(ino-1)*64,raw,64);if(r)return r;
  r=reserve(read,write,imap,ino,s->swapped);if(r)return r;
  r=reserve(read,write,zmap,zbit,s->swapped);if(r)return r;
  for(at=0;at<1024;at+=64) {
    r=store(write,zone*1024+at,raw,64);if(r)return r;
  }
  r=store(write,zone*1024,(const unsigned char *)bytes,n);if(r)return r;
  put16(raw,0100644,s->swapped);put16(raw+2,1,s->swapped);
  put32(raw+8,n,s->swapped);put32(raw+24,zone,s->swapped);
  /* uid/gid and unavailable wall-clock timestamps remain zero. */
  r=store(write,itable+(ino-1)*64,raw,64);if(r)return r;
  for(i=0;i<16;i++)entry[i]=0;
  put16(entry,ino,s->swapped);
  for(i=0;i<length;i++)entry[2+i]=(unsigned char)name[i];
  r=store(write,base+slot,entry,16);if(r)return r;
  if(slot==root.size) {
    put32(parent+8,root.size+16,s->swapped);
    r=store(write,itable,parent,64);if(r)return r;
  }
  return (int)n;
}

/* MINIX read.c:read_write/rw_chunk + zone allocation. Append new data before
 * publishing the larger inode size. At most one data zone and one indirect
 * table are needed for a <=64-byte append; no dynamic staging allocations. */
CP32_IRAM_EXT int cp32_minix_append(cp32_disk_reader read,cp32_disk_writer write,
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
CP32_IRAM_EXT static int release_zone(cp32_disk_reader read,cp32_disk_writer write,
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
CP32_IRAM_EXT static int snapshot_zones(cp32_disk_reader read,
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
CP32_IRAM_EXT int cp32_minix_truncate(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,unsigned length)
{
  struct cp32_minix_file file;struct cp32_minix_stat info;
  struct cp32_minix_super *s=&file.super;
  unsigned char inode[64],zero[64];
  unsigned i,table,keep,itable,at,n,zone,changed;
  int r;
  if(!read || !write || !path)return -CP32_SUPER_INVALID;
  cp32_cache_invalidate();
  r=cp32_minix_stat(read,capacity,path,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  if(info.mode&0111)return -CP32_SUPER_UNSUPPORTED;
  r=cp32_minix_file_open(read,capacity,path,&file);if(r)return r;
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

/* MINIX link.c:do_link. Single-volume root destination; publish an increased
 * count before the new name. Interrupted publication can overcount links but
 * cannot expose a new name with an undercount. No rollback after uncertain I/O. */
CP32_IRAM_EXT int cp32_minix_link_root(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path,const char *name)
{
  struct cp32_minix_dir root;struct cp32_minix_stat info;
  struct cp32_minix_super *s=&root.super;
  unsigned char entry[16],inode[64],parent[64];
  unsigned length,i,at,slot,base,itable;int r;
  if(!read || !write || !path || !name)return -CP32_SUPER_INVALID;
  for(length=0;name[length];length++)
    if(length==14 || name[length]=='/')return -CP32_FILE_NAME;
  if(!length || (length==1 && name[0]=='.') ||
     (length==2 && name[0]=='.' && name[1]=='.'))return -CP32_FILE_NAME;
  cp32_cache_invalidate();
  r=cp32_minix_stat(read,capacity,path,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  /* MINIX V2 LINK_MAX is 127; retain this limit rather than wrapping u16. */
  if(!info.links || info.links>=127)return -CP32_SUPER_UNSUPPORTED;
  r=cp32_minix_root_open(read,capacity,&root);if(r)return r;
  if(s->log_zone_size || root.size>1024)return -CP32_SUPER_UNSUPPORTED;
  base=root.zones[0]*1024;slot=root.size;
  for(at=0;at<root.size;at+=16) {
    if(read(base+at,(char *)entry,16)!=16)return -CP32_SUPER_IO;
    if(!cp32_fs_u16(entry,s->swapped)) {
      if(at>=32 && slot==root.size)slot=at;
    } else {
      for(i=0;i<length && entry[2+i]==(unsigned char)name[i];i++){}
      if(i==length && (length==14 || !entry[2+length]))return -CP32_FILE_EXISTS;
    }
  }
  if(slot==1024 || (slot==root.size && root.size+16>s->max_size))
    return -CP32_FILE_NOSPACE;
  itable=(2+s->imap_blocks+s->zmap_blocks)*1024;
  if(read(itable+(info.inode-1)*64,(char *)inode,64)!=64 ||
     read(itable,(char *)parent,64)!=64)return -CP32_SUPER_IO;
  put16(inode+2,info.links+1,s->swapped);
  r=store(write,itable+(info.inode-1)*64,inode,64);if(r)return r;
  for(i=0;i<16;i++)entry[i]=0;
  put16(entry,info.inode,s->swapped);
  for(i=0;i<length;i++)entry[2+i]=(unsigned char)name[i];
  r=store(write,base+slot,entry,16);if(r)return r;
  if(slot==root.size) {
    put32(parent+8,root.size+16,s->swapped);
    r=store(write,itable,parent,64);if(r)return r;
  }
  return 0;
}


/* MINIX link.c:unlink_file and inode.c:put_inode/free_inode. Remove the name
 * before reducing references. Detach last-link inode before reclaiming storage;
 * uncertain writes may leak but must never recycle a still-referenced object.
 * Open files are rejected until deferred last-close reclamation is ported. */
CP32_IRAM_EXT int cp32_minix_unlink(cp32_disk_reader read,cp32_disk_writer write,
    unsigned capacity,const char *path)
{
  struct cp32_minix_dir dir;struct cp32_minix_stat info;
  struct cp32_minix_super *s=&dir.super;
  char parent[CP32_MINIX_PATH_MAX+1];
  unsigned char inode[64],entry[16],bits[2];
  unsigned length,start=0,n,i,at,slot,itable,word;int r,fd;
  if(!read || !write || !path)return -CP32_SUPER_INVALID;
  for(length=0;path[length];length++) {
    if(length==CP32_MINIX_PATH_MAX)return -CP32_FILE_NAME;
    if(path[length]=='/')start=length+1;
  }
  n=length-start;
  if(!n || n>14 || (n==1 && path[start]=='.') ||
     (n==2 && path[start]=='.' && path[start+1]=='.'))return -CP32_FILE_NAME;
  if(start) {for(i=0;i<start;i++)parent[i]=path[i];parent[start]=0;}
  else {parent[0]='/';parent[1]=0;}
  cp32_cache_invalidate();
  r=cp32_minix_stat(read,capacity,path,&info);if(r)return r;
  if((info.mode&0170000)!=0100000)return -CP32_FILE_IS_DIR;
  if(!info.links)return -CP32_SUPER_INVALID;
  for(fd=0;fd<CP32_OPEN_MAX;fd++) {
    const struct cp32_minix_file *open=cp32_fd_file(fd);
    if(open && open->inode==info.inode)return -CP32_FILE_BUSY;
  }
  r=cp32_minix_dir_open(read,capacity,parent,&dir);if(r)return r;
  /* Existing root and /boot directories fit this bounded mutation path. */
  if(s->log_zone_size || dir.size>1024)return -CP32_SUPER_UNSUPPORTED;
  slot=dir.size;
  for(at=32;at<dir.size;at+=16) {
    if(read(dir.zones[0]*1024+at,(char *)entry,16)!=16)return -CP32_SUPER_IO;
    if(cp32_fs_u16(entry,s->swapped)!=info.inode)continue;
    for(i=0;i<n && entry[2+i]==(unsigned char)path[start+i];i++){}
    if(i==n && (n==14 || !entry[2+n])) {slot=at;break;}
  }
  if(slot==dir.size)return -CP32_FILE_NOT_FOUND;
  itable=(2+s->imap_blocks+s->zmap_blocks)*1024+(info.inode-1)*64;
  if(read(itable,(char *)inode,64)!=64)return -CP32_SUPER_IO;
  if(info.links==1) {
    if(info.size>263*1024)return -CP32_SUPER_UNSUPPORTED;
    r=snapshot_zones(read,s,inode);if(r)return r;
  }
  for(i=0;i<16;i++)entry[i]=0;
  r=store(write,dir.zones[0]*1024+slot,entry,16);if(r)return r;
  if(info.links>1) {
    put16(inode+2,info.links-1,s->swapped);
    return store(write,itable,inode,64);
  }
  for(i=0;i<64;i++)inode[i]=0;
  r=store(write,itable,inode,64);if(r)return r;
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

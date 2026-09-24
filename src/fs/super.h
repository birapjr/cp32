#ifndef CP32_FS_SUPER_H
#define CP32_FS_SUPER_H
/* Validated on-disk geometry; no mounted-superblock table yet. */
struct cp32_minix_super {
  unsigned ninodes, zones, first_data_zone, log_zone_size, max_size;
  unsigned imap_blocks, zmap_blocks, swapped;
};
#endif

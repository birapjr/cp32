#ifndef CP32_FS_CONST_H
#define CP32_FS_CONST_H
/* CP32 helper status codes, not the final MINIX syscall errno ABI. */
#define CP32_SUPER_OK 0
#define CP32_SUPER_ABSENT 1
#define CP32_SUPER_UNSUPPORTED 2
#define CP32_SUPER_INVALID 3
#define CP32_SUPER_IO 4
#define CP32_FILE_NOT_FOUND 5
#define CP32_FILE_IS_DIR 6
#define CP32_FILE_NAME 7
#define CP32_FILE_NOT_DIR 8
#define CP32_OPEN_MAX 8
#define CP32_FILE_BAD_FD 9
#define CP32_FILE_LIMIT 10
#define CP32_MINIX_PATH_MAX 255
#endif

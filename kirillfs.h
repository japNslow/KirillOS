#ifndef KIRILLFS_H
#define KIRILLFS_H

#define KFS_MAX_FILES 16
#define KFS_NAME_MAX  24
#define KFS_DATA_MAX  49152  /* 48 KB per file, supports full BMP images up to 256x160 */

typedef enum {
    KFS_OK = 0,
    KFS_NOT_FOUND,
    KFS_EXISTS,
    KFS_FULL,
    KFS_INVALID_NAME,
    KFS_TOO_LARGE
} kfs_result_t;

void kfs_init(void);
void kfs_format(void);
int kfs_is_persistent(void);
int kfs_file_count(void);
const char* kfs_name(int index);
int kfs_size(int index);
kfs_result_t kfs_touch(const char* name);
kfs_result_t kfs_write(const char* name, const char* data);
kfs_result_t kfs_write_binary(const char* name, const void* data, int size);
kfs_result_t kfs_read(const char* name, const char** data, int* size);
kfs_result_t kfs_remove(const char* name);

#endif

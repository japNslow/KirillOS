#include "kirillfs.h"
#include "ata.h"
#include <stdint.h>

#define KFS_MAGIC            0x4B465334  /* KFS4 */
#define KFS_FIRST_SECTOR     321         /* Superblock */
#define KFS_DIR_SECTOR       322         /* Directory Table (512 bytes = 1 sector) */
#define KFS_DATA_START       323         /* First data sector */
#define KFS_SECTORS_PER_FILE ((KFS_DATA_MAX + 511) / 512) /* 96 sectors for 48KB */

typedef struct {
    int used;
    int size;
    char name[KFS_NAME_MAX];
    char data[KFS_DATA_MAX];
} kfs_file_t;

static kfs_file_t files[KFS_MAX_FILES];
static int persistent;
static uint8_t sector[512];

static void put32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)value; p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16); p[3] = (uint8_t)(value >> 24);
}

static uint32_t get32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static int string_equal(const char* a, const char* b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int string_length(const char* value) {
    int length = 0;
    while (value && value[length]) length++;
    return length;
}

static int valid_name(const char* name) {
    int length;
    if (!name || !name[0]) return 0;
    length = string_length(name);
    if (length >= KFS_NAME_MAX) return 0;
    for (int i = 0; i < length; i++) {
        char c = name[i];
        if (c < 32 || c > 126 || c == ' ' || c == '/' || c == '\\') return 0;
    }
    return 1;
}

static void save_dir(void) {
    if (!persistent) return;
    for (int i = 0; i < 512; i++) sector[i] = 0;
    for (int f = 0; f < KFS_MAX_FILES; f++) {
        int off = f * 32;
        put32(sector + off, (uint32_t)(files[f].used == 1 ? 1 : 0));
        put32(sector + off + 4, (uint32_t)files[f].size);
        for (int i = 0; i < KFS_NAME_MAX; i++) {
            sector[off + 8 + i] = (uint8_t)files[f].name[i];
            if (!files[f].name[i]) break;
        }
    }
    (void)ata_write28(KFS_DIR_SECTOR, sector);
}

static void save_file_data(int slot) {
    if (!persistent || slot < 0 || slot >= KFS_MAX_FILES) return;
    int num_sec = (files[slot].size + 511) / 512;
    if (num_sec <= 0) return;
    if (num_sec > KFS_SECTORS_PER_FILE) num_sec = KFS_SECTORS_PER_FILE;

    uint32_t start_lba = KFS_DATA_START + slot * KFS_SECTORS_PER_FILE;
    for (int s = 0; s < num_sec; s++) {
        for (int i = 0; i < 512; i++) sector[i] = 0;
        int rem = files[slot].size - s * 512;
        int chunk = rem > 512 ? 512 : rem;
        for (int i = 0; i < chunk; i++) {
            sector[i] = (uint8_t)files[slot].data[s * 512 + i];
        }
        (void)ata_write28(start_lba + s, sector);
    }
}

static int load(void) {
    if (!persistent) return 0;

    /* 1. Superblock check */
    if (!ata_read28(KFS_FIRST_SECTOR, sector)) return 0;
    if (get32(sector) != KFS_MAGIC) return 0;

    /* 2. Directory table */
    if (!ata_read28(KFS_DIR_SECTOR, sector)) return 0;
    for (int f = 0; f < KFS_MAX_FILES; f++) {
        int off = f * 32;
        files[f].used = (int)get32(sector + off);
        files[f].size = (int)get32(sector + off + 4);
        for (int i = 0; i < KFS_NAME_MAX; i++) {
            files[f].name[i] = (char)sector[off + 8 + i];
        }
        files[f].name[KFS_NAME_MAX - 1] = 0;
        files[f].data[0] = 0;

        /* Validate entry integrity */
        if (files[f].used != 1 || !valid_name(files[f].name) ||
            files[f].size < 0 || files[f].size > KFS_DATA_MAX) {
            files[f].used = 0;
            files[f].size = 0;
            files[f].name[0] = 0;
            files[f].data[0] = 0;
            continue;
        }

        /* Load file data if used */
        if (files[f].size > 0) {
            int num_sec = (files[f].size + 511) / 512;
            if (num_sec > KFS_SECTORS_PER_FILE) num_sec = KFS_SECTORS_PER_FILE;
            uint32_t start_lba = KFS_DATA_START + f * KFS_SECTORS_PER_FILE;
            for (int s = 0; s < num_sec; s++) {
                if (ata_read28(start_lba + s, sector)) {
                    int rem = files[f].size - s * 512;
                    int chunk = rem > 512 ? 512 : rem;
                    for (int i = 0; i < chunk; i++) {
                        files[f].data[s * 512 + i] = (char)sector[i];
                    }
                }
            }
            if (files[f].size < KFS_DATA_MAX) {
                files[f].data[files[f].size] = 0;
            }
        }
    }
    return 1;
}

static int find_file(const char* name) {
    for (int i = 0; i < KFS_MAX_FILES; i++) {
        if (files[i].used && string_equal(files[i].name, name)) return i;
    }
    return -1;
}

void kfs_format(void) {
    for (int i = 0; i < KFS_MAX_FILES; i++) {
        files[i].used = 0;
        files[i].size = 0;
        files[i].name[0] = 0;
        files[i].data[0] = 0;
    }
    if (!persistent) return;

    /* Write Superblock */
    for (int i = 0; i < 512; i++) sector[i] = 0;
    put32(sector, KFS_MAGIC);
    put32(sector + 4, KFS_MAX_FILES);
    put32(sector + 8, KFS_DATA_MAX);
    (void)ata_write28(KFS_FIRST_SECTOR, sector);

    /* Write empty Directory Table */
    save_dir();
}

void kfs_init(void) {
    persistent = ata_init();
    if (!load()) kfs_format();
}

int kfs_is_persistent(void) { return persistent; }

int kfs_file_count(void) {
    int count = 0;
    for (int i = 0; i < KFS_MAX_FILES; i++)
        if (files[i].used) count++;
    return count;
}

const char* kfs_name(int index) {
    int current = 0;
    for (int i = 0; i < KFS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        if (current++ == index) return files[i].name;
    }
    return 0;
}

int kfs_size(int index) {
    int current = 0;
    for (int i = 0; i < KFS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        if (current++ == index) return files[i].size;
    }
    return 0;
}

kfs_result_t kfs_touch(const char* name) {
    int slot = -1;
    if (!valid_name(name)) return KFS_INVALID_NAME;
    if (find_file(name) >= 0) return KFS_EXISTS;
    for (int i = 0; i < KFS_MAX_FILES; i++) {
        if (!files[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return KFS_FULL;
    files[slot].used = 1;
    files[slot].size = 0;
    for (int i = 0; i < KFS_NAME_MAX; i++) {
        files[slot].name[i] = name[i];
        if (!name[i]) break;
    }
    files[slot].name[KFS_NAME_MAX - 1] = 0;
    files[slot].data[0] = 0;
    save_dir();
    return KFS_OK;
}

kfs_result_t kfs_write_binary(const char* name, const void* data, int length) {
    int slot = find_file(name);
    if (slot < 0) return KFS_NOT_FOUND;
    if (length < 0 || length >= KFS_DATA_MAX) return KFS_TOO_LARGE;
    const uint8_t* src = (const uint8_t*)data;
    for (int i = 0; i < length; i++) files[slot].data[i] = (char)src[i];
    files[slot].data[length] = 0;
    files[slot].size = length;
    save_dir();
    save_file_data(slot);
    return KFS_OK;
}

kfs_result_t kfs_write(const char* name, const char* data) {
    return kfs_write_binary(name, data, string_length(data));
}

kfs_result_t kfs_read(const char* name, const char** data, int* size) {
    int slot = find_file(name);
    if (slot < 0) return KFS_NOT_FOUND;
    *data = files[slot].data;
    *size = files[slot].size;
    return KFS_OK;
}

kfs_result_t kfs_remove(const char* name) {
    int slot = find_file(name);
    if (slot < 0) return KFS_NOT_FOUND;
    files[slot].used = 0;
    files[slot].size = 0;
    files[slot].name[0] = 0;
    files[slot].data[0] = 0;
    save_dir();
    return KFS_OK;
}

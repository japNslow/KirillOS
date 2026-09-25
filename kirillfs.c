#include "kirillfs.h"
#include "ata.h"
#include <stdint.h>

#define KFS_MAGIC 0x4B465331
#define KFS_FIRST_SECTOR 321

#define KFS_HEADER_SIZE 4
#define KFS_RECORD_SIZE (4 + 4 + KFS_NAME_MAX + KFS_DATA_MAX)
#define KFS_SECTOR_COUNT ((KFS_HEADER_SIZE + KFS_MAX_FILES * KFS_RECORD_SIZE + 511) / 512)

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

static void save(void) {
    int offset = 0;
    if (!persistent) return;
    for (int s = 0; s < KFS_SECTOR_COUNT; s++) {
        for (int i = 0; i < 512; i++) sector[i] = 0;
        if (s == 0) put32(sector, KFS_MAGIC);
        for (int f = 0; f < KFS_MAX_FILES; f++) {
            int base = KFS_HEADER_SIZE + f * KFS_RECORD_SIZE;
            for (int i = 0; i < 4; i++) {
                if (base + i >= s * 512 && base + i < (s + 1) * 512)
                    sector[base + i - s * 512] = (uint8_t)files[f].used;
            }
            offset = base + 4;
            for (int i = 0; i < 4; i++)
                if (offset + i >= s * 512 && offset + i < (s + 1) * 512)
                    sector[offset + i - s * 512] = (uint8_t)(files[f].size >> (i * 8));
            for (int i = 0; i < KFS_NAME_MAX + KFS_DATA_MAX; i++) {
                int pos = base + 8 + i;
                char value = i < KFS_NAME_MAX ? files[f].name[i] : files[f].data[i - KFS_NAME_MAX];
                if (pos >= s * 512 && pos < (s + 1) * 512)
                    sector[pos - s * 512] = (uint8_t)value;
            }
        }
        (void)ata_write28(KFS_FIRST_SECTOR + s, sector);
    }
}

static int load(void) {
    if (!persistent || !ata_read28(KFS_FIRST_SECTOR, sector) ||
        get32(sector) != KFS_MAGIC) return 0;
    for (int s = 0; s < KFS_SECTOR_COUNT; s++) {
        if (!ata_read28(KFS_FIRST_SECTOR + s, sector)) return 0;
        for (int f = 0; f < KFS_MAX_FILES; f++) {
            int base = KFS_HEADER_SIZE + f * KFS_RECORD_SIZE;
            int pos = base;
            if (pos >= s * 512 && pos + 3 < (s + 1) * 512)
                files[f].used = sector[pos - s * 512];
            pos = base + 4;
            if (pos >= s * 512 && pos + 3 < (s + 1) * 512)
                files[f].size = get32(sector + pos - s * 512);
            for (int i = 0; i < KFS_NAME_MAX + KFS_DATA_MAX; i++) {
                pos = base + 8 + i;
                if (pos >= s * 512 && pos < (s + 1) * 512) {
                    if (i < KFS_NAME_MAX) files[f].name[i] = sector[pos - s * 512];
                    else files[f].data[i - KFS_NAME_MAX] = sector[pos - s * 512];
                }
            }
        }
    }
    return 1;
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
    while (value[length]) length++;
    return length;
}

static int valid_name(const char* name) {
    int length;
    if (!name || !name[0]) return 0;
    length = string_length(name);
    if (length >= KFS_NAME_MAX) return 0;
    for (int i = 0; i < length; i++) {
        char c = name[i];
        if (c == ' ' || c == '/' || c == '\\') return 0;
    }
    return 1;
}

static int find_file(const char* name) {
    for (int i = 0; i < KFS_MAX_FILES; i++)
        if (files[i].used && string_equal(files[i].name, name)) return i;
    return -1;
}

void kfs_format(void) {
    for (int i = 0; i < KFS_MAX_FILES; i++) {
        files[i].used = 0;
        files[i].size = 0;
        files[i].name[0] = 0;
        files[i].data[0] = 0;
    }
    save();
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
    files[slot].data[0] = 0;
    save();
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
    save();
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
    save();
    return KFS_OK;
}

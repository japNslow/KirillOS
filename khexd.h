#ifndef KHEXD_H
#define KHEXD_H

#include <stdint.h>
#include <stddef.h>

#define KOBJ_MAGIC 0x4B4F424A   /* 'KOBJ' */
#define KHEX_MAGIC 0x4B484558   /* 'KHEX' */

/* .khex File Header */
typedef struct {
    uint32_t magic;          /* 0x4B484558 ('KHEX') */
    uint16_t version;        /* 0x0002 */
    uint16_t flags;          /* Flags: bit 0 = bytecode, bit 1 = native */
    uint32_t entry_offset;   /* Offset to entry point in code */
    uint32_t code_size;      /* Byte size of code section */
    uint32_t data_size;      /* Byte size of string/data section */
    char     name[16];       /* Program title */
} __attribute__((packed)) khex_exec_header_t;

/* Public KHEXD Compiler & Linker API */
int  khexd_compile(const char* src_file, const char* obj_file);
int  khexd_link(const char* obj_file, const char* khex_file);
int  khexd_build(const char* src_file, const char* khex_file);
int  khexd_run_khex(const char* filename);
void khexd_init_samples(void);

#endif

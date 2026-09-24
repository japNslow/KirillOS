#include "khexd.h"
#include "khex.h"
#include "kirillfs.h"
#include "vga.h"
#include "keyboard.h"
#include "sound.h"

/* ================================================================ */
/*                     Bytecode Instruction Opcodes                 */
/* ================================================================ */
#define OP_NOP        0x00
#define OP_PUSH_CONST 0x01   /* [imm32] */
#define OP_LOAD_VAR   0x02   /* [var_idx:uint8] */
#define OP_STORE_VAR  0x03   /* [var_idx:uint8] */
#define OP_ADD        0x04
#define OP_SUB        0x05
#define OP_MUL        0x06
#define OP_DIV        0x07
#define OP_MOD        0x08
#define OP_EQ         0x09
#define OP_NEQ        0x0A
#define OP_LT         0x0B
#define OP_GT         0x0C
#define OP_LTE        0x0D
#define OP_GTE        0x0E
#define OP_JMP        0x10   /* [offset:uint16] */
#define OP_JZ         0x11   /* [offset:uint16] */
#define OP_JNZ        0x12   /* [offset:uint16] */
#define OP_CALL_SYS   0x20   /* [sys_id:uint8] */
#define OP_EXIT       0xFF

/* System call IDs */
#define SYS_PRINT_STR 1
#define SYS_PRINT_NUM 2
#define SYS_PUTCHAR   3
#define SYS_CLEAR     4
#define SYS_SET_COLOR 5
#define SYS_SET_CURSOR 6
#define SYS_DRAW_CHAR 7
#define SYS_GETCHAR   8
#define SYS_POLLCHAR  9
#define SYS_BEEP      11
#define SYS_DELAY     12
#define SYS_RAND      13

/* ================================================================ */
/*                       Lexer & Parser State                       */
/* ================================================================ */
typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUM,
    TOK_STR,
    TOK_VAR,
    TOK_IF,
    TOK_WHILE,
    TOK_BREAK,
    TOK_FN,
    TOK_PRINT,
    TOK_COLOR,
    TOK_CLEAR,
    TOK_CURSOR,
    TOK_DRAW,
    TOK_BEEP,
    TOK_DELAY,
    TOK_RAND,
    TOK_GETCHAR,
    TOK_POLLCHAR,
    TOK_LBRACE,   /* { */
    TOK_RBRACE,   /* } */
    TOK_LPAREN,   /* ( */
    TOK_RPAREN,   /* ) */
    TOK_COMMA,    /* , */
    TOK_ASSIGN,   /* = */
    TOK_PLUS,     /* + */
    TOK_MINUS,    /* - */
    TOK_STAR,     /* * */
    TOK_SLASH,    /* / */
    TOK_PERCENT,  /* % */
    TOK_EQ,       /* == */
    TOK_NEQ,      /* != */
    TOK_LT,       /* < */
    TOK_GT,       /* > */
    TOK_LTE,      /* <= */
    TOK_GTE       /* >= */
} token_type_t;

typedef struct {
    token_type_t type;
    int32_t      num_val;
    char         str_val[64];
} token_t;

static const char* src_ptr;
static token_t     cur_tok;

static uint8_t code_buf[1024];
static int     code_len = 0;
static char    data_buf[512];
static int     data_len = 0;

static char var_names[32][16];
static int  var_count = 0;

static int find_or_add_var(const char* name) {
    for (int i = 0; i < var_count; i++) {
        const char* a = var_names[i];
        const char* b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return i;
    }
    if (var_count >= 32) return 0;
    int len = 0;
    while (name[len] && len < 15) {
        var_names[var_count][len] = name[len];
        len++;
    }
    var_names[var_count][len] = 0;
    return var_count++;
}

static int add_string_data(const char* str) {
    int start = data_len;
    while (*str && data_len < 510) {
        if (*str == '\\' && *(str + 1) == 'n') {
            data_buf[data_len++] = '\n';
            str += 2;
        } else if (*str == '\\' && *(str + 1) == 't') {
            data_buf[data_len++] = '\t';
            str += 2;
        } else {
            data_buf[data_len++] = *str++;
        }
    }
    data_buf[data_len++] = 0;
    return start;
}

static void emit_byte(uint8_t b) {
    if (code_len < 1024) code_buf[code_len++] = b;
}

static void emit_u16(uint16_t w) {
    emit_byte((uint8_t)(w & 0xFF));
    emit_byte((uint8_t)((w >> 8) & 0xFF));
}

static void emit_i32(int32_t val) {
    emit_byte((uint8_t)(val & 0xFF));
    emit_byte((uint8_t)((val >> 8) & 0xFF));
    emit_byte((uint8_t)((val >> 16) & 0xFF));
    emit_byte((uint8_t)((val >> 24) & 0xFF));
}

/* ================================================================ */
/*                             Lexer                                */
/* ================================================================ */
static void next_token(void) {
    while (*src_ptr) {
        /* Skip whitespace */
        if (*src_ptr == ' ' || *src_ptr == '\t' || *src_ptr == '\r' || *src_ptr == '\n') {
            src_ptr++;
            continue;
        }
        /* Skip line comments */
        if (*src_ptr == '/' && *(src_ptr + 1) == '/') {
            while (*src_ptr && *src_ptr != '\n') src_ptr++;
            continue;
        }
        break;
    }

    if (!*src_ptr) {
        cur_tok.type = TOK_EOF;
        return;
    }

    /* Single / multi-char operators */
    if (*src_ptr == '{') { cur_tok.type = TOK_LBRACE; src_ptr++; return; }
    if (*src_ptr == '}') { cur_tok.type = TOK_RBRACE; src_ptr++; return; }
    if (*src_ptr == '(') { cur_tok.type = TOK_LPAREN; src_ptr++; return; }
    if (*src_ptr == ')') { cur_tok.type = TOK_RPAREN; src_ptr++; return; }
    if (*src_ptr == ',') { cur_tok.type = TOK_COMMA;  src_ptr++; return; }
    if (*src_ptr == '+') { cur_tok.type = TOK_PLUS;   src_ptr++; return; }
    if (*src_ptr == '-') { cur_tok.type = TOK_MINUS;  src_ptr++; return; }
    if (*src_ptr == '*') { cur_tok.type = TOK_STAR;   src_ptr++; return; }
    if (*src_ptr == '/') { cur_tok.type = TOK_SLASH;  src_ptr++; return; }
    if (*src_ptr == '%') { cur_tok.type = TOK_PERCENT;src_ptr++; return; }

    if (*src_ptr == '=') {
        if (*(src_ptr + 1) == '=') { src_ptr += 2; cur_tok.type = TOK_EQ; return; }
        src_ptr++; cur_tok.type = TOK_ASSIGN; return;
    }
    if (*src_ptr == '!') {
        if (*(src_ptr + 1) == '=') { src_ptr += 2; cur_tok.type = TOK_NEQ; return; }
    }
    if (*src_ptr == '<') {
        if (*(src_ptr + 1) == '=') { src_ptr += 2; cur_tok.type = TOK_LTE; return; }
        src_ptr++; cur_tok.type = TOK_LT; return;
    }
    if (*src_ptr == '>') {
        if (*(src_ptr + 1) == '=') { src_ptr += 2; cur_tok.type = TOK_GTE; return; }
        src_ptr++; cur_tok.type = TOK_GT; return;
    }

    /* String literal */
    if (*src_ptr == '"') {
        src_ptr++;
        int len = 0;
        while (*src_ptr && *src_ptr != '"' && len < 62) {
            cur_tok.str_val[len++] = *src_ptr++;
        }
        cur_tok.str_val[len] = 0;
        if (*src_ptr == '"') src_ptr++;
        cur_tok.type = TOK_STR;
        return;
    }

    /* Number literal */
    if (*src_ptr >= '0' && *src_ptr <= '9') {
        int32_t val = 0;
        while (*src_ptr >= '0' && *src_ptr <= '9') {
            val = val * 10 + (*src_ptr - '0');
            src_ptr++;
        }
        cur_tok.type = TOK_NUM;
        cur_tok.num_val = val;
        return;
    }

    /* Identifier or Keyword */
    if ((*src_ptr >= 'a' && *src_ptr <= 'z') || (*src_ptr >= 'A' && *src_ptr <= 'Z') || *src_ptr == '_') {
        int len = 0;
        while (((*src_ptr >= 'a' && *src_ptr <= 'z') ||
                (*src_ptr >= 'A' && *src_ptr <= 'Z') ||
                (*src_ptr >= '0' && *src_ptr <= '9') ||
                *src_ptr == '_') && len < 30) {
            cur_tok.str_val[len++] = *src_ptr++;
        }
        cur_tok.str_val[len] = 0;

        const char* s = cur_tok.str_val;
        #define MATCH(k) (s[0] == k[0] && s[1] == k[1])
        if (s[0]=='v'&&s[1]=='a'&&s[2]=='r'&&s[3]==0)      { cur_tok.type = TOK_VAR; return; }
        if (s[0]=='i'&&s[1]=='f'&&s[2]==0)                 { cur_tok.type = TOK_IF; return; }
        if (s[0]=='w'&&s[1]=='h'&&s[2]=='i'&&s[3]=='l'&&s[4]=='e') { cur_tok.type = TOK_WHILE; return; }
        if (s[0]=='b'&&s[1]=='r'&&s[2]=='e'&&s[3]=='a'&&s[4]=='k') { cur_tok.type = TOK_BREAK; return; }
        if (s[0]=='f'&&s[1]=='n'&&s[2]==0)                 { cur_tok.type = TOK_FN; return; }
        if (s[0]=='p'&&s[1]=='r'&&s[2]=='i'&&s[3]=='n'&&s[4]=='t'&&s[5]==0) { cur_tok.type = TOK_PRINT; return; }
        if (s[0]=='c'&&s[1]=='o'&&s[2]=='l'&&s[3]=='o'&&s[4]=='r') { cur_tok.type = TOK_COLOR; return; }
        if (s[0]=='c'&&s[1]=='l'&&s[2]=='e'&&s[3]=='a'&&s[4]=='r') { cur_tok.type = TOK_CLEAR; return; }
        if (s[0]=='c'&&s[1]=='u'&&s[2]=='r'&&s[3]=='s'&&s[4]=='o'&&s[5]=='r') { cur_tok.type = TOK_CURSOR; return; }
        if (s[0]=='d'&&s[1]=='r'&&s[2]=='a'&&s[3]=='w'&&s[4]==0)   { cur_tok.type = TOK_DRAW; return; }
        if (s[0]=='b'&&s[1]=='e'&&s[2]=='e'&&s[3]=='p')            { cur_tok.type = TOK_BEEP; return; }
        if (s[0]=='d'&&s[1]=='e'&&s[2]=='l'&&s[3]=='a'&&s[4]=='y') { cur_tok.type = TOK_DELAY; return; }
        if (s[0]=='r'&&s[1]=='a'&&s[2]=='n'&&s[3]=='d')            { cur_tok.type = TOK_RAND; return; }
        if (s[0]=='g'&&s[1]=='e'&&s[2]=='t'&&s[3]=='c')            { cur_tok.type = TOK_GETCHAR; return; }
        if (s[0]=='p'&&s[1]=='o'&&s[2]=='l'&&s[3]=='l')            { cur_tok.type = TOK_POLLCHAR; return; }

        cur_tok.type = TOK_IDENT;
        return;
    }

    src_ptr++;
    next_token();
}

/* ================================================================ */
/*                            Parser                                */
/* ================================================================ */
static void parse_expression(void);

static void parse_primary(void) {
    if (cur_tok.type == TOK_NUM) {
        emit_byte(OP_PUSH_CONST);
        emit_i32(cur_tok.num_val);
        next_token();
    } else if (cur_tok.type == TOK_GETCHAR) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        if (cur_tok.type == TOK_RPAREN) next_token();
        emit_byte(OP_CALL_SYS);
        emit_byte(SYS_GETCHAR);
    } else if (cur_tok.type == TOK_POLLCHAR) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        if (cur_tok.type == TOK_RPAREN) next_token();
        emit_byte(OP_CALL_SYS);
        emit_byte(SYS_POLLCHAR);
    } else if (cur_tok.type == TOK_RAND) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        parse_expression();
        if (cur_tok.type == TOK_RPAREN) next_token();
        emit_byte(OP_CALL_SYS);
        emit_byte(SYS_RAND);
    } else if (cur_tok.type == TOK_IDENT) {
        int v = find_or_add_var(cur_tok.str_val);
        emit_byte(OP_LOAD_VAR);
        emit_byte((uint8_t)v);
        next_token();
    } else if (cur_tok.type == TOK_LPAREN) {
        next_token();
        parse_expression();
        if (cur_tok.type == TOK_RPAREN) next_token();
    }
}

static void parse_multiplicative(void) {
    parse_primary();
    while (cur_tok.type == TOK_STAR || cur_tok.type == TOK_SLASH || cur_tok.type == TOK_PERCENT) {
        token_type_t op = cur_tok.type;
        next_token();
        parse_primary();
        if (op == TOK_STAR) emit_byte(OP_MUL);
        else if (op == TOK_SLASH) emit_byte(OP_DIV);
        else if (op == TOK_PERCENT) emit_byte(OP_MOD);
    }
}

static void parse_additive(void) {
    parse_multiplicative();
    while (cur_tok.type == TOK_PLUS || cur_tok.type == TOK_MINUS) {
        token_type_t op = cur_tok.type;
        next_token();
        parse_multiplicative();
        if (op == TOK_PLUS) emit_byte(OP_ADD);
        else emit_byte(OP_SUB);
    }
}

static void parse_comparison(void) {
    parse_additive();
    while (cur_tok.type >= TOK_EQ && cur_tok.type <= TOK_GTE) {
        token_type_t op = cur_tok.type;
        next_token();
        parse_additive();
        if (op == TOK_EQ)  emit_byte(OP_EQ);
        if (op == TOK_NEQ) emit_byte(OP_NEQ);
        if (op == TOK_LT)  emit_byte(OP_LT);
        if (op == TOK_GT)  emit_byte(OP_GT);
        if (op == TOK_LTE) emit_byte(OP_LTE);
        if (op == TOK_GTE) emit_byte(OP_GTE);
    }
}

static void parse_expression(void) {
    parse_comparison();
}

static void parse_statement(void);

static void parse_block(void) {
    if (cur_tok.type == TOK_LBRACE) next_token();
    while (cur_tok.type != TOK_RBRACE && cur_tok.type != TOK_EOF) {
        parse_statement();
    }
    if (cur_tok.type == TOK_RBRACE) next_token();
}

static void parse_statement(void) {
    if (cur_tok.type == TOK_VAR) {
        next_token();
        char vname[16];
        int l = 0; while (cur_tok.str_val[l] && l < 15) { vname[l] = cur_tok.str_val[l]; l++; }
        vname[l] = 0;
        int idx = find_or_add_var(vname);
        next_token();
        if (cur_tok.type == TOK_ASSIGN) {
            next_token();
            parse_expression();
            emit_byte(OP_STORE_VAR);
            emit_byte((uint8_t)idx);
        }
    }
    else if (cur_tok.type == TOK_IDENT) {
        char vname[16];
        int l = 0; while (cur_tok.str_val[l] && l < 15) { vname[l] = cur_tok.str_val[l]; l++; }
        vname[l] = 0;
        int idx = find_or_add_var(vname);
        next_token();
        if (cur_tok.type == TOK_ASSIGN) {
            next_token();
            parse_expression();
            emit_byte(OP_STORE_VAR);
            emit_byte((uint8_t)idx);
        }
    }
    else if (cur_tok.type == TOK_PRINT) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        if (cur_tok.type == TOK_STR) {
            int off = add_string_data(cur_tok.str_val);
            emit_byte(OP_PUSH_CONST);
            emit_i32(off);
            emit_byte(OP_CALL_SYS);
            emit_byte(SYS_PRINT_STR);
            next_token();
        } else {
            parse_expression();
            emit_byte(OP_CALL_SYS);
            emit_byte(SYS_PRINT_NUM);
        }
        if (cur_tok.type == TOK_RPAREN) next_token();
    }
    else if (cur_tok.type == TOK_COLOR) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        parse_expression();
        if (cur_tok.type == TOK_COMMA) next_token();
        parse_expression();
        if (cur_tok.type == TOK_RPAREN) next_token();
        emit_byte(OP_CALL_SYS);
        emit_byte(SYS_SET_COLOR);
    }
    else if (cur_tok.type == TOK_CLEAR) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        if (cur_tok.type == TOK_RPAREN) next_token();
        emit_byte(OP_CALL_SYS);
        emit_byte(SYS_CLEAR);
    }
    else if (cur_tok.type == TOK_CURSOR) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        parse_expression();
        if (cur_tok.type == TOK_COMMA) next_token();
        parse_expression();
        if (cur_tok.type == TOK_RPAREN) next_token();
        emit_byte(OP_CALL_SYS);
        emit_byte(SYS_SET_CURSOR);
    }
    else if (cur_tok.type == TOK_DRAW) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        parse_expression(); if (cur_tok.type == TOK_COMMA) next_token();
        parse_expression(); if (cur_tok.type == TOK_COMMA) next_token();
        parse_expression(); if (cur_tok.type == TOK_COMMA) next_token();
        parse_expression();
        if (cur_tok.type == TOK_RPAREN) next_token();
        emit_byte(OP_CALL_SYS);
        emit_byte(SYS_DRAW_CHAR);
    }
    else if (cur_tok.type == TOK_BEEP) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        parse_expression();
        if (cur_tok.type == TOK_COMMA) next_token();
        parse_expression();
        if (cur_tok.type == TOK_RPAREN) next_token();
        emit_byte(OP_CALL_SYS);
        emit_byte(SYS_BEEP);
    }
    else if (cur_tok.type == TOK_DELAY) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        parse_expression();
        if (cur_tok.type == TOK_RPAREN) next_token();
        emit_byte(OP_CALL_SYS);
        emit_byte(SYS_DELAY);
    }
    else if (cur_tok.type == TOK_IF) {
        next_token();
        if (cur_tok.type == TOK_LPAREN) next_token();
        parse_expression();
        if (cur_tok.type == TOK_RPAREN) next_token();

        emit_byte(OP_JZ);
        int patch_loc = code_len;
        emit_u16(0); /* placeholder */

        parse_block();

        /* Patch jump target */
        uint16_t target = (uint16_t)code_len;
        code_buf[patch_loc] = (uint8_t)(target & 0xFF);
        code_buf[patch_loc + 1] = (uint8_t)((target >> 8) & 0xFF);
    }
    else if (cur_tok.type == TOK_WHILE) {
        next_token();
        uint16_t loop_start = (uint16_t)code_len;
        if (cur_tok.type == TOK_LPAREN) next_token();
        parse_expression();
        if (cur_tok.type == TOK_RPAREN) next_token();

        emit_byte(OP_JZ);
        int patch_exit = code_len;
        emit_u16(0);

        parse_block();

        emit_byte(OP_JMP);
        emit_u16(loop_start);

        uint16_t loop_end = (uint16_t)code_len;
        code_buf[patch_exit] = (uint8_t)(loop_end & 0xFF);
        code_buf[patch_exit + 1] = (uint8_t)((loop_end >> 8) & 0xFF);
    }
    else if (cur_tok.type == TOK_BREAK) {
        next_token();
        emit_byte(OP_EXIT);
    }
    else {
        next_token();
    }
}

/* ================================================================ */
/*                   Compiler Implementation                        */
/* ================================================================ */
int khexd_compile(const char* src_file, const char* obj_file) {
    const char* src;
    int size = 0;

    if (kfs_read(src_file, &src, &size) != KFS_OK) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("[KHEXD] Cannot open source file: ");
        vga_write(src_file);
        vga_newline();
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return 0;
    }

    code_len = 0;
    data_len = 0;
    var_count = 0;
    src_ptr = src;

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("[KHEXD] Compiling '");
    vga_write(src_file);
    vga_write("' (");
    vga_write_hex((uint32_t)size, 4);
    vga_write(" bytes)...\n");

    next_token();

    while (cur_tok.type != TOK_EOF) {
        if (cur_tok.type == TOK_FN) {
            next_token(); /* skip fn */
            if (cur_tok.type == TOK_IDENT) next_token(); /* function name */
            if (cur_tok.type == TOK_LPAREN) next_token();
            if (cur_tok.type == TOK_RPAREN) next_token();
            parse_block();
        } else {
            parse_statement();
        }
    }

    emit_byte(OP_EXIT);

    /* Construct K-Object payload */
    uint8_t obj_buf[1536];
    int obj_pos = 0;

    /* Magic */
    obj_buf[obj_pos++] = 0x4A; obj_buf[obj_pos++] = 0x42;
    obj_buf[obj_pos++] = 0x4F; obj_buf[obj_pos++] = 0x4B; /* 'KOBJ' */

    /* Code size (2 bytes) */
    obj_buf[obj_pos++] = (uint8_t)(code_len & 0xFF);
    obj_buf[obj_pos++] = (uint8_t)((code_len >> 8) & 0xFF);

    /* Data size (2 bytes) */
    obj_buf[obj_pos++] = (uint8_t)(data_len & 0xFF);
    obj_buf[obj_pos++] = (uint8_t)((data_len >> 8) & 0xFF);

    /* Code bytes */
    for (int i = 0; i < code_len; i++) obj_buf[obj_pos++] = code_buf[i];

    /* Data bytes */
    for (int i = 0; i < data_len; i++) obj_buf[obj_pos++] = (uint8_t)data_buf[i];

    if (kfs_touch(obj_file) != KFS_OK && kfs_touch(obj_file) != KFS_EXISTS) {
        /* proceed to write */
    }
    kfs_write_binary(obj_file, obj_buf, obj_pos);

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("[KHEXD] Compiled successfully -> '");
    vga_write(obj_file);
    vga_write("' (code: ");
    vga_write_hex((uint32_t)code_len, 4);
    vga_write(" B, data: ");
    vga_write_hex((uint32_t)data_len, 4);
    vga_write(" B)\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    return 1;
}

/* ================================================================ */
/*                     Linker Implementation                        */
/* ================================================================ */
int khexd_link(const char* obj_file, const char* khex_file) {
    const char* obj_data;
    int obj_size = 0;

    if (kfs_read(obj_file, &obj_data, &obj_size) != KFS_OK) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("[KHEXD] Object file not found: ");
        vga_write(obj_file);
        vga_newline();
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return 0;
    }

    if (obj_size < 8) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("[KHEXD] Invalid object file (too small)\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return 0;
    }

    const uint8_t* p = (const uint8_t*)obj_data;
    uint16_t c_size = (uint16_t)(p[4] | (p[5] << 8));
    uint16_t d_size = (uint16_t)(p[6] | (p[7] << 8));

    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    vga_write("[KHEXD] Linking '");
    vga_write(obj_file);
    vga_write("' -> '");
    vga_write(khex_file);
    vga_write("'...\n");

    /* Create .khex executable */
    uint8_t out_buf[2048];
    int out_pos = 0;

    khex_exec_header_t hdr;
    hdr.magic        = KHEX_MAGIC;
    hdr.version      = 0x0002;
    hdr.flags        = 1; /* Bytecode executable */
    hdr.entry_offset = sizeof(khex_exec_header_t);
    hdr.code_size    = c_size;
    hdr.data_size    = d_size;

    int nl = 0;
    while (khex_file[nl] && nl < 15) { hdr.name[nl] = khex_file[nl]; nl++; }
    hdr.name[nl] = 0;

    /* Write header */
    uint8_t* hp = (uint8_t*)&hdr;
    for (size_t i = 0; i < sizeof(khex_exec_header_t); i++) {
        out_buf[out_pos++] = hp[i];
    }

    /* Write code */
    for (int i = 0; i < c_size; i++) {
        out_buf[out_pos++] = p[8 + i];
    }

    /* Write data */
    for (int i = 0; i < d_size; i++) {
        out_buf[out_pos++] = p[8 + c_size + i];
    }

    kfs_touch(khex_file);
    kfs_write_binary(khex_file, out_buf, out_pos);

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    vga_write("[KHEXD] Executable created -> '");
    vga_write(khex_file);
    vga_write("' (");
    vga_write_hex((uint32_t)out_pos, 4);
    vga_write(" bytes)\n");
    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);

    return 1;
}

/* One-step build: .k -> .ko -> .khex */
int khexd_build(const char* src_file, const char* khex_file) {
    char obj_name[32];
    int i = 0;
    while (src_file[i] && src_file[i] != '.' && i < 20) {
        obj_name[i] = src_file[i];
        i++;
    }
    obj_name[i++] = '.'; obj_name[i++] = 'k'; obj_name[i++] = 'o'; obj_name[i] = 0;

    if (!khexd_compile(src_file, obj_name)) return 0;
    return khexd_link(obj_name, khex_file);
}

/* ================================================================ */
/*                     Virtual Machine Execution                    */
/* ================================================================ */
int khexd_run_khex(const char* filename) {
    const char* data;
    int size = 0;

    if (kfs_read(filename, &data, &size) != KFS_OK) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("khexd: file not found: ");
        vga_write(filename);
        vga_newline();
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }

    if (size < (int)sizeof(khex_exec_header_t)) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("khexd: corrupted .khex binary\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }

    khex_exec_header_t* hdr = (khex_exec_header_t*)data;
    if (hdr->magic != KHEX_MAGIC) {
        vga_set_color(VGA_LIGHT_RED, VGA_BLACK);
        vga_write("khexd: invalid KHEX magic header\n");
        vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
        return -1;
    }

    const uint8_t* code = (const uint8_t*)(data + hdr->entry_offset);
    const char* strings = (const char*)(data + hdr->entry_offset + hdr->code_size);

    int32_t stack[64];
    int     sp = 0;
    int32_t vars[32];
    for (int i = 0; i < 32; i++) vars[i] = 0;

    uint32_t pc = 0;

    while (pc < hdr->code_size) {
        uint8_t op = code[pc++];

        if (op == OP_NOP) continue;
        if (op == OP_EXIT) break;

        if (op == OP_PUSH_CONST) {
            int32_t val = (int32_t)(code[pc] | (code[pc + 1] << 8) |
                                   (code[pc + 2] << 16) | (code[pc + 3] << 24));
            pc += 4;
            if (sp < 64) stack[sp++] = val;
        }
        else if (op == OP_LOAD_VAR) {
            uint8_t idx = code[pc++];
            if (sp < 64) stack[sp++] = vars[idx < 32 ? idx : 0];
        }
        else if (op == OP_STORE_VAR) {
            uint8_t idx = code[pc++];
            if (sp > 0) vars[idx < 32 ? idx : 0] = stack[--sp];
        }
        else if (op == OP_ADD) {
            if (sp >= 2) { int32_t b = stack[--sp]; stack[sp - 1] += b; }
        }
        else if (op == OP_SUB) {
            if (sp >= 2) { int32_t b = stack[--sp]; stack[sp - 1] -= b; }
        }
        else if (op == OP_MUL) {
            if (sp >= 2) { int32_t b = stack[--sp]; stack[sp - 1] *= b; }
        }
        else if (op == OP_DIV) {
            if (sp >= 2) { int32_t b = stack[--sp]; if (b != 0) stack[sp - 1] /= b; }
        }
        else if (op == OP_MOD) {
            if (sp >= 2) { int32_t b = stack[--sp]; if (b != 0) stack[sp - 1] %= b; }
        }
        else if (op == OP_EQ) {
            if (sp >= 2) { int32_t b = stack[--sp]; stack[sp - 1] = (stack[sp - 1] == b); }
        }
        else if (op == OP_NEQ) {
            if (sp >= 2) { int32_t b = stack[--sp]; stack[sp - 1] = (stack[sp - 1] != b); }
        }
        else if (op == OP_LT) {
            if (sp >= 2) { int32_t b = stack[--sp]; stack[sp - 1] = (stack[sp - 1] < b); }
        }
        else if (op == OP_GT) {
            if (sp >= 2) { int32_t b = stack[--sp]; stack[sp - 1] = (stack[sp - 1] > b); }
        }
        else if (op == OP_LTE) {
            if (sp >= 2) { int32_t b = stack[--sp]; stack[sp - 1] = (stack[sp - 1] <= b); }
        }
        else if (op == OP_GTE) {
            if (sp >= 2) { int32_t b = stack[--sp]; stack[sp - 1] = (stack[sp - 1] >= b); }
        }
        else if (op == OP_JMP) {
            uint16_t tgt = (uint16_t)(code[pc] | (code[pc + 1] << 8));
            pc = tgt;
        }
        else if (op == OP_JZ) {
            uint16_t tgt = (uint16_t)(code[pc] | (code[pc + 1] << 8));
            pc += 2;
            if (sp > 0 && stack[--sp] == 0) pc = tgt;
        }
        else if (op == OP_JNZ) {
            uint16_t tgt = (uint16_t)(code[pc] | (code[pc + 1] << 8));
            pc += 2;
            if (sp > 0 && stack[--sp] != 0) pc = tgt;
        }
        else if (op == OP_CALL_SYS) {
            uint8_t sys = code[pc++];
            if (sys == SYS_PRINT_STR) {
                if (sp > 0) {
                    int32_t off = stack[--sp];
                    if (off >= 0 && off < (int32_t)hdr->data_size) {
                        vga_write(strings + off);
                    }
                }
            } else if (sys == SYS_PRINT_NUM) {
                if (sp > 0) {
                    int32_t val = stack[--sp];
                    char num_b[12]; int nl = 0; int is_neg = 0;
                    if (val < 0) { is_neg = 1; val = -val; }
                    if (val == 0) num_b[nl++] = '0';
                    while (val > 0) { num_b[nl++] = (char)('0' + (val % 10)); val /= 10; }
                    if (is_neg) vga_putchar('-');
                    while (nl > 0) vga_putchar(num_b[--nl]);
                }
            } else if (sys == SYS_PUTCHAR) {
                if (sp > 0) vga_putchar((char)stack[--sp]);
            } else if (sys == SYS_CLEAR) {
                vga_clear();
            } else if (sys == SYS_SET_COLOR) {
                if (sp >= 2) {
                    uint8_t bg = (uint8_t)stack[--sp];
                    uint8_t fg = (uint8_t)stack[--sp];
                    vga_set_color(fg, bg);
                }
            } else if (sys == SYS_SET_CURSOR) {
                if (sp >= 2) {
                    uint32_t col = (uint32_t)stack[--sp];
                    uint32_t row = (uint32_t)stack[--sp];
                    vga_set_cursor(row, col);
                }
            } else if (sys == SYS_DRAW_CHAR) {
                if (sp >= 4) {
                    uint8_t col = (uint8_t)stack[--sp];
                    char ch     = (char)stack[--sp];
                    uint32_t c  = (uint32_t)stack[--sp];
                    uint32_t r  = (uint32_t)stack[--sp];
                    if (r < VGA_HEIGHT && c < VGA_WIDTH) {
                        VGA_MEMORY[r * VGA_WIDTH + c] = ((uint16_t)col << 8) | (uint8_t)ch;
                    }
                }
            } else if (sys == SYS_GETCHAR) {
                char ch = keyboard_getchar();
                if (sp < 64) stack[sp++] = (int32_t)ch;
            } else if (sys == SYS_POLLCHAR) {
                char ch = keyboard_poll();
                if (sp < 64) stack[sp++] = (int32_t)ch;
            } else if (sys == SYS_BEEP) {
                if (sp >= 2) {
                    uint32_t ms = (uint32_t)stack[--sp];
                    uint32_t fr = (uint32_t)stack[--sp];
                    sound_soft_note(fr, ms);
                }
            } else if (sys == SYS_DELAY) {
                if (sp > 0) {
                    uint32_t ms = (uint32_t)stack[--sp];
                    sound_note(0, ms);
                }
            } else if (sys == SYS_RAND) {
                if (sp > 0) {
                    int32_t max = stack[--sp];
                    static uint32_t s = 0x98765432;
                    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
                    int32_t res = max > 0 ? (int32_t)(s % max) : 0;
                    stack[sp++] = res;
                }
            }
        }
    }

    vga_set_color(VGA_LIGHT_GREY, VGA_BLACK);
    return 0;
}

/* ================================================================ */
/*                     Preload Sample .k Programs                   */
/* ================================================================ */
void khexd_init_samples(void) {
    /* 1. hello.k */
    if (kfs_touch("hello.k") == KFS_OK) {
        kfs_write("hello.k",
            "fn main() {\n"
            "    clear()\n"
            "    color(10, 0)\n"
            "    print(\"===============================\\n\")\n"
            "    print(\"   HELLO FROM K-LANGUAGE!      \\n\")\n"
            "    print(\"===============================\\n\")\n"
            "    beep(523, 100)\n"
            "    beep(659, 100)\n"
            "    beep(784, 200)\n"
            "    color(14, 0)\n"
            "    print(\"Your lucky number today is: \")\n"
            "    var num = rand(100)\n"
            "    print(num)\n"
            "    print(\"\\n\\nPress any key to exit...\")\n"
            "    var k = getchar()\n"
            "}\n");
        khexd_build("hello.k", "hello.khex");
    }

    /* 2. clicker.k */
    if (kfs_touch("clicker.k") == KFS_OK) {
        kfs_write("clicker.k",
            "fn main() {\n"
            "    clear()\n"
            "    color(11, 0)\n"
            "    print(\"=== SPACEBAR CLICKER (K-LANG) ===\\n\")\n"
            "    print(\"Press SPACE to score, Q to quit.\\n\\n\")\n"
            "    var score = 0\n"
            "    while (1) {\n"
            "        var key = getchar()\n"
            "        if (key == 32) {\n"
            "            score = score + 1\n"
            "            color(14, 0)\n"
            "            print(\"Score: \")\n"
            "            print(score)\n"
            "            print(\"\\n\")\n"
            "            beep(750, 40)\n"
            "        }\n"
            "        if (key == 113) {\n"
            "            color(12, 0)\n"
            "            print(\"Game Over! Final score: \")\n"
            "            print(score)\n"
            "            print(\"\\n\")\n"
            "            break\n"
            "        }\n"
            "    }\n"
            "}\n");
        khexd_build("clicker.k", "clicker.khex");
    }
}

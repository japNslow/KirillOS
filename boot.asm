BITS 32

; --- Multiboot заголовок (GRUB) ---
MBALIGN     equ 1 << 0
MEMINFO     equ 1 << 1
FLAGS       equ MBALIGN | MEMINFO
MAGIC       equ 0x1BADB002
CHECKSUM    equ -(MAGIC + FLAGS)

section .multiboot
align 4
    dd MAGIC
    dd FLAGS
    dd CHECKSUM

; --- Стек ядра ---
section .bss
align 16
stack_bottom:
    resb 16384       ; 16 КБ стека
stack_top:
global boot_row
boot_row:
    resd 1
global boot_col
boot_col:
    resd 1
boot_color:
    resb 1
boot_magic_value:
    resd 1
boot_mbi_value:
    resd 1

; --- Точка входа ---
section .text
global _start
extern kernel_main

_start:
    mov esp, stack_top
    mov esi, eax
    mov edi, ebx
    mov [boot_magic_value], esi
    mov [boot_mbi_value], edi
    call boot_screen_init
    mov al, 0x0B
    mov [boot_color], al
    mov esi, boot_log
    call boot_write
    mov esi, boot_magic_label
    call boot_write
    mov eax, [boot_magic_value]
    call boot_hex
    call boot_newline
    mov esi, boot_mbi_label
    call boot_write
    mov eax, [boot_mbi_value]
    call boot_hex
    call boot_newline
    cmp dword [boot_magic_value], 0x2BADB002
    jne boot_bad_magic
    mov esi, boot_ok
    call boot_write
    jmp boot_continue
boot_bad_magic:
    mov al, 0x0C
    mov [boot_color], al
    mov esi, boot_bad
    call boot_write
boot_continue:
    mov al, 0x0B
    mov [boot_color], al
    mov esi, boot_handoff
    call boot_write
    mov eax, [boot_magic_value]
    push dword [boot_mbi_value]
    push eax
    call kernel_main
    add esp, 8

.hang:
    cli
    hlt
    jmp .hang

boot_screen_init:
    mov dword [boot_row], 0
    mov dword [boot_col], 0
    mov byte [boot_color], 0x07
    mov edi, 0xB8000
    mov ecx, 2000
    mov ax, 0x0720
    rep stosw
    ret

boot_newline:
    inc dword [boot_row]
    mov dword [boot_col], 0
    call boot_scroll
    ret

boot_scroll:
    cmp dword [boot_row], 25
    jb .done
    mov esi, 0xB80A0
    mov edi, 0xB8000
    mov ecx, 1920
    rep movsw
    mov ax, 0x0720
    mov ecx, 80
    rep stosw
    mov dword [boot_row], 24
.done:
    ret

boot_putc:
    cmp al, 10
    jne .not_newline
    push esi
    push edi
    call boot_newline
    pop edi
    pop esi
    ret
.not_newline:
    movzx ecx, word [boot_row]
    imul ecx, 160
    movzx edx, word [boot_col]
    imul edx, 2
    add ecx, edx
    mov edi, 0xB8000
    add edi, ecx
    mov ah, [boot_color]
    stosw
    inc dword [boot_col]
    cmp dword [boot_col], 80
    jb .done
    call boot_newline
.done:
    ret

boot_write:
    lodsb
    test al, al
    jz .done
    call boot_putc
    jmp boot_write
.done:
    ret

boot_hex:
    push ebx
    push ecx
    push edx
    mov ebx, eax
    mov ecx, 8
.loop:
    rol ebx, 4
    mov al, bl
    and al, 0x0F
    cmp al, 10
    jb .digit
    add al, 'A' - 10
    jmp .emit
.digit:
    add al, '0'
.emit:
    push ecx
    call boot_putc
    pop ecx
    loop .loop
    pop edx
    pop ecx
    pop ebx
    ret

boot_log db "[KirillBoot] starting real-mode handoff -> protected mode", 10
         db "[KirillBoot] VGA text console online", 10
         db "[KirillBoot] stack: 16 KiB reserved", 10
         db "[KirillBoot] checking boot contract", 10, 0
boot_magic_label db "[KirillBoot] multiboot magic: 0x", 0
boot_mbi_label db "[KirillBoot] multiboot info:  0x", 0
boot_ok db "[KirillBoot] boot contract: OK", 10
        db "[KirillBoot] loading KirillOS kernel services...", 10, 0
boot_bad db "[KirillBoot] ERROR: invalid multiboot magic", 10, 0
boot_handoff db "[KirillBoot] handoff -> kernel_main", 10, 0
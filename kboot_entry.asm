[BITS 32]
global _kboot_start
global isr_keyboard_stub
global isr_default_stub
global jump_to_kernel

extern kboot_main
extern kboot_keyboard_isr_c

section .text
_kboot_start:
    ; Инициализация 32-битных сегментов
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x70000

    ; Переход в C-код терминала kboot
    call kboot_main

.hang:
    hlt
    jmp .hang

; Ассемблерная обёртка для аппаратного прерывания клавиатуры (IRQ1 / INT 0x21)
isr_keyboard_stub:
    pushad
    cld
    call kboot_keyboard_isr_c
    popad
    iretd

; Заглушка по умолчанию для остальных прерываний PIC
isr_default_stub:
    push eax
    mov al, 0x20
    out 0x20, al
    pop eax
    iretd

; void jump_to_kernel(uint32_t entry_point, uint32_t magic, uint32_t mbi_addr);
jump_to_kernel:
    mov edx, [esp + 4]    ; Адрес точки входа ядра
    mov eax, [esp + 8]    ; Multiboot magic (0x2BADB002)
    mov ebx, [esp + 12]   ; Указатель на структуру mbi
    cli                   ; Отключаем прерывания перед передачей управления ядру
    jmp edx

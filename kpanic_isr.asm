BITS 32
; Kirill Panic: точки входа для всех 256 векторов IDT.
; Каждая заглушка кладёт в стек код ошибки (0, если CPU его не кладёт сам)
; и номер вектора, затем kpanic_common сохраняет регистры и зовёт kpanic_handle.

global kpanic_isr_table
global kpanic_irq_master_stub
global kpanic_irq_slave_stub
extern kpanic_handle

section .text

; Исключения, для которых CPU сам кладёт код ошибки
%define HAS_ERROR(n) ((n) == 8 || (n) == 10 || (n) == 11 || (n) == 12 || (n) == 13 || (n) == 14 || (n) == 17 || (n) == 21 || (n) == 29 || (n) == 30)

%assign i 0
%rep 256
kpanic_isr_ %+ i:
%if !HAS_ERROR(i)
    push dword 0
%endif
    push dword i
    jmp kpanic_common
%assign i i + 1
%endrep

kpanic_common:
    pushad
    push ds
    push es
    push fs
    push gs
    cld
    mov eax, esp                ; eax -> kpanic_frame_t
    mov esp, kpanic_stack_top   ; свой стек: старый может быть испорчен
    push eax
    call kpanic_handle
.hang:
    cli
    hlt
    jmp .hang

; IRQ 0..7 и 8..15 (PIC после ремапа kboot): подтвердить и вернуться
kpanic_irq_master_stub:
    push eax
    mov al, 0x20
    out 0x20, al
    pop eax
    iretd

kpanic_irq_slave_stub:
    push eax
    mov al, 0x20
    out 0xA0, al
    out 0x20, al
    pop eax
    iretd

section .rodata
align 4
kpanic_isr_table:
%assign i 0
%rep 256
    dd kpanic_isr_ %+ i
%assign i i + 1
%endrep

section .bss
align 16
kpanic_stack:
    resb 8192
kpanic_stack_top:

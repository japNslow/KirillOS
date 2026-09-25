[BITS 16]
[ORG 0x7C00]

start:
    jmp short entry
    nop

entry:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov [boot_drive], dl    ; сохранить номер загрузочного диска от BIOS

    ; Переключить видеорежим 80x25 16 цветов
    mov ah, 0x00
    mov al, 0x03
    int 0x10

    mov si, msg_mbr
    call puts16

    ; Включение A20
    call enable_a20

    ; Загрузка kboot.bin (64 сектора = 32 КБ, LBA 1) в 0x0800:0x0000 (физ. 0x8000)
    mov dword [dap_lba_low], 1
    mov dword [dap_lba_high], 0
    mov word [dap_count], 64
    mov word [dap_segment], 0x0800
    mov word [dap_offset], 0x0000
    call read_sectors

    ; Загрузка kernel.bin (256 секторов = 128 КБ, LBA 65..320) в 0x2000:0x0000 (физ. 0x20000)
    ; Читаем 4 блоками по 64 сектора для совместимости с BIOS INT 13h
    mov cx, 4
    mov dword [dap_lba_low], 65
    mov word [dap_segment], 0x2000
.load_kernel_loop:
    push cx
    mov dword [dap_lba_high], 0
    mov word [dap_count], 64
    mov word [dap_offset], 0x0000
    call read_sectors
    add dword [dap_lba_low], 64
    add word [dap_segment], 0x0800
    pop cx
    loop .load_kernel_loop


    mov si, msg_pmode
    call puts16

    ; Переход в 32-битный Protected Mode
    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; Дальний прыжок в 32-битный код kboot по адресу 0x8000
    jmp 0x08:0x8000

; --- Вывод строки в Real Mode ---
puts16:
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp puts16
.done:
    ret

; --- Включение адресной линии A20 ---
enable_a20:
    ; Метод 1: BIOS INT 15h, AX=2401h
    mov ax, 0x2401
    int 0x15
    jnc .done

    ; Метод 2: Fast A20 gate через порт 0x92
    in al, 0x92
    or al, 2
    out 0x92, al
.done:
    ret

; --- Чтение секторов через INT 13h LBA ---
read_sectors:
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    ret

disk_error:
    mov si, msg_err
    call puts16
.hang:
    hlt
    jmp .hang

; --- Данные ---
boot_drive db 0
msg_mbr    db "[kboot] Stage 1 MBR active. Reading stage 2...", 13, 10, 0
msg_pmode  db "[kboot] Switching to 32-bit Protected Mode...", 13, 10, 0
msg_err    db "[kboot] ERROR: Disk read failed!", 13, 10, 0

align 4
dap:
    db 0x10
    db 0
dap_count:
    dw 0
dap_offset:
    dw 0
dap_segment:
    dw 0
dap_lba_low:
    dd 0
dap_lba_high:
    dd 0

; --- Global Descriptor Table (GDT) ---
align 8
gdt_start:
    ; Нулевой дескриптор
    dd 0, 0
    ; 32-bit Code Segment (0x08): Base=0, Limit=4GB, Exec/Read
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x9A
    db 0xCF
    db 0x00
    ; 32-bit Data Segment (0x10): Base=0, Limit=4GB, Read/Write
    dw 0xFFFF
    dw 0x0000
    db 0x00
    db 0x92
    db 0xCF
    db 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; Сигнатура MBR
times 510 - ($ - $$) db 0
dw 0xAA55

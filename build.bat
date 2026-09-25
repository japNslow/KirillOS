@echo off
setlocal enabledelayedexpansion
title KirillOS and kboot Builder
pushd "%~dp0"

REM ==== Auto-detect tools ====
set NASM=nasm
set QEMU=qemu-system-x86_64
set PYTHON=python
set AUDIO_ARGS=-audiodev driver=dsound,id=kirillos_audio,out.buffer-length=100000

if exist "C:\nasm\nasm.exe" set NASM="C:\nasm\nasm.exe"
if exist "C:\msys64\ucrt64\bin\qemu-system-x86_64.exe" set QEMU="C:\msys64\ucrt64\bin\qemu-system-x86_64.exe"
if exist "C:\Program Files\Python310\python.exe" set PYTHON="C:\Program Files\Python310\python.exe"

set GCC=
set LD=
for %%P in (
    "C:\i686\bin\i686-elf-gcc.exe"
    "C:\i686\i686-elf\bin\i686-elf-gcc.exe"
    "C:\tools\i686-elf\bin\i686-elf-gcc.exe"
    "%USERPROFILE%\i686-elf\bin\i686-elf-gcc.exe"
) do (
    if exist %%P (
        set "GCC=%%~P"
        set "LD=%%~dpPi686-elf-ld.exe"
    )
)

if "%GCC%"=="" (
    where i686-elf-gcc >nul 2>nul
    if !errorlevel!==0 (
        set GCC=i686-elf-gcc
        set LD=i686-elf-ld
    ) else (
        echo [ERROR] i686-elf-gcc not found in PATH or C:\i686\bin
        pause
        exit /b 1
    )
)

echo Using GCC:    %GCC%
echo Using LD:     %LD%
echo Using NASM:   %NASM%
echo Using Python: %PYTHON%
echo Using QEMU:   %QEMU%
echo.

set OUT=kernel.bin
set DISK_IMG=kirillos.img

echo [1/5] Building kboot bootloader (MBR + Stage 2)...
%NASM% -f bin kboot_mbr.asm -o kboot_mbr.bin
if errorlevel 1 goto :error

%NASM% -f elf32 kboot_entry.asm -o kboot_entry.o
if errorlevel 1 goto :error

"%GCC%" -m32 -c kboot.c -o kboot.o -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error

"%LD%" -m elf_i386 -T kboot_linker.ld --oformat binary -o kboot.bin kboot_entry.o kboot.o
if errorlevel 1 goto :error

echo [2/5] Assembling kernel boot.asm...
%NASM% -f elf32 boot.asm -o boot.o
if errorlevel 1 goto :error

echo [2.5/5] Building KHEX applications (.bin)...
"%GCC%" -m32 -c apps\guess.c  -o apps\guess.o  -ffreestanding -fno-pie -fno-stack-protector -Os
"%LD%" -m elf_i386 -T apps\app.ld --oformat binary -o apps\guess.bin apps\guess.o
"%GCC%" -m32 -c apps\matrix.c -o apps\matrix.o -ffreestanding -fno-pie -fno-stack-protector -Os
"%LD%" -m elf_i386 -T apps\app.ld --oformat binary -o apps\matrix.bin apps\matrix.o
"%GCC%" -m32 -c apps\snake.c  -o apps\snake.o  -ffreestanding -fno-pie -fno-stack-protector -Os
"%LD%" -m elf_i386 -T apps\app.ld --oformat binary -o apps\snake.bin apps\snake.o
%PYTHON% -c "for n in ['guess','matrix','snake']: data=open(f'apps/{n}.bin','rb').read(); open(f'apps/{n}_data.h','w').write(f'static const uint8_t {n.upper()}_BIN[{len(data)}] = {{\n' + ', '.join(f'0x{b:02X}' for b in data) + '\n};\n')"

echo [3/5] Compiling KirillOS kernel C files...
"%GCC%" -m32 -c kernel.c   -o kernel.o   -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c vga.c      -o vga.o      -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c keyboard.c -o keyboard.o -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c kirillfs.c -o kirillfs.o -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c kmemory.c -o kmemory.o -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c ata.c      -o ata.o      -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c sound.c    -o sound.o    -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c ac97.c     -o ac97.o     -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c kano.c     -o kano.o     -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c kdhe.c     -o kdhe.o     -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c kmidi.c    -o kmidi.o    -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c khex.c     -o khex.o     -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c khexd.c    -o khexd.o    -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c kfetch.c   -o kfetch.o   -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error
"%GCC%" -m32 -c mode13.c   -o mode13.o   -ffreestanding -fno-pie -fno-stack-protector -Wall -Wextra -O2
if errorlevel 1 goto :error

echo [4/5] Linking KirillOS kernel...
"%LD%" -m elf_i386 -T linker.ld -o %OUT% boot.o kernel.o keyboard.o vga.o kirillfs.o kmemory.o ata.o sound.o ac97.o kano.o kdhe.o kmidi.o khex.o khexd.o kfetch.o mode13.o

if errorlevel 1 goto :error

echo [5/5] Assembling bootable disk image %DISK_IMG%...
%PYTHON% create_image.py
if errorlevel 1 goto :error

echo.
echo [DONE] Build successful!
echo Starting QEMU with native kboot...
echo.

%QEMU% -drive file=%DISK_IMG%,format=raw,if=ide %AUDIO_ARGS% -machine pcspk-audiodev=kirillos_audio -device AC97,audiodev=kirillos_audio -m 128M
goto :eof

:error
echo.
echo *** BUILD FAILED ***
pause
exit /b 1
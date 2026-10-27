#!/usr/bin/env bash
# KirillOS and kboot builder for Linux (the Linux counterpart of build.bat).
#
#   ./build.sh            build everything and start QEMU
#   ./build.sh --no-run   build only
#
# Tools: nasm, python3, qemu-system-x86_64 and either an i686-elf cross
# toolchain or the host gcc/ld (x86 Linux with -m32 / -m elf_i386).
# Overrides: CC=..., LD=..., NASM=..., PYTHON=..., QEMU=..., AUDIO_DRIVER=...

set -u
cd "$(dirname "$0")"

RUN=1
for arg in "$@"; do
    case "$arg" in
        --no-run) RUN=0 ;;
        -h|--help) sed -n '2,9p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "[ERROR] Unknown option: $arg"; exit 1 ;;
    esac
done

fail() {
    echo
    echo "*** BUILD FAILED ***"
    exit 1
}

need() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[ERROR] $1 not found. $2"
        exit 1
    fi
}

# ==== Auto-detect tools ====
NASM=${NASM:-nasm}
PYTHON=${PYTHON:-python3}
QEMU=${QEMU:-qemu-system-x86_64}

# Prefer the i686-elf cross compiler (same as build.bat); fall back to the host toolchain
EXTRA_CFLAGS=()
if [ -z "${CC:-}" ]; then
    for p in i686-elf-gcc "$HOME/opt/cross/bin/i686-elf-gcc" "$HOME/i686-elf/bin/i686-elf-gcc" /usr/local/i686-elf/bin/i686-elf-gcc; do
        if command -v "$p" >/dev/null 2>&1; then
            CC=$(command -v "$p")
            LD=${LD:-${CC%gcc}ld}
            break
        fi
    done
fi
if [ -z "${CC:-}" ]; then
    CC=gcc
    LD=${LD:-ld}
    # Distribution gcc targets x86-64 with SSE2 even under -m32, but neither kboot
    # nor the kernel enables SSE, so match i686-elf-gcc's default -march=i686.
    # It also emits PIE code, unwind tables and CET / ISA-level notes by default;
    # none of that belongs in a flat binary
    EXTRA_CFLAGS=(-march=i686 -fno-pic -fno-asynchronous-unwind-tables -fno-unwind-tables -fcf-protection=none -Wa,-mx86-used-note=no)
fi
LD=${LD:-ld}

need "$NASM" "Install it: sudo pacman -S nasm  /  sudo apt install nasm"
need "$CC" "Install gcc or an i686-elf cross compiler"
need "$LD" "Install binutils"
need "$PYTHON" "Install python3"

# NASM objects carry no .note.GNU-stack and the kernel image is one RWX segment:
# silence those warnings when the linker knows the flags
LDFLAGS=(-m elf_i386 -z noexecstack)
if "$LD" --help 2>/dev/null | grep -q -- --no-warn-rwx-segments; then
    LDFLAGS+=(--no-warn-rwx-segments)
fi

CFLAGS=(-m32 -ffreestanding -fno-pie -fno-stack-protector "${EXTRA_CFLAGS[@]}")

echo "Using GCC:    $CC"
echo "Using LD:     $LD"
echo "Using NASM:   $NASM"
echo "Using Python: $PYTHON"
echo "Using QEMU:   $QEMU"
echo

OUT=kernel.bin
DISK_IMG=kirillos.img

echo "[1/5] Building kboot bootloader (MBR + Stage 2)..."
"$NASM" -f bin kboot_mbr.asm -o kboot_mbr.bin || fail
"$NASM" -f elf32 kboot_entry.asm -o kboot_entry.o || fail
"$CC" "${CFLAGS[@]}" -c kboot.c -o kboot.o -Wall -Wextra -O2 || fail
"$LD" "${LDFLAGS[@]}" -T kboot_linker.ld --oformat binary -o kboot.bin kboot_entry.o kboot.o || fail

echo "[2/5] Assembling kernel boot.asm..."
"$NASM" -f elf32 boot.asm -o boot.o || fail

echo "[2.5/5] Building KHEX applications (.bin)..."
for app in guess matrix snake; do
    "$CC" "${CFLAGS[@]}" -c "apps/$app.c" -o "apps/$app.o" -Os || fail
    "$LD" "${LDFLAGS[@]}" -T apps/app.ld --oformat binary -o "apps/$app.bin" "apps/$app.o" || fail
done
"$PYTHON" -c "for n in ['guess','matrix','snake']: data=open(f'apps/{n}.bin','rb').read(); open(f'apps/{n}_data.h','w').write(f'static const uint8_t {n.upper()}_BIN[{len(data)}] = {{\n' + ', '.join(f'0x{b:02X}' for b in data) + '\n};\n')" || fail

echo "[3/5] Compiling KirillOS kernel C files..."
for src in kernel vga keyboard kirillfs kmemory ata sound ac97 kano kdhe kmidi khex khexd kfetch mode13 mouse; do
    "$CC" "${CFLAGS[@]}" -c "$src.c" -o "$src.o" -Wall -Wextra -O2 || fail
done
for src in kcommander kaint kguiq kguid kgui e1000 net; do
    "$CC" "${CFLAGS[@]}" -c "$src.c" -o "$src.o" -Wall -Wextra -Os || fail
done

echo "[4/5] Linking KirillOS kernel..."
# -s drops symbol tables: kboot only needs program headers, and the kernel must fit in 128 KB
"$LD" "${LDFLAGS[@]}" -s -T linker.ld -o "$OUT" boot.o kernel.o keyboard.o vga.o kirillfs.o kmemory.o ata.o sound.o ac97.o kano.o kdhe.o kmidi.o khex.o khexd.o kfetch.o mode13.o mouse.o kcommander.o kaint.o kguiq.o kguid.o kgui.o e1000.o net.o || fail

echo "[5/5] Assembling bootable disk image $DISK_IMG..."
"$PYTHON" create_image.py || fail

echo
echo "[DONE] Build successful!"
[ "$RUN" = 1 ] || exit 0

need "$QEMU" "Install it: sudo pacman -S qemu-system-x86 qemu-audio-pipewire  /  sudo apt install qemu-system-x86"

# Pick the first sound backend this QEMU build has: PipeWire, PulseAudio, ALSA, or silence
if [ -z "${AUDIO_DRIVER:-}" ]; then
    drivers=$("$QEMU" -audiodev help 2>/dev/null)
    AUDIO_DRIVER=none
    for d in pipewire pa alsa; do
        if grep -qx "$d" <<<"$drivers"; then AUDIO_DRIVER=$d; break; fi
    done
fi
AUDIO_ARGS=(-audiodev "driver=$AUDIO_DRIVER,id=kirillos_audio,out.buffer-length=100000")

echo "Starting QEMU with native kboot (audio: $AUDIO_DRIVER)..."
echo
exec "$QEMU" -drive "file=$DISK_IMG,format=raw,if=ide" "${AUDIO_ARGS[@]}" -machine pcspk-audiodev=kirillos_audio -device AC97,audiodev=kirillos_audio -nic user,model=e1000 -m 128M

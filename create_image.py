import sys
import os

IMG_FILE = "kirillos.img"
IMG_SIZE = 64 * 1024 * 1024  # 64 MiB

MBR_FILE = "kboot_mbr.bin"
KBOOT_FILE = "kboot.bin"
KERNEL_FILE = "kernel.bin"

SECTOR_SIZE = 512
MBR_OFFSET = 0
KBOOT_OFFSET = 1 * SECTOR_SIZE       # LBA 1
KERNEL_OFFSET = 65 * SECTOR_SIZE     # LBA 65

def main():
    # Создаём 64 МБ образ, если его ещё нет
    if not os.path.exists(IMG_FILE):
        print(f"[*] Creating new {IMG_SIZE // (1024*1024)}MB disk image: {IMG_FILE}")
        with open(IMG_FILE, "wb") as f:
            f.truncate(IMG_SIZE)

    with open(IMG_FILE, "r+b") as img:
        # 1. Запись MBR
        with open(MBR_FILE, "rb") as f:
            mbr_data = f.read()
            if len(mbr_data) != 512:
                print(f"[ERROR] MBR size is {len(mbr_data)}, expected 512!")
                sys.exit(1)
            img.seek(MBR_OFFSET)
            img.write(mbr_data)
            print(f"[OK] Wrote MBR (512 bytes) to LBA 0")

        # 2. Запись Stage 2 (kboot.bin)
        with open(KBOOT_FILE, "rb") as f:
            kboot_data = f.read()
            max_kboot = 64 * SECTOR_SIZE  # 32 КБ
            if len(kboot_data) > max_kboot:
                print(f"[ERROR] kboot.bin is {len(kboot_data)} bytes, exceeds {max_kboot} limit!")
                sys.exit(1)
            img.seek(KBOOT_OFFSET)
            img.write(kboot_data)
            # Дополняем нулями до границы LBA 65
            pad = max_kboot - len(kboot_data)
            if pad > 0:
                img.write(b"\x00" * pad)
            print(f"[OK] Wrote kboot ({len(kboot_data)} bytes) to LBA 1..64")

        # 3. Запись ядра (kernel.bin)
        with open(KERNEL_FILE, "rb") as f:
            kernel_data = f.read()
            max_kernel = (321 - 65) * SECTOR_SIZE  # 256 секторов = 131072 байта (128 КБ)
            if len(kernel_data) > max_kernel:
                print(f"[ERROR] kernel.bin is {len(kernel_data)} bytes, exceeds {max_kernel} limit!")
                sys.exit(1)
            img.seek(KERNEL_OFFSET)
            img.write(kernel_data)
            pad_kernel = max_kernel - len(kernel_data)
            if pad_kernel > 0:
                img.write(b"\x00" * pad_kernel)
            print(f"[OK] Wrote kernel ({len(kernel_data)} bytes) to LBA 65..320")


    print("[SUCCESS] Disk image 'kirillos.img' assembled.")

if __name__ == "__main__":
    main()

#!/usr/bin/python3
"""
Crea un boot.img Android (header v0) per il bootloader del Galaxy Note 10.1 (GT-N8000).
Indirizzi e pagina sono quelli delle partizioni BOOT e RECOVERY originali.

  mkbootimg.py --kernel zImage --ramdisk initramfs.cpio.gz --cmdline "..." -o boot.img
"""
import argparse
import hashlib
import struct

BASE = 0x40000000
KERNEL_ADDR = BASE + 0x00008000
RAMDISK_ADDR = BASE + 0x01000000
TAGS_ADDR = BASE + 0x00000100
PAGE = 2048
PARTITION_SIZE = 8 * 1024 * 1024  # BOOT e RECOVERY sono da 8 MB


def pad(data):
    return data + b"\0" * (-len(data) % PAGE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", required=True, help="zImage del kernel")
    ap.add_argument("--ramdisk", required=True)
    ap.add_argument("--cmdline", default="")
    ap.add_argument("--os-version", type=int, default=0, help="campo os_version dell'header")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()

    kernel = open(a.kernel, "rb").read()
    ramdisk = open(a.ramdisk, "rb").read()
    cmdline = a.cmdline.encode()
    if len(cmdline) > 511:
        raise SystemExit("cmdline troppo lunga (max 511 byte)")

    sha = hashlib.sha1()
    for blob in (kernel, ramdisk, b""):
        sha.update(blob)
        sha.update(struct.pack("<I", len(blob)))
    header = struct.pack("<8s10I16s512s32s",
                         b"ANDROID!", len(kernel), KERNEL_ADDR, len(ramdisk), RAMDISK_ADDR,
                         0, BASE + 0x00F00000, TAGS_ADDR, PAGE, 0, a.os_version,
                         b"", cmdline, sha.digest())
    img = pad(header) + pad(kernel) + pad(ramdisk)
    if len(img) > PARTITION_SIZE - PAGE:  # l'ultimo blocco di BOOT conserva la scelta del menu
        raise SystemExit(f"immagine di {len(img)} byte: non sta nella partizione da 8 MB")
    open(a.output, "wb").write(img)
    print(f"{a.output}: {len(img)} byte (kernel {len(kernel)}, ramdisk {len(ramdisk)})")


if __name__ == "__main__":
    main()

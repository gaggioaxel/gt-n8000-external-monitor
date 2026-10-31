#!/usr/bin/python3
"""
Creates an Android boot.img (header v0) for the Galaxy Note 10.1 (GT-N8000) bootloader.
Addresses and page size are those of the original BOOT and RECOVERY partitions.

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
PARTITION_SIZE = 8 * 1024 * 1024  # BOOT and RECOVERY are 8 MB


def pad(data):
    return data + b"\0" * (-len(data) % PAGE)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kernel", required=True, help="kernel zImage")
    ap.add_argument("--ramdisk", required=True)
    ap.add_argument("--cmdline", default="")
    ap.add_argument("--os-version", type=int, default=0, help="os_version header field")
    ap.add_argument("-o", "--output", required=True)
    a = ap.parse_args()

    kernel = open(a.kernel, "rb").read()
    ramdisk = open(a.ramdisk, "rb").read()
    cmdline = a.cmdline.encode()
    if len(cmdline) > 511:
        raise SystemExit("cmdline too long (max 511 bytes)")

    sha = hashlib.sha1()
    for blob in (kernel, ramdisk, b""):
        sha.update(blob)
        sha.update(struct.pack("<I", len(blob)))
    header = struct.pack("<8s10I16s512s32s",
                         b"ANDROID!", len(kernel), KERNEL_ADDR, len(ramdisk), RAMDISK_ADDR,
                         0, BASE + 0x00F00000, TAGS_ADDR, PAGE, 0, a.os_version,
                         b"", cmdline, sha.digest())
    img = pad(header) + pad(kernel) + pad(ramdisk)
    if len(img) > PARTITION_SIZE - PAGE:  # the last BOOT block keeps the menu choice
        raise SystemExit(f"{len(img)}-byte image does not fit the 8 MB partition")
    open(a.output, "wb").write(img)
    print(f"{a.output}: {len(img)} bytes (kernel {len(kernel)}, ramdisk {len(ramdisk)})")


if __name__ == "__main__":
    main()

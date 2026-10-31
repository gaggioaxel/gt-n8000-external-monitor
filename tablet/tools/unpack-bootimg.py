#!/usr/bin/python3
"""
Extracts kernel, ramdisk and os_version from an Android boot.img (header v0).

  unpack-bootimg.py BOOT.img vendor/
"""
import os
import struct
import sys


def main():
    img, out = sys.argv[1], sys.argv[2]
    data = open(img, "rb").read()
    if data[:8] != b"ANDROID!":
        raise SystemExit(f"{img}: not a boot.img")
    fields = struct.unpack("<10I", data[8:48])
    kernel_size, ramdisk_size, page, os_version = fields[0], fields[2], fields[7], fields[9]

    def pages(n):
        return (n + page - 1) // page * page

    kernel = data[page:page + kernel_size]
    start = page + pages(kernel_size)
    ramdisk = data[start:start + ramdisk_size]
    os.makedirs(out, exist_ok=True)
    open(os.path.join(out, "zImage"), "wb").write(kernel)
    open(os.path.join(out, "ramdisk.img"), "wb").write(ramdisk)
    open(os.path.join(out, "os_version"), "w").write(f"{os_version}\n")
    print(f"kernel {kernel_size} bytes, ramdisk {ramdisk_size} bytes, os_version {os_version}")


if __name__ == "__main__":
    main()

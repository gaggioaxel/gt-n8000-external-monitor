#!/bin/sh
# Prepares vendor/ with the files that are not in the repository: kernel and ramdisk from the
# original BOOT, MFC firmware from the Android system. Needs backup/BOOT.img created by
# tools/backup.sh while Android is still intact.
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
[ -f backup/BOOT.img ] || { echo "backup/BOOT.img is missing: run tools/backup.sh first"; exit 1; }
python3 tools/unpack-bootimg.py backup/BOOT.img vendor
adb pull /system/vendor/firmware/mfc_fw.bin vendor/mfc_fw.bin
echo "Only vendor/busybox is left: see docs/development.md"

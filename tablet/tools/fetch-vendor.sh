#!/bin/sh
# Prepara vendor/ con i file che non stanno nella repo: kernel e ramdisk dal BOOT originale,
# firmware dell'MFC dal sistema Android. Richiede backup/BOOT.img creato da tools/backup.sh,
# con Android ancora intatto.
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
[ -f backup/BOOT.img ] || { echo "manca backup/BOOT.img: esegui prima tools/backup.sh"; exit 1; }
python3 tools/unpack-bootimg.py backup/BOOT.img vendor
adb pull /system/vendor/firmware/mfc_fw.bin vendor/mfc_fw.bin
echo "Manca solo vendor/busybox: vedi docs/sviluppo.md"

#!/bin/sh
# Writes build/boot.img to the BOOT partition, from Android with root, and reads it back
# before rebooting. It writes only the image: the last block, with the menu choice, is kept.
# If verification fails it restores the original BOOT from backup/BOOT.img and does not reboot.
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
IMG=build/boot.img
ORIG=backup/BOOT.img
[ -f "$IMG" ] || { echo "$IMG is missing: run make image"; exit 1; }
[ -f "$ORIG" ] || { echo "$ORIG is missing: run tools/backup.sh"; exit 1; }
SIZE=$(stat -c %s "$IMG")
[ $((SIZE % 2048)) -eq 0 ] || { echo "size not aligned to 2048 bytes"; exit 1; }
LOCAL=$(sha1sum "$IMG" | cut -d' ' -f1)

shell() { adb shell "su -c '$1'" | tr -d '\r'; }

BOOT=$(shell 'readlink /dev/block/platform/dw_mmc/by-name/BOOT')
[ "$BOOT" = /dev/block/mmcblk0p5 ] || { echo "unexpected BOOT partition: $BOOT"; exit 1; }

adb push "$IMG" /data/local/tmp/nd.img > /dev/null
adb push "$ORIG" /data/local/tmp/boot-orig.img > /dev/null
[ "$(shell 'sha1sum /data/local/tmp/nd.img' | cut -d' ' -f1)" = "$LOCAL" ] || { echo "corrupted transfer"; exit 1; }

# Android's dd rejects conv=fsync: with that option it would write nothing
shell "dd if=/data/local/tmp/nd.img of=$BOOT bs=2048; sync"
BACK=$(shell "dd if=$BOOT bs=2048 count=$((SIZE / 2048)) 2>/dev/null | sha1sum" | cut -d' ' -f1)
if [ "$BACK" != "$LOCAL" ]; then
	echo "verification failed: restoring the original BOOT"
	shell "dd if=/data/local/tmp/boot-orig.img of=$BOOT bs=2048; sync"
	exit 1
fi
shell 'rm /data/local/tmp/nd.img /data/local/tmp/boot-orig.img'
echo "BOOT written and verified: $LOCAL"
adb reboot

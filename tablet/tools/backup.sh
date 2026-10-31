#!/bin/sh
# Copies the partitions the tablet's restore depends on, verifying the checksums.
# Run it from Android with root, BEFORE any write. The backup/ folder holds data unique to the
# device (EFS: IMEI and radio calibration): never publish it.
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
mkdir -p backup
for p in EFS PARAM BOOT RECOVERY RADIO BOTA0 BOTA1; do
	dev=$(adb shell "su -c 'readlink /dev/block/platform/dw_mmc/by-name/$p'" | tr -d '\r')
	[ -n "$dev" ] || { echo "partition $p not found"; exit 1; }
	adb shell "su -c 'dd if=$dev of=/data/local/tmp/$p.img bs=4096 && chmod 644 /data/local/tmp/$p.img'" 2>/dev/null
	adb pull "/data/local/tmp/$p.img" "backup/$p.img" > /dev/null
	remote=$(adb shell "su -c 'sha1sum $dev'" | cut -d' ' -f1)
	local=$(sha1sum "backup/$p.img" | cut -d' ' -f1)
	adb shell "su -c 'rm /data/local/tmp/$p.img'"
	[ "$remote" = "$local" ] || { echo "$p: checksum mismatch"; exit 1; }
	echo "$p ok ($dev)"
done

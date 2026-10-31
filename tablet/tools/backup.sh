#!/bin/sh
# Copia le partizioni da cui dipende il ripristino del tablet, con verifica dei checksum.
# Va eseguito da Android con root, PRIMA di qualsiasi scrittura. La cartella backup/ contiene
# dati unici del dispositivo (EFS: IMEI e calibrazione radio): non va mai pubblicata.
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
mkdir -p backup
for p in EFS PARAM BOOT RECOVERY RADIO BOTA0 BOTA1; do
	dev=$(adb shell "su -c 'readlink /dev/block/platform/dw_mmc/by-name/$p'" | tr -d '\r')
	[ -n "$dev" ] || { echo "partizione $p non trovata"; exit 1; }
	adb shell "su -c 'dd if=$dev of=/data/local/tmp/$p.img bs=4096 && chmod 644 /data/local/tmp/$p.img'" 2>/dev/null
	adb pull "/data/local/tmp/$p.img" "backup/$p.img" > /dev/null
	remote=$(adb shell "su -c 'sha1sum $dev'" | cut -d' ' -f1)
	local=$(sha1sum "backup/$p.img" | cut -d' ' -f1)
	adb shell "su -c 'rm /data/local/tmp/$p.img'"
	[ "$remote" = "$local" ] || { echo "$p: checksum diverso"; exit 1; }
	echo "$p ok ($dev)"
done

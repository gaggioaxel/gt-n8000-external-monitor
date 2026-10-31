#!/bin/sh
# Scrive build/boot.img nella partizione BOOT, da Android con root, e la rilegge prima di
# riavviare. Scrive solo l'immagine: l'ultimo blocco, con la scelta del menu, resta intatto.
# Se la verifica non torna rimette il BOOT originale da backup/BOOT.img e non riavvia.
set -e
cd "$(dirname "$(readlink -f "$0")")/.."
IMG=build/boot.img
ORIG=backup/BOOT.img
[ -f "$IMG" ] || { echo "manca $IMG: esegui make image"; exit 1; }
[ -f "$ORIG" ] || { echo "manca $ORIG: esegui tools/backup.sh"; exit 1; }
SIZE=$(stat -c %s "$IMG")
[ $((SIZE % 2048)) -eq 0 ] || { echo "dimensione non allineata a 2048 byte"; exit 1; }
LOCAL=$(sha1sum "$IMG" | cut -d' ' -f1)

shell() { adb shell "su -c '$1'" | tr -d '\r'; }

BOOT=$(shell 'readlink /dev/block/platform/dw_mmc/by-name/BOOT')
[ "$BOOT" = /dev/block/mmcblk0p5 ] || { echo "partizione BOOT inattesa: $BOOT"; exit 1; }

adb push "$IMG" /data/local/tmp/nd.img > /dev/null
adb push "$ORIG" /data/local/tmp/boot-orig.img > /dev/null
[ "$(shell 'sha1sum /data/local/tmp/nd.img' | cut -d' ' -f1)" = "$LOCAL" ] || { echo "trasferimento corrotto"; exit 1; }

# Il dd di Android non accetta conv=fsync: con quell'opzione non scriverebbe nulla
shell "dd if=/data/local/tmp/nd.img of=$BOOT bs=2048; sync"
BACK=$(shell "dd if=$BOOT bs=2048 count=$((SIZE / 2048)) 2>/dev/null | sha1sum" | cut -d' ' -f1)
if [ "$BACK" != "$LOCAL" ]; then
	echo "verifica fallita: ripristino il BOOT originale"
	shell "dd if=/data/local/tmp/boot-orig.img of=$BOOT bs=2048; sync"
	exit 1
fi
shell 'rm /data/local/tmp/nd.img /data/local/tmp/boot-orig.img'
echo "BOOT scritto e verificato: $LOCAL"
adb reboot

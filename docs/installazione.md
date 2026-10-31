# Installazione

Servono un GT-N8000 con ROM LineageOS/RR 7.1 e root, e un PC Ubuntu con GNOME su Wayland. La procedura richiede circa mezz'ora, soprattutto per compilare busybox.

## 1. Pacchetti sul PC

```bash
sudo apt install adb heimdall-flash gcc-arm-linux-gnueabihf make cpio python3-gi gstreamer1.0-pipewire gstreamer1.0-plugins-ugly gstreamer1.0-plugins-bad gstreamer1.0-tools libusb-1.0-0
```

`gstreamer1.0-plugins-ugly` contiene `x264enc`. Per la codifica hardware Intel serve anche `gstreamer1.0-vaapi`.

## 2. Backup

Con il tablet in Android, debug USB attivo e root concesso alla shell:

```bash
cd tablet
tools/backup.sh
```

Crea `tablet/backup/` con EFS, PARAM, BOOT, RECOVERY, RADIO e BOTA, confrontando i checksum con il tablet. **Copia questa cartella in un posto sicuro.** EFS è unica per ogni dispositivo e contiene IMEI e calibrazione radio: non pubblicarla mai.

## 3. File del tablet

La repo non contiene i file proprietari. Con il BOOT ancora originale:

```bash
tools/fetch-vendor.sh
```

Crea `tablet/vendor/` con kernel, ramdisk Android e firmware dell'MFC. Manca `vendor/busybox`: la [guida di sviluppo](sviluppo.md#busybox) spiega come compilarlo.

## 4. Costruire e scrivere l'immagine

```bash
make image
tools/flash.sh
```

`flash.sh` rilegge la partizione prima di riavviare. Se il controllo fallisce rimette il BOOT originale e si ferma.

## 5. Host sul PC

```bash
cd ../host
./install.sh
```

Installa `~/.local/bin/GalaxyNoteDisplay`, il servizio utente e la regola udev, per cui chiede `sudo`.

## 6. Prova

Riavvia il tablet e lascia passare i 5 secondi del menu. Collega il cavo: su GNOME compare il monitor, e puoi posizionarlo in Impostazioni → Schermi.

Se qualcosa non va, vedi [Risoluzione dei problemi](problemi.md).

## Tornare ad Android per sempre

Rimetti il BOOT originale. Dal tablet in Android, scegliendolo nel menu:

```bash
adb push backup/BOOT.img /data/local/tmp/BOOT.img
adb shell "su -c 'dd if=/data/local/tmp/BOOT.img of=/dev/block/mmcblk0p5 bs=2048; sync'"
```

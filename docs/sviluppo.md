# Sviluppo

## Compilare

```bash
cd tablet
make        # solo i programmi, non serve vendor/
make image  # build/boot.img, serve vendor/
```

Il compilatore è `arm-linux-gnueabihf-gcc` con link statico. Il binario gira sul kernel 3.0 nonostante la glibc recente.

## busybox

Scarica i sorgenti di busybox 1.36.1 da [busybox.net](https://busybox.net/downloads/), poi:

```bash
cp tablet/busybox.config busybox-1.36.1/.config
```

```bash
make -C busybox-1.36.1 CROSS_COMPILE=arm-linux-gnueabihf- -j8
```

```bash
cp busybox-1.36.1/busybox tablet/vendor/busybox
```

## Ciclo di prova

Il tablet in Display esterno risponde in telnet su `172.16.42.1`, quindi si può provare senza passare da Android. Per lasciare intatto BOOT:

1. Trasferisci l'immagine con `nc` in `/tmp`.
2. Controlla lo sha1.
3. Scrivila in RECOVERY (`/dev/mmcblk0p6`) con `dd`, e rileggila.
4. Riavvia in recovery.
5. A fine lavoro scrivi BOOT e rimetti TWRP dal backup.

Si distingue da quale partizione si è partiti da `bootmode=2` in `/proc/cmdline`.

Per vedere cosa c'è sul pannello, senza guardarlo: scarica `/dev/fb0` con `nc` e convertilo in immagine. Con la rotazione predefinita risulta capovolto.

## Trappole del kernel Samsung 3.0

- **`VIDIOC_G_FMT` prima di tutto:** è lì che il driver FIMC alloca `ctrl->out`. Senza, `VIDIOC_G_FBUF` dà un NULL pointer nel kernel.
- **`struct v4l2_buffer`:** il compilatore predefinisce `_TIME_BITS=64`, ma il kernel 3.0 la vuole da 68 byte. Va ridefinita, come fa `display.c`, altrimenti cambia il numero dell'ioctl.
- **`V4L2_CID_ROTATION` vale `0x08000000`:** è un controllo privato Samsung, ricavato dal disassemblato del hwcomposer.
- **`reboot -f` torna sempre a BOOT:** ignora il flag in PARAM. Per la recovery serve il riavvio con argomento `recovery`, cioè `bootmenu recovery`.
- **Non uccidere `notedisplayd`:** a caldo il tablet si riavvia. La causa non è stata indagata.
- **`oops=panic`:** ogni oops riavvia il tablet. Il log resta in `/proc/last_kmsg`, leggibile da Android.
- **Firmware MFC:** il driver lo chiede prima che parta `/init`, quindi `/init` lo carica a mano da `/sys/class/firmware`.
- **`dd` di Android:** rifiuta `conv=fsync` e non scrive nulla.
- **`/init` su Android:** `/etc` di Android è un link e il nostro una cartella, quindi prima di estrarre il ramdisk si sposta tutto, compreso busybox.

## Host

```bash
cd host
./build.sh
./GalaxyNoteDisplay --stats
```

`--test` esegue cattura e codifica senza tablet.

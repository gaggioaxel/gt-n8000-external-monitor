# Installation

The procedure takes about half an hour, mostly to build busybox. Before you start, check that you have:

- A **rooted** GT-N8000 with an Android 7.1 ROM. Without root nothing here works: the backup and the flashing both use `su`.
- **TWRP as recovery**, the only one tested. The `Recovery (TWRP)` menu entry reboots into the RECOVERY partition, but it does not adapt to other recoveries.
- **An Ubuntu PC with GNOME on Wayland**, the only setup that works out of the box. See the [requirements](../README.md#requirements).

## 1. Packages on the PC

```bash
sudo apt install adb heimdall-flash gcc-arm-linux-gnueabihf make cpio python3-gi gstreamer1.0-pipewire gstreamer1.0-plugins-ugly gstreamer1.0-plugins-bad gstreamer1.0-tools libusb-1.0-0
```

`gstreamer1.0-plugins-ugly` provides `x264enc`. Intel hardware encoding also needs `gstreamer1.0-vaapi`.

## 2. Backup

With the tablet in Android, USB debugging on and root granted to the shell:

```bash
cd tablet
tools/backup.sh
```

This creates `tablet/backup/` with EFS, PARAM, BOOT, RECOVERY, RADIO and BOTA, comparing checksums with the tablet. **Keep a copy of this folder somewhere safe.** EFS is unique to each device and holds the IMEI and radio calibration: never publish it.

## 3. Tablet files

The repository does not contain the proprietary files. With the original BOOT still in place:

```bash
tools/fetch-vendor.sh
```

This creates `tablet/vendor/` with the kernel, the Android ramdisk and the MFC firmware. `vendor/busybox` is still missing: the [development guide](development.md#busybox) explains how to build it.

## 4. Build and flash the image

```bash
make image
tools/flash.sh
```

`flash.sh` reads the partition back before rebooting. If the check fails it restores the original BOOT and stops.

## 5. PC host

```bash
cd ../host
./install.sh
```

This installs `~/.local/bin/GalaxyNoteDisplay`, the user service and the udev rule, which asks for `sudo`. The rule gives USB access without `sudo` and starts the service when the tablet appears.

## 6. Try it

Reboot the tablet and let the 5-second menu pass. Plug in the cable: the monitor appears in GNOME, and you can position it in Settings → Displays.

If something goes wrong, see [Troubleshooting](troubleshooting.md).

## Going back to Android for good

Restore the original BOOT. With the tablet in Android, chosen from the menu:

```bash
adb push backup/BOOT.img /data/local/tmp/BOOT.img
adb shell "su -c 'dd if=/data/local/tmp/BOOT.img of=/dev/block/mmcblk0p5 bs=2048; sync'"
```

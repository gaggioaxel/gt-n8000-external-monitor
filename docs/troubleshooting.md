# Troubleshooting

## The monitor does not appear on the PC

Check that the tablet is seen and that the service started:

```bash
lsusb | grep 18d1:2d00
```

```bash
systemctl --user status galaxynotedisplay.service
```

```bash
journalctl --user -u galaxynotedisplay.service -n 20
```

If `lsusb` does not show it, the tablet is not in External display mode. If it does but the service did not start, unplug and replug the cable: the udev rule fires only when the device appears.

If it still does not start, or the log shows a USB permission error, the udev rule may be missing. `host/install.sh` installs it; to set it by hand from the `host/` folder:

```bash
sudo cp 70-galaxynotedisplay.rules /etc/udev/rules.d/ && sudo udevadm control --reload
```

Then unplug and replug the cable.

## The mouse stutters or the image drags

- **Stutter:** Mutter delivers too few frames when its limit equals its 60 Hz. The host asks it for 120 fps and applies the 60 fps limit itself: make sure you are using the host from this repository.
- **Dragging:** frames are piling up. The `queue` value in `/tmp/nd.log` on the tablet must stay at 1. Lower FPS limit from the panel.

## Upside-down image

Choose the orientation from the panel. Also check that the orientation in GNOME is Landscape.

## The tablet reboots by itself

If `notedisplayd` dies, the tablet restarts. From Android, the crash log is in `/proc/last_kmsg`:

```bash
adb shell "su -c 'cat /proc/last_kmsg'" > last_kmsg.txt
```

## Seeing what happens on the tablet

In External display mode the tablet has a USB network: the PC gets `172.16.42.2`, the tablet is `172.16.42.1` with a telnet shell and no password. It can be reached only over the cable.

```bash
telnet 172.16.42.1
```

The log is in `/tmp/nd.log`.

## The tablet no longer boots

1. Put the tablet in Download mode: from power-off, hold Power and Volume down, then confirm with Volume up.
2. Write the original BOOT from the backup:

```bash
heimdall flash --BOOT tablet/backup/BOOT.img
```

Only one `heimdall` command works per Download mode session.

If the tablet stays on and frozen, hold Power until it restarts.

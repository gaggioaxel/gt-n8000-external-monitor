# Development

## Building

```bash
cd tablet
make        # programs only, vendor/ not needed
make image  # build/boot.img, needs vendor/
```

The compiler is `arm-linux-gnueabihf-gcc` with static linking. The binary runs on kernel 3.0 despite the recent glibc.

## busybox

Download the busybox 1.36.1 sources from [busybox.net](https://busybox.net/downloads/), then:

```bash
cp tablet/busybox.config busybox-1.36.1/.config
```

```bash
make -C busybox-1.36.1 CROSS_COMPILE=arm-linux-gnueabihf- -j8
```

```bash
cp busybox-1.36.1/busybox tablet/vendor/busybox
```

## Test cycle

In External display mode the tablet answers telnet on `172.16.42.1`, so you can test without going through Android. To leave BOOT intact:

1. Transfer the image with `nc` to `/tmp`.
2. Check its sha1.
3. Write it to RECOVERY (`/dev/mmcblk0p6`) with `dd`, and read it back.
4. Reboot into recovery.
5. When done, write BOOT and restore TWRP from the backup.

`bootmode=2` in `/proc/cmdline` tells which partition you started from.

To see what is on the panel without looking at it, fetch `/dev/fb0` with `nc` and convert it to an image. With the default rotation it comes out upside down.

## Samsung 3.0 kernel pitfalls

- **`VIDIOC_G_FMT` first:** that is where the FIMC driver allocates `ctrl->out`. Without it, `VIDIOC_G_FBUF` hits a NULL pointer in the kernel.
- **`struct v4l2_buffer`:** the compiler predefines `_TIME_BITS=64`, but kernel 3.0 wants it at 68 bytes. It must be redefined, as `display.c` does, or the ioctl number changes.
- **`V4L2_CID_ROTATION` is `0x08000000`:** a private Samsung control, taken from the hwcomposer disassembly.
- **`reboot -f` always returns to BOOT:** it ignores the flag in PARAM. Recovery needs a reboot with the `recovery` argument, that is `bootmenu recovery`.
- **Do not kill `notedisplayd`:** doing it live reboots the tablet. The cause was not investigated.
- **`oops=panic`:** any oops reboots the tablet. The log stays in `/proc/last_kmsg`, readable from Android.
- **MFC firmware:** the driver requests it before `/init` runs, so `/init` loads it by hand from `/sys/class/firmware`.
- **Android's `dd`:** it rejects `conv=fsync` and writes nothing.
- **`/init` on Android:** Android's `/etc` is a link and ours is a folder, so everything moves away before extracting the ramdisk, busybox included.

## Host

```bash
cd host
./build.sh
./GalaxyNoteDisplay --stats
```

`--test` runs capture and encoding without a tablet.

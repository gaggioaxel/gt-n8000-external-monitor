# Project history

The project started as a way to use the tablet as a low-latency external screen, mostly for drawing. It ended up taking a different road than planned.

| Stage | Outcome |
| --- | --- |
| Android app with MediaCodec | worked, but with SurfaceFlinger and Java in the way |
| Wayland host with a virtual monitor | worked; the dummy Xorg screen would not turn on |
| Mainline Linux 7.2 on the tablet | abandoned: white panel and no USB gadget |
| Samsung kernel with our own initramfs | worked on the first try |
| MFC decoding and FIMC conversion | first image without Android |
| A true 60 fps | solved the Mutter limit |
| Boot menu and automatic host | daily use needs no intervention |
| Touch and pen to the PC | dropped |

## Why not the mainline kernel

A device tree for the p4note exists, but MFC, FIMC and Wacom are missing from it. The decompressor hung when enabling the cache, and the kernel reached the end of boot with a white panel. The Samsung kernel already offers `/dev/s3c-mfc`, the FIMC and `/dev/usb_accessory`, so the same result came with far less work.

## Touch and pen

Touch was forwarded to the virtual monitor through Mutter's RemoteDesktop API and worked technically, but it was not comfortable to use. The pen had already gone badly on Android and was not ported. The system stayed video-only, and touch is used to open the panel.

## Not measured

The total latency from the PC to the panel was never measured with a camera. The times in this repository are those inside the tablet.

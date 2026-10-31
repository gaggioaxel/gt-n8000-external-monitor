# Costruisce boot.img per il GT-N8000: kernel e ramdisk Android presi dal tablet (vendor/,
# vedi tools/fetch-vendor.sh), initramfs con il menu di avvio e notedisplayd.
CROSS   ?= arm-linux-gnueabihf-
CC      := $(CROSS)gcc
STRIP   := $(CROSS)strip
CFLAGS  := -O2 -Wall -Wextra -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast -static -pthread -Isrc/kernel
CMDLINE := console=ttySAC2,115200 buildvariant=user

SRC     := src/notedisplayd.c src/mfc.c src/display.c src/panel.c
HDR     := $(wildcard src/*.h src/kernel/*.h)
VENDOR  := vendor/zImage vendor/ramdisk.img vendor/os_version vendor/mfc_fw.bin vendor/busybox
ROOT    := build/rootfs

all: build/notedisplayd build/bootmenu

image: build/boot.img

build/notedisplayd: $(SRC) $(HDR)
	mkdir -p build
	$(CC) $(CFLAGS) $(SRC) -o $@
	$(STRIP) $@

build/bootmenu: src/bootmenu.c src/font.h
	mkdir -p build
	$(CC) $(CFLAGS) src/bootmenu.c -o $@
	$(STRIP) $@

build/initramfs.cpio.gz: build/notedisplayd build/bootmenu initramfs/init $(VENDOR)
	rm -rf $(ROOT)
	mkdir -p $(ROOT)/bin $(ROOT)/sbin $(ROOT)/usr/bin $(ROOT)/usr/sbin $(ROOT)/etc \
		$(ROOT)/dev $(ROOT)/proc $(ROOT)/sys $(ROOT)/tmp $(ROOT)/root \
		$(ROOT)/var/lib/misc $(ROOT)/lib/firmware
	cp vendor/busybox build/notedisplayd build/bootmenu $(ROOT)/bin/
	cp vendor/mfc_fw.bin $(ROOT)/lib/firmware/
	cp vendor/ramdisk.img $(ROOT)/android.cpio.gz
	cp initramfs/init $(ROOT)/init
	chmod +x $(ROOT)/init
	cd $(ROOT) && find . | LC_ALL=C sort | cpio -o -H newc 2>/dev/null | gzip -9 > ../initramfs.cpio.gz

build/boot.img: build/initramfs.cpio.gz vendor/zImage vendor/os_version
	python3 tools/mkbootimg.py --kernel vendor/zImage --ramdisk $< \
		--cmdline "$(CMDLINE)" --os-version $$(cat vendor/os_version) -o $@

clean:
	rm -rf build

.PHONY: all image clean

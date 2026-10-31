/*
 * bootmenu - GRUB-style system choice at boot, called by /init.
 *
 * Volume up/down moves, Power confirms, a touch on an entry starts it. After 5 seconds
 * without input the entry given as argument starts, that is the most recent choice, or
 * else the external display. The exit code is the chosen entry.
 */
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <linux/fb.h>
#include <linux/input.h>
#include <linux/reboot.h>

#include "font.h"

#define TIMEOUT		5
#define KEYS_NAME	"gpio-keys"
#define TOUCH_NAME	"sec_touchscreen"

#define ROW_X		240
#define ROW_W		800
#define ROW_Y		250
#define ROW_H		90
#define ROW_GAP		16
#define CLOSE_STEPS	14

#define C_BG		0xff14171bu
#define C_ROW		0xff2a2f36u
#define C_SEL		0xff2f7fe0u
#define C_TEXT		0xffe8eaedu
#define C_DIM		0xff9aa0a6u

enum { ITEM_DISPLAY, ITEM_ANDROID, ITEM_RECOVERY, ITEMS };

static const char *const labels[ITEMS] = { "External display", "Android", "Recovery (TWRP)" };

static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static uint8_t *fb;

/* The panel image is upside down relative to fb0, as for the video: x and y are inverted */
static void put(int x, int y, uint32_t color)
{
	int fx = var.xres - 1 - x, fy = var.yres - 1 - y;

	*(uint32_t *)(fb + var.yoffset * fix.line_length + fy * fix.line_length + fx * 4) = color;
}

static void fill(int x, int y, int w, int h, uint32_t color)
{
	for (int r = y; r < y + h; r++)
		for (int c = x; c < x + w; c++)
			put(c, r, color);
}

static void text(int x, int y, uint32_t color, const char *s)
{
	for (; *s; s++, x += FONT_W) {
		const uint8_t *g = font16x32[(uint8_t)*s];

		for (int r = 0; r < FONT_H; r++) {
			int bits = g[r * 2] << 8 | g[r * 2 + 1];

			for (int c = 0; c < FONT_W; c++)
				if (bits >> (15 - c) & 1)
					put(x + c, y + r, color);
		}
	}
}

static void draw(int selected, int left)
{
	char line[64];

	text(ROW_X, 150, C_TEXT, "Galaxy Note 10.1: choose a system");
	for (int i = 0; i < ITEMS; i++) {
		int y = ROW_Y + i * (ROW_H + ROW_GAP);

		fill(ROW_X, y, ROW_W, ROW_H, i == selected ? C_SEL : C_ROW);
		text(ROW_X + 32, y + (ROW_H - FONT_H) / 2, C_TEXT, labels[i]);
	}
	fill(ROW_X, 610, ROW_W, FONT_H, C_BG);
	if (left >= 0)
		snprintf(line, sizeof(line), "Starting automatically in %d s", left);
	else
		snprintf(line, sizeof(line), "Volume to move, Power to start");
	text(ROW_X, 610, C_DIM, line);
}

static void wait_ms(int ms)
{
	struct timespec ts = { 0, ms * 1000000L };

	nanosleep(&ts, NULL);
}

/* Visible confirmation: the entry flashes, then the screen closes onto it */
static int leave(int item)
{
	int y = ROW_Y + item * (ROW_H + ROW_GAP), h = var.yres, top = 0, bottom = h;
	int label_y = y + (ROW_H - FONT_H) / 2;

	fill(ROW_X, y, ROW_W, ROW_H, C_TEXT);
	text(ROW_X + 32, label_y, C_BG, labels[item]);
	wait_ms(90);
	fill(ROW_X, y, ROW_W, ROW_H, C_SEL);
	text(ROW_X + 32, label_y, C_TEXT, labels[item]);
	wait_ms(90);

	for (int i = 1; i <= CLOSE_STEPS; i++) {
		int t = y * i / CLOSE_STEPS;
		int b = h - (h - y - ROW_H) * i / CLOSE_STEPS;

		fill(0, top, var.xres, t - top, 0xff000000u);
		fill(0, b, var.xres, bottom - b, 0xff000000u);
		top = t;
		bottom = b;
		wait_ms(16);
	}
	fill(0, y, var.xres, ROW_H, 0xff000000u);
	text(ROW_X + 32, label_y, C_DIM, "Starting...");
	return item;
}

static int open_input(const char *wanted)
{
	for (int i = 0; i < 16; i++) {
		char path[32], name[64] = "";
		int fd;

		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;
		if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0 && !strcmp(name, wanted))
			return fd;
		close(fd);
	}
	return -1;
}

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Entry under the touch, or -1. The touchscreen is aligned to fb0, the image is upside down */
static int touched_item(int tx, int ty, int xmax, int ymax)
{
	int x = var.xres - 1 - tx * (int)var.xres / (xmax + 1);
	int y = var.yres - 1 - ty * (int)var.yres / (ymax + 1);

	if (x < ROW_X || x >= ROW_X + ROW_W)
		return -1;
	for (int i = 0; i < ITEMS; i++) {
		int top = ROW_Y + i * (ROW_H + ROW_GAP);

		if (y >= top && y < top + ROW_H)
			return i;
	}
	return -1;
}

int main(int argc, char **argv)
{
	/* The Samsung kernel picks the partition from the reboot argument: reboot -f returns to BOOT */
	if (argc > 1 && !strcmp(argv[1], "recovery")) {
		sync();
		return syscall(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
			       LINUX_REBOOT_CMD_RESTART2, "recovery");
	}

	int fd = open("/dev/fb0", O_RDWR);
	int keys, touch, selected = ITEM_DISPLAY, touched = 0, tx = 0, ty = 0, down = 0, was_down = 0;
	int xmax = 4095, ymax = 4095, slot = 0, left = TIMEOUT;
	double deadline = now() + TIMEOUT;

	if (argc > 1 && atoi(argv[1]) > ITEM_DISPLAY && atoi(argv[1]) < ITEMS)
		selected = atoi(argv[1]);

	/* Without a screen there is no choice: Android, the original behaviour */
	if (fd < 0 || ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0 ||
	    ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0)
		return ITEM_ANDROID;
	fb = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED)
		return ITEM_ANDROID;

	keys = open_input(KEYS_NAME);
	touch = open_input(TOUCH_NAME);
	if (touch >= 0) {
		struct input_absinfo a;

		if (ioctl(touch, EVIOCGABS(ABS_MT_POSITION_X), &a) == 0)
			xmax = a.maximum;
		if (ioctl(touch, EVIOCGABS(ABS_MT_POSITION_Y), &a) == 0)
			ymax = a.maximum;
	}

	fill(0, 0, var.xres, var.yres, C_BG);
	draw(selected, left);

	for (;;) {
		struct pollfd p[2] = { { keys, POLLIN, 0 }, { touch, POLLIN, 0 } };
		struct input_event ev[32];
		int interacted = selected;

		poll(p, 2, 200);
		if (p[0].revents & POLLIN) {
			int n = read(keys, ev, sizeof(ev));

			for (int i = 0; i < n / (int)sizeof(ev[0]); i++) {
				if (ev[i].type != EV_KEY || ev[i].value != 1)
					continue;
				if (ev[i].code == KEY_POWER)
					return leave(selected);
				if (ev[i].code == KEY_VOLUMEUP)
					selected = (selected + ITEMS - 1) % ITEMS;
				else if (ev[i].code == KEY_VOLUMEDOWN)
					selected = (selected + 1) % ITEMS;
				touched = 1;
			}
		}
		if (p[1].revents & POLLIN) {
			int n = read(touch, ev, sizeof(ev));

			for (int i = 0; i < n / (int)sizeof(ev[0]); i++) {
				if (ev[i].type == EV_ABS) {
					if (ev[i].code == ABS_MT_SLOT)
						slot = ev[i].value;
					else if (slot != 0)
						continue;
					else if (ev[i].code == ABS_MT_TRACKING_ID)
						down = ev[i].value >= 0;
					else if (ev[i].code == ABS_MT_POSITION_X)
						tx = ev[i].value;
					else if (ev[i].code == ABS_MT_POSITION_Y)
						ty = ev[i].value;
				} else if (ev[i].type == EV_SYN && ev[i].code == SYN_REPORT) {
					if (down && !was_down) {
						int item = touched_item(tx, ty, xmax, ymax);

						if (item >= 0)
							return leave(item);
					}
					was_down = down;
				}
			}
		}

		/* As in GRUB, any key stops the countdown */
		if (!touched) {
			int l = (int)(deadline - now()) + 1;

			if (now() >= deadline)
				return leave(selected);
			if (l != left) {
				left = l;
				draw(selected, left);
			}
		} else if (interacted != selected || left != -1) {
			left = -1;
			draw(selected, left);
		}
	}
}

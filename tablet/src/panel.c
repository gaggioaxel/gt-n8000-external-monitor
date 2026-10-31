/*
 * Pop-up panel: a touch or Power opens it, Volume changes brightness and opens it if closed.
 * It closes by itself after a few seconds or with a touch outside. It shows battery and
 * statistics, and sets brightness, rotation, the fps limit and the PC keepalive.
 *
 * The input thread does all the slow work (sysfs, text drawing) and publishes an image
 * already rotated like fb0; the presenter only has to copy it over the video.
 */
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>

#include "display.h"
#include "font.h"
#include "panel.h"

_Static_assert(sizeof(struct input_event) == 16, "kernel 3.0 input_event");

#define TOUCH_NAME	"sec_touchscreen"
#define KEYS_NAME	"gpio-keys"
#define BL_PATH		"/sys/class/backlight/panel/brightness"
#define BAT_PATH	"/sys/class/power_supply/battery/"
#define USB_ONLINE	"/sys/class/power_supply/usb/online"
#define AC_ONLINE	"/sys/class/power_supply/ac/online"
#define OFF_AFTER	10		/* seconds between cable removal and power off */
#define BL_MAX		255
#define BL_MIN		10		/* never zero: the panel would be needed to turn it back on */
#define BL_STEP		13		/* 5% for each Volume press */

#define HIDE_AFTER	6.0		/* seconds without touches */
#define PW		620
#define PH		456
#define PAD		12
#define PTOP		8		/* distance from the top edge */
#define ROW_H		36
#define BTN_H		52
#define LABEL_W		150

/* Top to bottom: Y values are relative to the panel */
#define Y_BATTERY	8
#define Y_BRIGHT	(Y_BATTERY + ROW_H)
#define Y_SLIDER	(Y_BRIGHT + ROW_H)
#define SLIDER_H	44
#define Y_ROT		(Y_SLIDER + SLIDER_H + 8)
#define Y_FPS		(Y_ROT + BTN_H + 8)
#define Y_KEEP		(Y_FPS + BTN_H + 8)
#define Y_INFO		(Y_KEEP + BTN_H + 8)

#define C_BG		0xff1e2228u
#define C_BORDER	0xff4a5058u
#define C_TEXT		0xffe8eaedu
#define C_DIM		0xff9aa0a6u
#define C_BTN		0xff343a42u
#define C_ACCENT	0xff2f7fe0u

enum { G_ROT, G_FPS, G_KEEP };

struct button {
	int group, value;
	const char *label;
};

static const struct button buttons[] = {
	{ G_ROT, 180, "Land" }, { G_ROT, 90, "Port" }, { G_ROT, 0, "Land 180" }, { G_ROT, 270, "Port 180" },
	{ G_FPS, 30, "30" }, { G_FPS, 45, "45" }, { G_FPS, 60, "60" },
	{ G_KEEP, 25, "25" }, { G_KEEP, 50, "50" }, { G_KEEP, 100, "100" }, { G_KEEP, 250, "250" },
};
#define NBUTTONS	(int)(sizeof(buttons) / sizeof(buttons[0]))

static pthread_mutex_t plock = PTHREAD_MUTEX_INITIALIZER;

/* Protected by plock */
static int rotation = 180;
static struct panel_settings settings = { 0, 60, 50 };
static unsigned int settings_gen;
static struct panel_stats stats;
static char host_encoder[16];
static float host_fps, host_encode_ms;
static int visible, idle_drawn;
static int img_x, img_y, img_w, img_h;
static const uint32_t *img;

/* Owned by the input thread */
static uint32_t canvas[PH][PW];
static uint32_t images[2][PW * PH];
static int next_image;
static int fb_w, fb_h;
static int abs_x_max, abs_y_max;
static int brightness = 150;
static int bat_percent = -1, bat_temp;
static char bat_status[24];
static double last_touch, last_battery, last_power;
static double power_lost;		/* time of removal, 0 while the cable is in */

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int read_int(const char *path, int fallback)
{
	char buf[32];
	int fd = open(path, O_RDONLY);
	int n = fd < 0 ? -1 : read(fd, buf, sizeof(buf) - 1);

	if (fd >= 0)
		close(fd);
	if (n <= 0)
		return fallback;
	buf[n] = 0;
	return atoi(buf);
}

static void read_battery(void)
{
	char buf[24];
	int fd = open(BAT_PATH "status", O_RDONLY);
	int n = fd < 0 ? -1 : read(fd, buf, sizeof(buf) - 1);

	if (fd >= 0)
		close(fd);
	bat_status[0] = 0;
	if (n > 0) {
		buf[n] = 0;
		strcpy(bat_status, !strncmp(buf, "Charging", 8) ? "charging" :
				   !strncmp(buf, "Full", 4) ? "full" : "");
	}
	bat_percent = read_int(BAT_PATH "capacity", -1);
	bat_temp = read_int(BAT_PATH "temp", 0);
	last_battery = now();
}

static void set_brightness(int value)
{
	char buf[16];
	int fd, n;

	if (value < BL_MIN)
		value = BL_MIN;
	if (value > BL_MAX)
		value = BL_MAX;
	if (value == brightness)
		return;
	brightness = value;
	fd = open(BL_PATH, O_WRONLY);
	if (fd < 0)
		return;
	n = snprintf(buf, sizeof(buf), "%d\n", value);
	if (write(fd, buf, n) < 0)
		perror("panel: brightness");
	close(fd);
}

/* --------------------------------------------------------------- drawing */

static void fill(int x, int y, int w, int h, uint32_t color)
{
	for (int r = y; r < y + h; r++)
		for (int c = x; c < x + w; c++)
			canvas[r][c] = color;
}

/* The font is codepage 437: \xf8 is the degree sign */
static void text(int x, int y, uint32_t color, const char *s)
{
	for (; *s && x + FONT_W <= PW; s++, x += FONT_W) {
		const uint8_t *g = font16x32[(uint8_t)*s];

		for (int r = 0; r < FONT_H; r++) {
			int bits = g[r * 2] << 8 | g[r * 2 + 1];

			for (int c = 0; c < FONT_W; c++)
				if (bits >> (15 - c) & 1)
					canvas[y + r][x + c] = color;
		}
	}
}

static void button_rect(int i, int *x, int *y, int *w)
{
	static const int rows[] = { Y_ROT, Y_FPS, Y_KEEP };
	int group = buttons[i].group, first = 0, count = 0;
	int left = group == G_ROT ? PAD : PAD + LABEL_W;

	for (int b = 0; b < NBUTTONS; b++) {
		if (buttons[b].group == group) {
			if (!count)
				first = b;
			count++;
		}
	}
	*w = (PW - PAD - left - 8 * (count - 1)) / count;
	*x = left + (i - first) * (*w + 8);
	*y = rows[group];
}

static int button_selected(const struct button *b, const struct panel_settings *s, int rot)
{
	return b->group == G_ROT ? b->value == rot :
	       b->group == G_FPS ? b->value == s->fps : b->value == s->keepalive_ms;
}

static void user_to_fb(int rot, int ux, int uy, int *fx, int *fy)
{
	switch (rot) {
	case 0:   *fx = ux;           *fy = uy;           break;
	case 90:  *fx = fb_w - 1 - uy; *fy = ux;           break;
	case 180: *fx = fb_w - 1 - ux; *fy = fb_h - 1 - uy; break;
	default:  *fx = uy;           *fy = fb_h - 1 - ux; break;
	}
}

static void fb_to_user(int rot, int fx, int fy, int *ux, int *uy)
{
	switch (rot) {
	case 0:   *ux = fx;           *uy = fy;           break;
	case 90:  *ux = fy;           *uy = fb_w - 1 - fx; break;
	case 180: *ux = fb_w - 1 - fx; *uy = fb_h - 1 - fy; break;
	default:  *ux = fb_h - 1 - fy; *uy = fx;           break;
	}
}

static int user_width(int rot)
{
	return rot == 90 || rot == 270 ? fb_h : fb_w;
}

static void draw_controls(void)
{
	struct panel_stats st;
	struct panel_settings set;
	char line[64], encoder[16];
	float pc_fps, pc_ms;
	int rot, percent = (brightness * 100 + BL_MAX / 2) / BL_MAX;

	pthread_mutex_lock(&plock);
	st = stats;
	set = settings;
	rot = rotation;
	strcpy(encoder, host_encoder);
	pc_fps = host_fps;
	pc_ms = host_encode_ms;
	pthread_mutex_unlock(&plock);

	if (bat_percent >= 0)
		snprintf(line, sizeof(line), "Battery %d%%  %s  %d.%d\xf8""C", bat_percent, bat_status,
			 bat_temp / 10, abs(bat_temp) % 10);
	else
		snprintf(line, sizeof(line), "Battery not available");
	text(PAD, Y_BATTERY, C_TEXT, line);

	snprintf(line, sizeof(line), "Brightness %d%%", percent);
	text(PAD, Y_BRIGHT, C_TEXT, line);
	{
		int track = PW - 2 * PAD;
		int knob = (brightness - BL_MIN) * (track - 20) / (BL_MAX - BL_MIN);

		fill(PAD, Y_SLIDER + 16, track, 12, C_BTN);
		fill(PAD, Y_SLIDER + 16, knob + 10, 12, C_ACCENT);
		fill(PAD + knob, Y_SLIDER + 2, 20, 40, C_TEXT);
	}

	text(PAD, Y_FPS + 10, C_DIM, "FPS limit");
	text(PAD, Y_KEEP + 10, C_DIM, "Repeat ms");
	for (int i = 0; i < NBUTTONS; i++) {
		int x, y, w, sel = button_selected(&buttons[i], &set, rot);
		int len = strlen(buttons[i].label) * FONT_W;

		button_rect(i, &x, &y, &w);
		fill(x, y, w, BTN_H, sel ? C_ACCENT : C_BTN);
		text(x + (w - len) / 2, y + 10, C_TEXT, buttons[i].label);
	}

	snprintf(line, sizeof(line), "Video %.0f fps  %.1f Mbit/s  %dx%d", st.fps, st.mbit,
		 st.stream_w, st.stream_h);
	text(PAD, Y_INFO, C_TEXT, line);
	snprintf(line, sizeof(line), "Tablet dec %.1f ms  fimc %.1f ms", st.decode_ms, st.fimc_ms);
	text(PAD, Y_INFO + ROW_H - 2, C_DIM, line);
	if (encoder[0])
		snprintf(line, sizeof(line), "PC %s  %.0f fps  encode %.1f ms", encoder, pc_fps, pc_ms);
	else
		snprintf(line, sizeof(line), "PC not connected");
	text(PAD, Y_INFO + 2 * ROW_H - 4, C_DIM, line);
}

static void draw_alert(void)
{
	char line[64];
	int left = OFF_AFTER - (int)(now() - power_lost);

	text(PAD + 16, 120, C_TEXT, "Connection to the PC lost");
	snprintf(line, sizeof(line), "Powering off in %d s", left > 0 ? left : 0);
	text(PAD + 16, 180, C_TEXT, line);
	text(PAD + 16, 240, C_DIM, "Reconnect the cable to cancel");
}

/* Prepares the image and publishes it; from here on the panel is visible */
static void redraw(void)
{
	fill(0, 0, PW, PH, C_BORDER);
	fill(2, 2, PW - 4, PH - 4, C_BG);
	if (power_lost)
		draw_alert();
	else
		draw_controls();

	/* Panel rectangle in fb0 coordinates */
	int rot = panel_rotation();
	int x0 = (user_width(rot) - PW) / 2, ax, ay, bx, by;

	user_to_fb(rot, x0, PTOP, &ax, &ay);
	user_to_fb(rot, x0 + PW - 1, PTOP + PH - 1, &bx, &by);
	int rx = ax < bx ? ax : bx, ry = ay < by ? ay : by;
	int rw = abs(bx - ax) + 1, rh = abs(by - ay) + 1;
	uint32_t *out = images[next_image];

	for (int v = 0; v < PH; v++) {
		for (int u = 0; u < PW; u++) {
			int fx, fy;

			user_to_fb(rot, x0 + u, PTOP + v, &fx, &fy);
			out[(fy - ry) * rw + fx - rx] = canvas[v][u];
		}
	}

	pthread_mutex_lock(&plock);
	img = out;
	img_x = rx;
	img_y = ry;
	img_w = rw;
	img_h = rh;
	visible = 1;
	pthread_mutex_unlock(&plock);
	next_image = !next_image;
}

static void hide(void)
{
	pthread_mutex_lock(&plock);
	visible = 0;
	pthread_mutex_unlock(&plock);
}

/* If the state cannot be read the cable counts as connected: never power off on an error */
static void check_power(void)
{
	double t = now();

	if (t - last_power < 1)
		return;
	last_power = t;
	if (read_int(USB_ONLINE, 1) || read_int(AC_ONLINE, 0)) {
		if (power_lost) {
			printf("cable reconnected, power off cancelled\n");
			power_lost = 0;
			hide();
		}
		return;
	}
	if (!power_lost) {
		printf("cable removed, powering off in %d s\n", OFF_AFTER);
		power_lost = t;
	}
	if (t - power_lost >= OFF_AFTER) {
		printf("powering off\n");
		sync();
		reboot(RB_POWER_OFF);
	}
	redraw();
}

/* --------------------------------------------------------------- input */

static int is_open(void)
{
	int open;

	pthread_mutex_lock(&plock);
	open = visible;
	pthread_mutex_unlock(&plock);
	return open;
}

static void show(void)
{
	brightness = read_int(BL_PATH, brightness);
	read_battery();
	last_touch = now();
	redraw();
}

static void on_key(int code, int value)
{
	if (power_lost)
		return;
	if (code == KEY_POWER && value == 1) {
		if (is_open())
			hide();
		else
			show();
	} else if ((code == KEY_VOLUMEUP || code == KEY_VOLUMEDOWN) && value) {
		if (!is_open())
			show();
		set_brightness(brightness + (code == KEY_VOLUMEUP ? BL_STEP : -BL_STEP));
		last_touch = now();
		redraw();
	}
}

static void select_button(const struct button *b)
{
	pthread_mutex_lock(&plock);
	switch (b->group) {
	case G_ROT:
		rotation = b->value;
		settings.portrait = b->value == 90 || b->value == 270;
		break;
	case G_FPS:
		settings.fps = b->value;
		break;
	default:
		settings.keepalive_ms = b->value;
		break;
	}
	settings_gen++;
	pthread_mutex_unlock(&plock);
}

static void on_touch(int down, int pressed, int ux, int uy, int rot)
{
	static int dragging;
	int x0 = (user_width(rot) - PW) / 2;
	int px = ux - x0, py = uy - PTOP;
	int inside = px >= 0 && px < PW && py >= 0 && py < PH;

	if (power_lost)
		return;
	if (!is_open()) {
		dragging = 0;
		if (pressed)
			show();
		return;
	}
	if (!down) {
		dragging = 0;
		return;
	}
	last_touch = now();
	if (pressed) {
		if (!inside) {
			hide();
			return;
		}
		if (py >= Y_SLIDER && py < Y_SLIDER + SLIDER_H)
			dragging = 1;
		for (int i = 0; i < NBUTTONS; i++) {
			int x, y, w;

			button_rect(i, &x, &y, &w);
			if (px >= x && px < x + w && py >= y && py < y + BTN_H) {
				select_button(&buttons[i]);
				redraw();
			}
		}
	}
	if (dragging) {
		int track = PW - 2 * PAD - 20;
		int v = (px - PAD - 10) * (BL_MAX - BL_MIN) / track + BL_MIN;
		int old = brightness;

		set_brightness(v);
		if (brightness != old)
			redraw();
	}
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
	fprintf(stderr, "panel: %s not found\n", wanted);
	return -1;
}

static int open_touch(void)
{
	struct input_absinfo ax, ay;
	int fd = open_input(TOUCH_NAME);

	if (fd < 0)
		return -1;
	if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &ax) < 0 ||
	    ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &ay) < 0) {
		close(fd);
		return -1;
	}
	abs_x_max = ax.maximum;
	abs_y_max = ay.maximum;
	printf("touch: x 0-%d, y 0-%d\n", abs_x_max, abs_y_max);
	return fd;
}

static void *input_thread(void *arg)
{
	int fd = (intptr_t)arg;
	int keys = open_input(KEYS_NAME);
	int slot = 0, tx = 0, ty = 0, down = 0, was_down = 0, changed = 0;
	double last_redraw = 0;

	for (;;) {
		struct pollfd pfd[2] = { { fd, POLLIN, 0 }, { keys, POLLIN, 0 } };
		struct input_event ev[32];

		/* poll ignores a negative fd: the cable check works anyway */
		poll(pfd, 2, 500);
		if (pfd[1].revents & POLLIN) {
			int n = read(keys, ev, sizeof(ev));

			for (int i = 0; i < n / (int)sizeof(ev[0]); i++)
				if (ev[i].type == EV_KEY)
					on_key(ev[i].code, ev[i].value);
		}
		if (pfd[0].revents & POLLIN) {
			int n = read(fd, ev, sizeof(ev));

			for (int i = 0; i < n / (int)sizeof(ev[0]); i++) {
				if (ev[i].type == EV_ABS) {
					if (ev[i].code == ABS_MT_SLOT)
						slot = ev[i].value;
					else if (slot != 0)
						continue;	/* only the first finger counts */
					else if (ev[i].code == ABS_MT_TRACKING_ID)
						down = ev[i].value >= 0;
					else if (ev[i].code == ABS_MT_POSITION_X)
						tx = ev[i].value;
					else if (ev[i].code == ABS_MT_POSITION_Y)
						ty = ev[i].value;
					changed = 1;
				} else if (ev[i].type == EV_SYN && ev[i].code == SYN_REPORT && changed) {
					int rot = panel_rotation(), ux, uy;

					fb_to_user(rot, tx * fb_w / (abs_x_max + 1),
						   ty * fb_h / (abs_y_max + 1), &ux, &uy);
					on_touch(down, down && !was_down, ux, uy, rot);
					was_down = down;
					changed = 0;
				}
			}
		}

		check_power();
		if (is_open() && !power_lost) {
			double t = now();

			if (t - last_touch >= HIDE_AFTER) {
				hide();
			} else if (t - last_redraw >= 1) {
				if (t - last_battery >= 5)
					read_battery();
				redraw();
				last_redraw = t;
			}
		}
	}
	return NULL;
}

/* --------------------------------------------------------------- interface */

int panel_start(void)
{
	pthread_t th;
	int fd = open_touch();

	display_size(&fb_w, &fb_h);
	return pthread_create(&th, NULL, input_thread, (void *)(intptr_t)fd);
}

int panel_rotation(void)
{
	int r;

	pthread_mutex_lock(&plock);
	r = rotation;
	pthread_mutex_unlock(&plock);
	return r;
}

unsigned int panel_settings(struct panel_settings *s)
{
	unsigned int gen;

	pthread_mutex_lock(&plock);
	*s = settings;
	gen = settings_gen;
	pthread_mutex_unlock(&plock);
	return gen;
}

void panel_set_stats(const struct panel_stats *s)
{
	pthread_mutex_lock(&plock);
	stats = *s;
	pthread_mutex_unlock(&plock);
}

void panel_set_host(const char *encoder, float fps, float encode_ms)
{
	pthread_mutex_lock(&plock);
	snprintf(host_encoder, sizeof(host_encoder), "%s", encoder);
	host_fps = fps;
	host_encode_ms = encode_ms;
	pthread_mutex_unlock(&plock);
}

void panel_blit(void)
{
	pthread_mutex_lock(&plock);
	if (visible)
		display_blit(img, img_x, img_y, img_w, img_h, 0);
	pthread_mutex_unlock(&plock);
}

void panel_idle(void)
{
	pthread_mutex_lock(&plock);
	if (visible) {
		display_blit(img, img_x, img_y, img_w, img_h, 1);
		idle_drawn = 1;
	} else if (idle_drawn) {
		display_clear(img_x, img_y, img_w, img_h);
		idle_drawn = 0;
	}
	pthread_mutex_unlock(&plock);
}

/*
 * Visualizzazione su fb0 del kernel Samsung 3.0: la FIMC converte il frame del decoder
 * (NV12 a tile 64x32, per indirizzo fisico, zero copie) in RGB32 nella pagina nascosta di
 * fb0, poi FBIOPAN_DISPLAY la rende visibile. Stessa sequenza di V4L2 "one shot" del
 * libhwcomposer exynos4 (../ref/SecHWCUtils.c), che usa /dev/video3 (FIMC3).
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/fb.h>
#include <linux/videodev2.h>

#include "display.h"

#define FIMC_DEV		"/dev/video3"
#define FB_DEV			"/dev/fb0"
#define V4L2_PIX_FMT_NV12T	v4l2_fourcc('T', 'V', '1', '2')	/* Samsung: NV12 a tile 64x32 */
#define V4L2_CID_ROTATION	0x08000000			/* Samsung, dal libhwcomposer */

/*
 * v4l2_buffer del kernel 3.0: timeval a 32 bit. Il toolchain usa time_t a 64 bit, quindi la
 * struttura dell'header ha un'altra dimensione e VIDIOC_QBUF un altro numero di ioctl.
 */
struct kbuffer {
	uint32_t index, type, bytesused, flags, field;
	int32_t tv_sec, tv_usec;
	struct v4l2_timecode timecode;
	uint32_t sequence, memory;
	unsigned long userptr;
	uint32_t length, input, reserved;
};
_Static_assert(sizeof(struct kbuffer) == 68, "v4l2_buffer del kernel 3.0");
_Static_assert(sizeof(struct v4l2_framebuffer) == 44, "v4l2_framebuffer del kernel 3.0");
_Static_assert(sizeof(struct v4l2_format) == 204, "v4l2_format del kernel 3.0");

#define KVIDIOC_QBUF	_IOWR('V', 15, struct kbuffer)
#define KVIDIOC_DQBUF	_IOWR('V', 17, struct kbuffer)

/* Dal driver fimc: QBUF riceve in userptr gli indirizzi fisici dei piani */
struct fimc_buf {
	unsigned int base[3];
	size_t length[3];
};

static int fb_fd = -1, fimc_fd = -1;
static struct fb_var_screeninfo var;
static struct fb_fix_screeninfo fix;
static struct v4l2_framebuffer fbuf;
static uint8_t *fb_map;
static int back;			/* pagina nascosta, 0 o 1 */
static int src_w, src_h, crop_w, crop_h;

static int vioctl(unsigned long req, void *arg, const char *what)
{
	int r = ioctl(fimc_fd, req, arg);

	if (r < 0)
		fprintf(stderr, "fimc: %s fallita (errno %d)\n", what, errno);
	return r;
}

int display_open(void)
{
	struct v4l2_capability cap;
	struct v4l2_format f;

	fb_fd = open(FB_DEV, O_RDWR);
	if (fb_fd < 0) {
		perror("display: open " FB_DEV);
		return -1;
	}
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0 ||
	    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		perror("display: info fb0");
		return -1;
	}
	if (var.yres_virtual < 2 * var.yres) {
		fprintf(stderr, "display: serve il doppio buffer, area virtuale %ux%u\n",
			var.xres_virtual, var.yres_virtual);
		return -1;
	}
	back = var.yoffset ? 0 : 1;
	printf("display: fb0 %ux%u virtuale %ux%u %u bpp, riga %u, fisico 0x%08lx, offset y %u\n",
	       var.xres, var.yres, var.xres_virtual, var.yres_virtual, var.bits_per_pixel,
	       fix.line_length, fix.smem_start, var.yoffset);
	fb_map = mmap(NULL, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_map == MAP_FAILED) {
		perror("display: mmap fb0");
		fb_map = NULL;
		return -1;
	}
	/* Via il menu di avvio: fino al primo frame lo schermo resta nero */
	memset(fb_map, 0, 2 * var.yres * fix.line_length);

	fimc_fd = open(FIMC_DEV, O_RDWR);
	if (fimc_fd < 0) {
		perror("display: open " FIMC_DEV);
		return -1;
	}
	if (vioctl(VIDIOC_QUERYCAP, &cap, "QUERYCAP") < 0)
		return -1;
	printf("display: %s, %s\n", (char *)cap.driver, (char *)cap.card);
	/* Va chiamata per prima: alloca ctrl->out, senza cui G_FBUF manda il kernel in panic */
	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	return vioctl(VIDIOC_G_FMT, &f, "G_FMT output") < 0 ? -1 : 0;
}

void display_size(int *w, int *h)
{
	*w = var.xres;
	*h = var.yres;
}

int display_configure(int buf_w, int buf_h, int w, int h)
{
	memset(&fbuf, 0, sizeof(fbuf));
	if (vioctl(VIDIOC_G_FBUF, &fbuf, "G_FBUF") < 0)
		return -1;
	src_w = buf_w;
	src_h = buf_h;
	crop_w = w;
	crop_h = h;
	return 0;
}

/* Il driver accetta i controlli solo a stream fermo: fra un frame e l'altro lo è */
static int set_destination(unsigned int phys, int rotation)
{
	struct v4l2_control c = { .id = V4L2_CID_ROTATION, .value = rotation };
	struct v4l2_format f;
	int swap = rotation == 90 || rotation == 270;	/* a 90 gradi la destinazione è verticale */
	int w = swap ? var.yres : var.xres;
	int h = swap ? var.xres : var.yres;

	if (vioctl(VIDIOC_S_CTRL, &c, "S_CTRL rotazione") < 0)
		return -1;

	fbuf.base = (void *)phys;
	fbuf.fmt.width = w;
	fbuf.fmt.height = h;
	fbuf.fmt.pixelformat = V4L2_PIX_FMT_RGB32;
	if (vioctl(VIDIOC_S_FBUF, &fbuf, "S_FBUF") < 0)
		return -1;

	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	f.fmt.win.w.width = w;
	f.fmt.win.w.height = h;
	return vioctl(VIDIOC_S_FMT, &f, "S_FMT overlay");
}

static int set_source(void)
{
	struct v4l2_format f;
	struct v4l2_crop c;
	struct v4l2_requestbuffers req;

	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	f.fmt.pix.width = src_w;
	f.fmt.pix.height = src_h;
	f.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12T;
	f.fmt.pix.field = V4L2_FIELD_NONE;
	if (vioctl(VIDIOC_S_FMT, &f, "S_FMT output") < 0)
		return -1;

	memset(&c, 0, sizeof(c));
	c.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	c.c.width = crop_w;
	c.c.height = crop_h;
	if (vioctl(VIDIOC_S_CROP, &c, "S_CROP") < 0)
		return -1;

	memset(&req, 0, sizeof(req));
	req.count = 1;
	req.memory = V4L2_MEMORY_USERPTR;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	return vioctl(VIDIOC_REQBUFS, &req, "REQBUFS");
}

int display_convert(unsigned int y_phys, unsigned int c_phys, int rotation)
{
	struct fimc_buf src;
	struct kbuffer b;
	struct v4l2_requestbuffers req;
	int type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	int ret = -1;

	if (set_destination(fix.smem_start + back * fix.line_length * var.yres, rotation) < 0 ||
	    set_source() < 0)
		return -1;
	if (vioctl(VIDIOC_STREAMON, &type, "STREAMON") < 0)
		goto clear;

	memset(&src, 0, sizeof(src));
	src.base[0] = y_phys;
	src.base[1] = c_phys;
	memset(&b, 0, sizeof(b));
	b.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	b.memory = V4L2_MEMORY_USERPTR;
	b.userptr = (unsigned long)&src;
	if (vioctl(KVIDIOC_QBUF, &b, "QBUF") == 0) {
		memset(&b, 0, sizeof(b));
		b.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		b.memory = V4L2_MEMORY_USERPTR;
		if (vioctl(KVIDIOC_DQBUF, &b, "DQBUF") == 0)
			ret = 0;
	}
	vioctl(VIDIOC_STREAMOFF, &type, "STREAMOFF");
clear:
	memset(&req, 0, sizeof(req));
	req.memory = V4L2_MEMORY_USERPTR;
	req.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
	vioctl(VIDIOC_REQBUFS, &req, "REQBUFS 0");
	return ret;
}

static uint8_t *pixel(int x, int y, int front)
{
	int page = front ? !back : back;

	return fb_map + (page * var.yres + y) * fix.line_length + x * 4;
}

void display_blit(const uint32_t *px, int x, int y, int w, int h, int front)
{
	for (int r = 0; r < h; r++)
		memcpy(pixel(x, y + r, front), px + r * w, w * 4);
}

void display_clear(int x, int y, int w, int h)
{
	for (int r = 0; r < h; r++)
		memset(pixel(x, y + r, 1), 0, w * 4);
}

int display_flip(void)
{
	var.yoffset = back * var.yres;
	if (ioctl(fb_fd, FBIOPAN_DISPLAY, &var) < 0) {
		fprintf(stderr, "display: FBIOPAN_DISPLAY fallita (errno %d)\n", errno);
		return -1;
	}
	back = !back;
	return 0;
}

int display_wait_vsync(void)
{
	uint32_t crtc = 0;

	return ioctl(fb_fd, FBIO_WAITFORVSYNC, &crtc);
}

void display_close(void)
{
	if (fb_map)
		munmap(fb_map, fix.smem_len);
	if (fimc_fd >= 0)
		close(fimc_fd);
	if (fb_fd >= 0)
		close(fb_fd);
	fb_map = NULL;
	fimc_fd = fb_fd = -1;
}

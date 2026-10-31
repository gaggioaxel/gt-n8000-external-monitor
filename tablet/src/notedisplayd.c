/*
 * notedisplayd - the Galaxy Note 10.1 as a USB monitor, on the GT-N8000 Samsung 3.0 kernel.
 *
 * Reads the host stream (GalaxyNoteDisplay) from /dev/usb_accessory with the protocol
 * [0xA5][type u8][length u32 big-endian][payload].
 * USB receive -> queue -> MFC decode -> latest frame -> FIMC conversion and pan on fb0.
 * Only the display step may skip frames: the decoder must see all of them, because P frames
 * depend on the previous ones. The pop-up panel lives in panel.c.
 *
 *   notedisplayd [-n]	 -n: do not wait for vsync after the pan
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "display.h"
#include "mfc.h"
#include "panel.h"

#define MAGIC		0xA5
#define T_NOOP		0x00
#define T_CONFIG	0x01
#define T_FRAME		0x02
#define T_KEY		0x03
#define T_INFO		0x04		/* PC -> tablet: encoder, fps, encode ms, as text */
#define T_SETTINGS	0x13		/* tablet -> PC: portrait u8, fps u8, keepalive ms u16 */
#define ACC_READ_MAX	16384		/* the f_accessory driver reads at most 16 KB at a time */
#define MAX_PAYLOAD	(8 << 20)
#define QSLOTS		4		/* beyond this, receive slows the host instead of building up delay */
#define IDLE_MS		500		/* with no frames for this long, the panel draws itself */

struct packet {
	uint8_t type;
	uint32_t len;
	uint8_t *data;
};

struct stats {
	unsigned received, keys, decoded, shown, errors, qmax;
	unsigned long long bytes;
	double decode, convert, pan, vsync;
};

static int display_ok, wait_vsync = 1;

/* All state shared between threads is protected by lock */
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t q_cond = PTHREAD_COND_INITIALIZER;	/* queue changed */
static pthread_cond_t show_cond = PTHREAD_COND_INITIALIZER;	/* latest frame or conversion changed */
static struct packet queue[QSLOTS];
static unsigned q_head, q_count;
static int stopping;
static struct mfc_frame latest;
static int latest_ready, converting;
static unsigned int converting_tag;
static int stream_w, stream_h;
static struct stats st;

static double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Read buffer: the driver returns up to 16 KB at a time, packets can straddle reads */
static uint8_t rbuf[ACC_READ_MAX];
static size_t rlen, rpos;

static int read_exact(int fd, uint8_t *dst, size_t n)
{
	while (n) {
		if (rpos == rlen) {
			ssize_t r = read(fd, rbuf, ACC_READ_MAX);

			if (r < 0 && errno == EINTR)
				continue;
			if (r <= 0)
				return -1;
			rlen = r;
			rpos = 0;
		}
		size_t c = rlen - rpos < n ? rlen - rpos : n;

		memcpy(dst, rbuf + rpos, c);
		rpos += c;
		dst += c;
		n -= c;
	}
	return 0;
}

static void queue_push(struct packet p)
{
	pthread_mutex_lock(&lock);
	while (q_count == QSLOTS && !stopping)
		pthread_cond_wait(&q_cond, &lock);
	if (stopping) {
		free(p.data);
	} else {
		queue[(q_head + q_count) % QSLOTS] = p;
		q_count++;
		if (q_count > st.qmax)
			st.qmax = q_count;
		pthread_cond_broadcast(&q_cond);
	}
	pthread_mutex_unlock(&lock);
}

static int queue_pop(struct packet *p)
{
	int ok = 0;

	pthread_mutex_lock(&lock);
	while (!q_count && !stopping)
		pthread_cond_wait(&q_cond, &lock);
	if (!stopping) {
		*p = queue[q_head];
		q_head = (q_head + 1) % QSLOTS;
		q_count--;
		pthread_cond_broadcast(&q_cond);
		ok = 1;
	}
	pthread_mutex_unlock(&lock);
	return ok;
}

/*
 * The frame being converted is a reference for the next one, so the decoder can reuse its
 * buffer only from the one after that: until then it waits for the FIMC.
 */
static void wait_conversion(unsigned int tag)
{
	pthread_mutex_lock(&lock);
	while (!stopping && converting && converting_tag + 1 < tag)
		pthread_cond_wait(&show_cond, &lock);
	pthread_mutex_unlock(&lock);
}

/* After this nobody reads the decoder buffers, so it can be closed */
static void drop_frames(void)
{
	pthread_mutex_lock(&lock);
	latest_ready = 0;
	while (converting)
		pthread_cond_wait(&show_cond, &lock);
	pthread_mutex_unlock(&lock);
}

static void *decoder(void *arg)
{
	static uint8_t init_buf[MAX_PAYLOAD + 4096];
	static uint8_t sps_pps[4096];
	size_t sps_pps_len = 0;
	int decoding = 0;
	unsigned int tag = 0;
	struct mfc_info info;
	struct mfc_frame fr;
	struct packet p;

	(void)arg;
	while (queue_pop(&p)) {
		if (p.type == T_CONFIG && p.len >= 6) {
			size_t n = p.len - 6;

			printf("config %ux%u, SPS %u byte\n", p.data[0] << 8 | p.data[1],
			       p.data[2] << 8 | p.data[3], p.data[4] << 8 | p.data[5]);
			/* Annex-B SPS and PPS after the 6 header bytes */
			if (n && n <= sizeof(sps_pps)) {
				/* A different stream (portrait, for example): rebuild the decoder */
				if (decoding && (n != sps_pps_len || memcmp(sps_pps, p.data + 6, n))) {
					drop_frames();
					mfc_close();
					if (mfc_open() < 0)
						fprintf(stderr, "mfc: reopen failed\n");
					decoding = 0;
				}
				sps_pps_len = n;
				memcpy(sps_pps, p.data + 6, n);
			}
		} else if (p.type == T_KEY || p.type == T_FRAME) {
			if (p.type == T_KEY && !decoding && sps_pps_len) {
				/* the MFC wants SPS, PPS and IDR in the same buffer for initialization */
				memcpy(init_buf, sps_pps, sps_pps_len);
				memcpy(init_buf + sps_pps_len, p.data, p.len);
				if (mfc_init(init_buf, sps_pps_len + p.len, &info) == 0) {
					printf("mfc: %dx%d, buffer %dx%d, DPB %d\n", info.width, info.height,
					       info.buf_width, info.buf_height, info.dpb);
					decoding = 1;
					if (display_ok)
						display_configure(info.buf_width, info.buf_height,
								  info.width, info.height);
					pthread_mutex_lock(&lock);
					stream_w = info.width;
					stream_h = info.height;
					pthread_mutex_unlock(&lock);
				}
			}
			if (decoding) {
				double t;
				int r;

				wait_conversion(++tag);
				t = now();
				r = mfc_decode(p.data, p.len, tag, &fr);
				t = now() - t;
				pthread_mutex_lock(&lock);
				if (r == 0) {
					st.decoded++;
					st.decode += t;
					if (fr.status == 1 || fr.status == 2) {
						latest = fr;
						latest_ready = 1;
						pthread_cond_broadcast(&show_cond);
					}
				} else {
					st.errors++;
				}
				pthread_mutex_unlock(&lock);
			}
		}
		free(p.data);
	}
	return NULL;
}

/* Runs for the whole program lifetime: the panel is needed without a stream too */
static void *presenter(void *arg)
{
	(void)arg;
	for (;;) {
		struct mfc_frame fr;
		struct timespec ts;
		double t0, t1, t2, t3;
		int r, timeout = 0;

		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_nsec += IDLE_MS * 1000000L;
		ts.tv_sec += ts.tv_nsec / 1000000000L;
		ts.tv_nsec %= 1000000000L;

		pthread_mutex_lock(&lock);
		while (!latest_ready && !timeout)
			timeout = pthread_cond_timedwait(&show_cond, &lock, &ts) == ETIMEDOUT;
		if (!latest_ready) {
			pthread_mutex_unlock(&lock);
			panel_idle();
			continue;
		}
		fr = latest;
		latest_ready = 0;
		converting = 1;
		converting_tag = fr.tag;
		pthread_mutex_unlock(&lock);

		t0 = now();
		r = display_convert(fr.y_phys, fr.c_phys, panel_rotation());
		t1 = now();

		pthread_mutex_lock(&lock);
		converting = 0;
		pthread_cond_broadcast(&show_cond);
		pthread_mutex_unlock(&lock);

		if (r == 0) {
			panel_blit();
			r = display_flip();
		}
		t2 = now();
		if (r == 0 && wait_vsync)
			display_wait_vsync();
		t3 = now();

		pthread_mutex_lock(&lock);
		if (r == 0) {
			st.shown++;
			st.convert += t1 - t0;
			st.pan += t2 - t1;
			st.vsync += t3 - t2;
		} else {
			st.errors++;
		}
		pthread_mutex_unlock(&lock);
	}
	return NULL;
}

static void print_stats(double dt)
{
	struct stats s;
	struct panel_stats ps;

	pthread_mutex_lock(&lock);
	s = st;
	memset(&st, 0, sizeof(st));
	ps.stream_w = stream_w;
	ps.stream_h = stream_h;
	pthread_mutex_unlock(&lock);
	ps.fps = s.shown / dt;
	ps.mbit = s.bytes * 8 / dt / 1e6;
	ps.decode_ms = s.decoded ? s.decode * 1e3 / s.decoded : 0;
	ps.fimc_ms = s.shown ? s.convert * 1e3 / s.shown : 0;
	panel_set_stats(&ps);
	printf("%5.1f received  %5.1f decoded  %5.1f shown  %6.2f Mbit/s  keyframes %u  "
	       "errors %u  queue %u  decode %.2f ms  fimc %.2f ms  pan %.2f ms  vsync %.2f ms\n",
	       s.received / dt, s.decoded / dt, s.shown / dt, ps.mbit, s.keys, s.errors, s.qmax,
	       ps.decode_ms, ps.fimc_ms, s.shown ? s.pan * 1e3 / s.shown : 0,
	       s.shown ? s.vsync * 1e3 / s.shown : 0);
}

static void send_settings(int fd, const struct panel_settings *s)
{
	uint8_t pkt[10] = { MAGIC, T_SETTINGS, 0, 0, 0, 4, s->portrait, s->fps,
			    s->keepalive_ms >> 8, s->keepalive_ms & 0xff };

	if (write(fd, pkt, sizeof(pkt)) < 0)
		perror("sending settings");
	else
		printf("settings: portrait %d, %d fps, keepalive %d ms\n", s->portrait, s->fps,
		       s->keepalive_ms);
}

static void session(int fd)
{
	pthread_t dec;
	uint8_t hdr[6];
	double t0 = now();
	unsigned int sent_gen = 0;
	int sent = 0;

	rlen = rpos = 0;
	q_head = q_count = 0;
	stopping = 0;
	memset(&st, 0, sizeof(st));
	pthread_create(&dec, NULL, decoder, NULL);

	for (;;) {
		if (read_exact(fd, hdr, 1))
			break;
		if (hdr[0] != MAGIC)
			continue;		/* resynchronize after a reconnection */
		if (read_exact(fd, hdr + 1, 5))
			break;
		uint32_t len = (uint32_t)hdr[2] << 24 | hdr[3] << 16 | hdr[4] << 8 | hdr[5];

		if (len > MAX_PAYLOAD)
			continue;
		struct packet p = { hdr[1], len, malloc(len + 1) };

		if (!p.data || read_exact(fd, p.data, len)) {
			free(p.data);
			break;
		}
		p.data[len] = 0;

		if (p.type == T_INFO) {
			char encoder[16];
			float fps, ms;

			if (sscanf((char *)p.data, "%15s %f %f", encoder, &fps, &ms) == 3)
				panel_set_host(encoder, fps, ms);
			free(p.data);
		} else {
			pthread_mutex_lock(&lock);
			if (p.type == T_KEY || p.type == T_FRAME) {
				st.received++;
				st.bytes += len;
				st.keys += p.type == T_KEY;
			}
			pthread_mutex_unlock(&lock);
			queue_push(p);
		}

		/* At the start and on every change: the PC follows what was chosen here */
		struct panel_settings s;
		unsigned int gen = panel_settings(&s);

		if (!sent || gen != sent_gen) {
			send_settings(fd, &s);
			sent = 1;
			sent_gen = gen;
		}

		double dt = now() - t0;

		if (dt >= 2) {
			print_stats(dt);
			t0 = now();
		}
	}

	pthread_mutex_lock(&lock);
	stopping = 1;
	pthread_cond_broadcast(&q_cond);
	pthread_cond_broadcast(&show_cond);
	pthread_mutex_unlock(&lock);
	pthread_join(dec, NULL);
	drop_frames();
	while (q_count) {
		free(queue[q_head].data);
		q_head = (q_head + 1) % QSLOTS;
		q_count--;
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "-n"))
		wait_vsync = 0;
	setvbuf(stdout, NULL, _IOLBF, 0);
	display_ok = display_open() == 0;
	if (display_ok) {
		pthread_t show;

		panel_start();
		pthread_create(&show, NULL, presenter, NULL);
	} else {
		printf("display not available: decode only\n");
	}
	for (;;) {
		int fd = open("/dev/usb_accessory", O_RDWR);

		if (fd < 0) {
			sleep(1);
			continue;
		}
		printf("accessory open, waiting for the stream\n");
		if (mfc_open() == 0) {
			session(fd);
			printf("connection closed\n");
		}
		mfc_close();
		close(fd);
		sleep(1);
	}
}

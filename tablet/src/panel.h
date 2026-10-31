#ifndef NOTEDISPLAY_PANEL_H
#define NOTEDISPLAY_PANEL_H

struct panel_stats {
	float fps, mbit, decode_ms, fimc_ms;
	int stream_w, stream_h;
};

/* Settings chosen on the tablet and applied by the PC */
struct panel_settings {
	int portrait, fps, keepalive_ms;
};

/* Starts the input thread, which also watches the cable: if removed it warns and powers off */
int panel_start(void);
int panel_rotation(void);
/* Copies the current settings and returns a counter that grows with every change */
unsigned int panel_settings(struct panel_settings *s);
void panel_set_stats(const struct panel_stats *s);
void panel_set_host(const char *encoder, float fps, float encode_ms);
/* If the panel is open, copies it to the hidden page before the flip */
void panel_blit(void);
/* Without a stream: shows the panel on the visible page, or erases it when it closes */
void panel_idle(void);

#endif

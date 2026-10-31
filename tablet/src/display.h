#ifndef NOTEDISPLAY_DISPLAY_H
#define NOTEDISPLAY_DISPLAY_H

#include <stdint.h>

int display_open(void);
void display_size(int *w, int *h);
/* buf_w/buf_h: decoder buffer (aligned to tiles); w/h: visible image */
int display_configure(int buf_w, int buf_h, int w, int h);
/* Converts a tiled NV12 frame with the FIMC into the hidden fb0 page, rotated by rotation
 * degrees (0, 90, 180, 270) */
int display_convert(unsigned int y_phys, unsigned int c_phys, int rotation);
/* Copies a rectangle to the hidden page, or to the visible one if front is true */
void display_blit(const uint32_t *px, int x, int y, int w, int h, int front);
void display_clear(int x, int y, int w, int h);
int display_flip(void);
int display_wait_vsync(void);
void display_close(void);

#endif

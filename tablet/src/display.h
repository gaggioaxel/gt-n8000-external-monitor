#ifndef NOTEDISPLAY_DISPLAY_H
#define NOTEDISPLAY_DISPLAY_H

#include <stdint.h>

int display_open(void);
void display_size(int *w, int *h);
/* buf_w/buf_h: buffer del decoder (allineato ai tile); w/h: immagine visibile */
int display_configure(int buf_w, int buf_h, int w, int h);
/* Converte con la FIMC un frame NV12 a tile verso la pagina nascosta di fb0, ruotandolo di
 * rotation gradi (0, 90, 180, 270) */
int display_convert(unsigned int y_phys, unsigned int c_phys, int rotation);
/* Copia un rettangolo nella pagina nascosta, oppure in quella visibile se front è vero */
void display_blit(const uint32_t *px, int x, int y, int w, int h, int front);
void display_clear(int x, int y, int w, int h);
int display_flip(void);
int display_wait_vsync(void);
void display_close(void);

#endif

#ifndef NOTEDISPLAY_MFC_H
#define NOTEDISPLAY_MFC_H

#include <stddef.h>
#include <stdint.h>

struct mfc_info {
	int width, height;		/* immagine */
	int buf_width, buf_height;	/* buffer (allineato ai tile) */
	int dpb;			/* frame di riferimento usati dal decoder */
};

struct mfc_frame {
	int status;			/* DISP_S_*: 0 decode only, 1 decode+display, 2 display only, 3 fine */
	unsigned int tag;		/* frametag del frame mostrato */
	unsigned int y_phys, c_phys;	/* indirizzi fisici luma/croma (NV12 a tile 64x32) */
};

int mfc_open(void);
int mfc_init(const uint8_t *au, size_t len, struct mfc_info *info);
int mfc_decode(const uint8_t *au, size_t len, unsigned int tag, struct mfc_frame *out);
void mfc_close(void);

#endif

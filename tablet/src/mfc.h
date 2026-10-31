#ifndef NOTEDISPLAY_MFC_H
#define NOTEDISPLAY_MFC_H

#include <stddef.h>
#include <stdint.h>

struct mfc_info {
	int width, height;		/* image */
	int buf_width, buf_height;	/* buffer (aligned to tiles) */
	int dpb;			/* reference frames used by the decoder */
};

struct mfc_frame {
	int status;			/* DISP_S_*: 0 decode only, 1 decode+display, 2 display only, 3 end */
	unsigned int tag;		/* frametag of the shown frame */
	unsigned int y_phys, c_phys;	/* physical luma/chroma addresses (64x32 tiled NV12) */
};

int mfc_open(void);
int mfc_init(const uint8_t *au, size_t len, struct mfc_info *info);
int mfc_decode(const uint8_t *au, size_t len, unsigned int tag, struct mfc_frame *out);
void mfc_close(void);

#endif

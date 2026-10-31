/*
 * Decodifica H.264 con il decoder hardware MFC v5 dell'Exynos 4412, tramite le ioctl
 * del driver Samsung mfc5x (/dev/s3c-mfc) del kernel 3.0. Strutture e costanti vengono
 * dagli header del driver in ../ref (LineageOS android_kernel_samsung_smdk4412, cm-14.1).
 *
 * Sequenza: mmap del device -> GET_IN_BUF (buffer stream) -> GET_REAL_ADDR (fisico)
 * -> DEC_INIT con SPS+PPS+IDR -> DEC_EXE per ogni access unit.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mfc_interface.h"
#include "mfc.h"

#define STRM_BUF_SIZE	MAX_DECODER_INPUT_BUFFER_SIZE

static int fd = -1;
static uint8_t *mapped;
static size_t mapped_size;
static uint8_t *strm_virt;
static unsigned int strm_phys;

static int mfc_ioctl(unsigned long req, struct mfc_common_args *arg, const char *what)
{
	int r = ioctl(fd, req, arg);

	if (r < 0 || arg->ret_code != MFC_OK) {
		fprintf(stderr, "mfc: %s fallita (ret %d, codice %d, errno %d)\n",
			what, r, arg->ret_code, errno);
		return -1;
	}
	return r;
}

int mfc_open(void)
{
	struct mfc_common_args a;

	fd = open("/dev/s3c-mfc", O_RDWR | O_NDELAY);
	if (fd < 0) {
		perror("mfc: open /dev/s3c-mfc");
		return -1;
	}
	/* GET_MMAP_SIZE restituisce la dimensione come valore di ritorno dell'ioctl */
	memset(&a, 0, sizeof(a));
	int size = ioctl(fd, IOCTL_MFC_GET_MMAP_SIZE, &a);

	if (size <= 0) {
		fprintf(stderr, "mfc: GET_MMAP_SIZE fallita (%d)\n", size);
		return -1;
	}
	mapped_size = size;
	mapped = mmap(NULL, mapped_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapped == MAP_FAILED) {
		perror("mfc: mmap");
		return -1;
	}

	memset(&a, 0, sizeof(a));
	a.args.mem_alloc.type = DECODER;
	a.args.mem_alloc.buff_size = STRM_BUF_SIZE;
	a.args.mem_alloc.buf_cache_type = NO_CACHE;
	a.args.mem_alloc.mapped_addr = (unsigned int)mapped;
	if (mfc_ioctl(IOCTL_MFC_GET_IN_BUF, &a, "GET_IN_BUF") < 0)
		return -1;
	unsigned int offset = a.args.mem_alloc.offset;

	strm_virt = mapped + offset;

	memset(&a, 0, sizeof(a));
	a.args.real_addr.key = offset;
	if (mfc_ioctl(IOCTL_MFC_GET_REAL_ADDR, &a, "GET_REAL_ADDR") < 0)
		return -1;
	strm_phys = a.args.real_addr.addr;
	printf("mfc: mmap %zu byte, stream buffer offset 0x%x fisico 0x%08x\n",
	       mapped_size, offset, strm_phys);
	return 0;
}

int mfc_init(const uint8_t *au, size_t len, struct mfc_info *info)
{
	struct mfc_common_args a;

	if (len > STRM_BUF_SIZE)
		return -1;
	memcpy(strm_virt, au, len);
	memset(&a, 0, sizeof(a));
	a.args.dec_init.in_codec_type = H264_DEC;
	a.args.dec_init.in_strm_buf = strm_phys;
	a.args.dec_init.in_strm_size = len;
	a.args.dec_init.in_packed_PB = 0;
	a.args.dec_init.in_mapped_addr = (unsigned int)mapped;
	if (mfc_ioctl(IOCTL_MFC_DEC_INIT, &a, "DEC_INIT") < 0)
		return -1;
	info->width = a.args.dec_init.out_frm_width;
	info->height = a.args.dec_init.out_frm_height;
	info->buf_width = a.args.dec_init.out_buf_width;
	info->buf_height = a.args.dec_init.out_buf_height;
	info->dpb = a.args.dec_init.out_dpb_cnt;
	return 0;
}

int mfc_decode(const uint8_t *au, size_t len, unsigned int tag, struct mfc_frame *out)
{
	struct mfc_common_args a;

	if (len > STRM_BUF_SIZE)
		return -1;
	memcpy(strm_virt, au, len);
	memset(&a, 0, sizeof(a));
	a.args.dec_exe.in_codec_type = H264_DEC;
	a.args.dec_exe.in_strm_buf = strm_phys;
	a.args.dec_exe.in_strm_size = len;
	a.args.dec_exe.in_frametag = tag;
	if (mfc_ioctl(IOCTL_MFC_DEC_EXE, &a, "DEC_EXE") < 0)
		return -1;
	out->status = a.args.dec_exe.out_display_status;
	out->tag = a.args.dec_exe.out_frametag_top;
	out->y_phys = a.args.dec_exe.out_display_Y_addr;
	out->c_phys = a.args.dec_exe.out_display_C_addr;
	return 0;
}

void mfc_close(void)
{
	if (mapped && mapped != MAP_FAILED)
		munmap(mapped, mapped_size);
	if (fd >= 0)
		close(fd);
	fd = -1;
	mapped = NULL;
}

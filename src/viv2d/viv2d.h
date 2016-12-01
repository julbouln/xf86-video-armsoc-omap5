#ifndef VIV2D_H
#define VIV2D_H

#include <stdint.h>
#include <xorg-server.h>
#include "xf86.h"
#include "xf86_OSproc.h"

#include "state.xml.h"
#include "state_2d.xml.h"
#include "cmdstream.xml.h"

#define VIV2D_MAX_RECTS 256

// experimental stuff
//#define RAW_ETNA_BO 1

#define VIV2D_DBG_MSG(fmt, ...)
/*#define VIV2D_DBG_MSG(fmt, ...) \
		do { xf86Msg(X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
*/

//#define VIV2D_UNSUPPORTED_MSG(fmt, ...)
#define VIV2D_UNSUPPORTED_MSG(fmt, ...) \
/*		do { xf86Msg(X_WARNING, fmt "\n",\
				##__VA_ARGS__); } while (0)
*/

//#define VIV2D_INFO_MSG(fmt, ...)
#define VIV2D_INFO_MSG(fmt, ...) \
		do { xf86Msg(X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)

//#define VIV2D_ERR_MSG(fmt, ...)
#define VIV2D_ERR_MSG(fmt, ...) \
		do { xf86Msg(X_ERROR, fmt "\n",\
				##__VA_ARGS__); } while (0)

#define ROP_BLACK 				0x00
#define ROP_NOT_SRC_AND_NOT_DST 0x11
#define ROP_NOT_SRC_AND_DST 	0x22
#define ROP_NOT_SRC				0x33
#define ROP_SRC_AND_NOT_DST 	0x44
#define ROP_NOT_DST				0x55
#define ROP_DST_XOR_SRC 		0x66
#define ROP_NOT_SRC_OR_NOT_DST	0x77
#define ROP_DST_AND_SRC 		0x88
#define ROP_NOT_SRC_XOR_DST		0x99
#define ROP_DST 				0xaa
#define ROP_NOT_SRC_OR_DST		0xbb
#define ROP_SRC 				0xcc
#define ROP_SRC_OR_NOT_DST		0xdd
#define ROP_DST_OR_SRC 			0xee
#define ROP_WHITE 				0xff

#ifdef RAW_ETNA_BO
/* 
	We let armsoc managing DRI2/DRI3. So we do want minimal management for etna_bo, 
	since we only instanciate once from armsoc_bo dmabuf before
	operations (Solid/Copy/Compose), and close it immediately after.
	I have some doubt about the libdrm etnaviv cache management, so we use some cache-free functions.
*/
struct etna_bo {
	struct etna_device *dev;
	void            *map;           /* userspace mmap'ing (if there is one) */
	uint32_t        size;
	uint32_t        handle;
	uint32_t        flags;
	uint32_t        name;           /* flink global handle (DRI2 name) */
	uint64_t        offset;
	uint32_t        refcnt; // atomic_t

	/* in the common case, a bo won't be referenced by more than a single
	 * command stream.  So to avoid looping over all the bo's in the
	 * reloc table to find the idx of a bo that might already be in the
	 * table, we cache the idx in the bo.  But in order to detect the
	 * slow-path where bo is ref'd in multiple streams, we also must track
	 * the current_stream for which the idx is valid.  See bo2idx().
	 */
	struct etna_cmd_stream *current_stream;
	uint32_t idx;

	int reuse;
	/* list excluded */
//	struct list_head list;   /* bucket-list entry */
//	time_t free_time;        /* time when added to bucket-list */
};

struct etna_device {
	int fd;
	uint32_t refcnt;
/* ... */
};

// etna_device_fd seems missing in libdrm_etna 2.4.73 ?
int raw_etna_device_fd(struct etna_device *dev)
{
   return dev->fd;
}

// bare minimal drm alloc
inline struct etna_bo *raw_etna_bo_alloc(struct etna_device *dev)
{
	struct etna_bo *mem;

	mem = calloc(1, sizeof *mem);
	if (mem) {
		mem->dev = dev;
		mem->refcnt = 1;
		mem->idx = -1;
		mem->reuse = 0;
	}

	return mem;
}

// etna_bo new without cache
struct etna_bo *raw_etna_bo_new(struct etna_device *dev, uint32_t size,
		uint32_t flags)
{
	struct etna_bo *bo;
	int ret;
	struct drm_etnaviv_gem_new req = {
			.flags = flags,
	};
	int dev_fd = raw_etna_device_fd(dev);

	req.size = size;
	ret = drmCommandWriteRead(dev_fd, DRM_ETNAVIV_GEM_NEW,
			&req, sizeof(req));
	if (ret)
		return NULL;


	bo = raw_etna_bo_alloc(dev);
	if (!bo) {
		struct drm_gem_close req = {
			.handle = req.handle,
		};

		drmIoctl(dev_fd, DRM_IOCTL_GEM_CLOSE, &req);

		return NULL;
	}

	bo->size = size;
	bo->flags = flags;
	bo->handle = req.handle;

	return bo;
}

// bare minimal drm etna_bo from dmabuf fd
// no cache management
inline struct etna_bo *raw_etna_bo_from_dmabuf(struct etna_device *dev, int fd)
{
	struct etna_bo *bo;
	int ret, size;
	uint32_t handle;

	int dev_fd=raw_etna_device_fd(dev);

	ret = drmPrimeFDToHandle(dev_fd, fd, &handle);
	if (ret) {
		return NULL;
	}

	/* lseek() to get bo size */
	size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_CUR);

	bo = raw_etna_bo_alloc(dev);
	if (!bo) {
		struct drm_gem_close req = {
			.handle = handle,
		};

		drmIoctl(dev_fd, DRM_IOCTL_GEM_CLOSE, &req);

		return NULL;
	}
	bo->size = size;
	bo->handle = handle;

	return bo;
}

// bare minimal drm etna_bo del
// original etna_bo_del won't delete everytime because of a cache management
inline void raw_etna_bo_del(struct etna_bo *bo) {
	if (bo->map)
		munmap(bo->map, bo->size);

	if (bo->handle) {
		int dev_fd=raw_etna_device_fd(bo->dev);
		struct drm_gem_close req = {
			.handle = bo->handle,
		};

		drmIoctl(dev_fd, DRM_IOCTL_GEM_CLOSE, &req);
	}

	free(bo);
}
#endif

typedef struct _Viv2DRect {
	int x1;
	int y1;
	int x2;
	int y2;
} Viv2DRect;

typedef struct _Viv2DFormat {
	int exaFmt;
	int bpp;
	unsigned int fmt;
	int swizzle;
	int alphaBits;
} Viv2DFormat;

typedef struct {
	void *priv;			/* EXA submodule private data */
	struct etna_bo *bo;
	int dmaFD;
	int width;
	int height;
	int pitch;
	Viv2DFormat format;
	Bool tiled;

	struct ARMSOCPixmapPrivRec *armsocPix;
} Viv2DPixmapPrivRec, *Viv2DPixmapPrivPtr;

typedef struct _Viv2DBlendOp {
	int op;
	int srcBlendMode;
	int dstBlendMode;
} Viv2DBlendOp;

enum viv2d_src_type {
	viv2d_src_pix = 0,
	viv2d_src_1x1_repeat,
	viv2d_src_solid
};

typedef struct _Viv2DOp {
	Viv2DBlendOp *blend_op;
	Bool has_mask;
	int src_type;
	int msk_type;

	uint32_t fg;
	uint32_t mask;

	uint8_t src_alpha;
	uint8_t dst_alpha;
	Bool src_alpha_mode_global;
	Bool dst_alpha_mode_global;

	Viv2DPixmapPrivPtr src;
	Viv2DPixmapPrivPtr msk;
	Viv2DPixmapPrivPtr dst;

	Viv2DFormat msk_fmt;

	int prev_src_x;
	int prev_src_y;
	int cur_rect;
	Viv2DRect rects[VIV2D_MAX_RECTS];

} Viv2DOp;

typedef struct _Viv2DRec {
	int fd;
	char *render_node;
	struct etna_device *dev;
	struct etna_gpu *gpu;
	struct etna_pipe *pipe;
	struct etna_cmd_stream *stream;

	Viv2DOp *op;

	struct etna_bo *bo;
	int width;
	int height;

} Viv2DRec, *Viv2DPtr;

static inline Bool Viv2DSetFormat(unsigned int depth, unsigned int bpp, Viv2DFormat *fmt)
{
	fmt->bpp = bpp;
	fmt->swizzle = DE_SWIZZLE_ARGB;
	switch (bpp) {
	case 8:
		fmt->fmt = DE_FORMAT_A8;
		break;
	case 16:
		if (depth == 15)
			fmt->fmt = DE_FORMAT_A1R5G5B5;
		else
			fmt->fmt = DE_FORMAT_R5G6B5;
		break;
	case 32:
		if(depth == 24)
			fmt->fmt = DE_FORMAT_X8R8G8B8;
		else
			fmt->fmt = DE_FORMAT_A8R8G8B8;			
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

/*
 * There is a bug in the GPU hardware with destinations lacking alpha and
 * swizzles BGRA/RGBA.  Rather than the GPU treating bits 7:0 as alpha, it
 * continues to treat bits 31:24 as alpha.  This results in it replacing
 * the B or R bits on input to the blend operation with 1.0.  However, it
 * continues to accept the non-existent source alpha from bits 31:24.
 *
 * Work around this by switching to the equivalent alpha format, and using
 * global alpha to replace the alpha channel.  The alpha channel subsitution
 * is performed at this function's callsite.
 */
static inline Bool Viv2DFixNonAlpha(Viv2DFormat *fmt)
{
	switch (fmt->fmt) {
	case DE_FORMAT_X4R4G4B4:
		fmt->fmt = DE_FORMAT_A4R4G4B4;
		return TRUE;
	case DE_FORMAT_X1R5G5B5:
		fmt->fmt = DE_FORMAT_A1R5G5B5;
		return TRUE;
	case DE_FORMAT_X8R8G8B8:
		fmt->fmt = DE_FORMAT_A8R8G8B8;
		return TRUE;
	case DE_FORMAT_R5G6B5:
		return TRUE;
	}
	return FALSE;
}

static inline const char *Viv2DFormatColorStr(Viv2DFormat *fmt)
{
	switch (fmt->fmt) {
	case DE_FORMAT_A4R4G4B4:
		return "A4R4G4B4";
	case DE_FORMAT_X1R5G5B5:
		return "X1R5G5B5";
	case DE_FORMAT_A1R5G5B5:
		return "A1R5G5B5";
	case DE_FORMAT_R5G6B5:
		return "R5G6B5";
	case DE_FORMAT_X8R8G8B8:
		return "X8R8G8B8";
	case DE_FORMAT_A8R8G8B8:
		return "A8R8G8B8";
	case DE_FORMAT_X4R4G4B4:
		return "X4R4G4B4";
	case DE_FORMAT_A8:
		return "A8";
	default:
		return "UNKNOWN";
	}
}


static inline const char *Viv2DFormatSwizzleStr(Viv2DFormat *fmt)
{
	switch (fmt->swizzle) {
	case DE_SWIZZLE_ARGB:
		return "ARGB";
	case DE_SWIZZLE_RGBA:
		return "RGBA";
	case DE_SWIZZLE_ABGR:
		return "ABGR";
	case DE_SWIZZLE_BGRA:
		return "BGRA";
	default:
		return "UNKNOWN";
	}
}

static inline Viv2DPixmapPrivPtr Viv2DAllocPix(Viv2DRec *v2d,int width, int height, int pitch) {

	Viv2DPixmapPrivPtr vpix = calloc(1, sizeof *vpix);
	if (vpix) {
		vpix->width = width;
		vpix->height = height;
		vpix->pitch = pitch;
//		vpix->bo = etna_bo_new(v2d->dev, pitch*height,ETNA_BO_UNCACHED);
	}
	return vpix;
}

#endif
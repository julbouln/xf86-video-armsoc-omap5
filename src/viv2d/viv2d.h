#ifndef VIV2D_H
#define VIV2D_H

#include <stdint.h>
#include <xorg-server.h>
#include "xf86.h"
#include "xf86_OSproc.h"

#include "state.xml.h"
#include "state_2d.xml.h"
#include "cmdstream.xml.h"

#define VIV2D_STREAM_SIZE 1024*4
#define VIV2D_MAX_RECTS 256
#define VIV2D_MAX_TMP_PIX 1024
#define VIV2D_PITCH_ALIGN 32

#define VIV2D_CACHE_BO 1
#define VIV2D_CACHE_SIZE 1024*4

#define ALIGN(val, align)	(((val) + (align) - 1) & ~((align) - 1))

#define VIV2D_DBG_MSG(fmt, ...)
/*#define VIV2D_DBG_MSG(fmt, ...)		\
		do { xf86Msg(X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
*/
#define VIV2D_UNSUPPORTED_MSG(fmt, ...)
/*#define VIV2D_UNSUPPORTED_MSG(fmt, ...) \
		do { xf86Msg(X_WARNING, fmt "\n",\
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

typedef struct _Viv2DRect {
	int x1;
	int y1;
	int x2;
	int y2;
} Viv2DRect;

typedef struct _Viv2DFormat {
	int exaFmt;
	int bpp;
	int depth;
	unsigned int fmt;
	int swizzle;
	int alphaBits;
} Viv2DFormat;

typedef struct _Viv2DBoCacheEntry {
	struct etna_bo *bo;
	int size;
	int used;
} Viv2DBoCacheEntry;

typedef struct {
	struct etna_bo *bo;
	int width;
	int height;
	int pitch;
	Viv2DFormat format;
	Bool tiled;

	struct ARMSOCPixmapPrivRec *armsocPix; // armsoc pixmap ref
	int refcnt;
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

	uint8_t msk_alpha;
	uint8_t src_alpha;
	uint8_t dst_alpha;
	Bool msk_alpha_mode_global;
	Bool src_alpha_mode_global;
	Bool dst_alpha_mode_global;

	Viv2DPixmapPrivPtr src;
	Viv2DPixmapPrivPtr msk;
	Viv2DPixmapPrivPtr dst;

	Viv2DFormat msk_fmt;
	Viv2DFormat src_fmt;

	int prev_src_x;
	int prev_src_y;
	int prev_width;
	int prev_height;
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

	Viv2DOp op;

	struct etna_bo *bo;
	int width;
	int height;

	Viv2DPixmapPrivRec tmp_pix[VIV2D_MAX_TMP_PIX];
	int tmp_pix_cnt;

#ifdef VIV2D_CACHE_BO
	Viv2DBoCacheEntry cache[VIV2D_CACHE_SIZE];
#endif
} Viv2DRec, *Viv2DPtr;


// for debug
static inline const char *pix_op_name(int op) {
	switch (op) {
	case PictOpClear:
	case PictOpSrc: return "OpSrc";
	case PictOpDst: return "OpDst";
	case PictOpOver: return "OpOver";
	case PictOpOverReverse: return "OpOverReverse";
	case PictOpIn: return "OpIn";
	case PictOpInReverse: return "OpInReverse";
	case PictOpOut: return "OpOut";
	case PictOpOutReverse: return "OpOutReverse";
	case PictOpAtop: return "OpAtop";
	case PictOpAtopReverse: return "OpAtopReverse";
	case PictOpXor: return "OpXor";
	case PictOpAdd: return "OpAdd";
	case PictOpSaturate: return "OpSaturate";
	default: return "OpUnknown";
	}
}

static inline const char *pix_format_name(pixman_format_code_t format)
{
	switch (format)
	{
	/* 32bpp formats */
	case PIXMAN_a8r8g8b8: return "a8r8g8b8";
	case PIXMAN_x8r8g8b8: return "x8r8g8b8";
	case PIXMAN_a8b8g8r8: return "a8b8g8r8";
	case PIXMAN_x8b8g8r8: return "x8b8g8r8";
	case PIXMAN_b8g8r8a8: return "b8g8r8a8";
	case PIXMAN_b8g8r8x8: return "b8g8r8x8";
	case PIXMAN_r8g8b8a8: return "r8g8b8a8";
	case PIXMAN_r8g8b8x8: return "r8g8b8x8";
	case PIXMAN_x14r6g6b6: return "x14r6g6b6";
	case PIXMAN_x2r10g10b10: return "x2r10g10b10";
	case PIXMAN_a2r10g10b10: return "a2r10g10b10";
	case PIXMAN_x2b10g10r10: return "x2b10g10r10";
	case PIXMAN_a2b10g10r10: return "a2b10g10r10";

	/* sRGB formats */
	case PIXMAN_a8r8g8b8_sRGB: return "a8r8g8b8_sRGB";

	/* 24bpp formats */
	case PIXMAN_r8g8b8: return "r8g8b8";
	case PIXMAN_b8g8r8: return "b8g8r8";

	/* 16bpp formats */
	case PIXMAN_r5g6b5: return "r5g6b5";
	case PIXMAN_b5g6r5: return "b5g6r5";

	case PIXMAN_a1r5g5b5: return "a1r5g5b5";
	case PIXMAN_x1r5g5b5: return "x1r5g5b5";
	case PIXMAN_a1b5g5r5: return "a1b5g5r5";
	case PIXMAN_x1b5g5r5: return "x1b5g5r5";
	case PIXMAN_a4r4g4b4: return "a4r4g4b4";
	case PIXMAN_x4r4g4b4: return "x4r4g4b4";
	case PIXMAN_a4b4g4r4: return "a4b4g4r4";
	case PIXMAN_x4b4g4r4: return "x4b4g4r4";

	/* 8bpp formats */
	case PIXMAN_a8: return "a8";
	case PIXMAN_r3g3b2: return "r3g3b2";
	case PIXMAN_b2g3r3: return "b2g3r3";
	case PIXMAN_a2r2g2b2: return "a2r2g2b2";
	case PIXMAN_a2b2g2r2: return "a2b2g2r2";

#if 0
	case PIXMAN_x4c4: return "x4c4";
	case PIXMAN_g8: return "g8";
#endif
	case PIXMAN_c8: return "x4c4 / c8";
	case PIXMAN_x4g4: return "x4g4 / g8";

	case PIXMAN_x4a4: return "x4a4";

	/* 4bpp formats */
	case PIXMAN_a4: return "a4";
	case PIXMAN_r1g2b1: return "r1g2b1";
	case PIXMAN_b1g2r1: return "b1g2r1";
	case PIXMAN_a1r1g1b1: return "a1r1g1b1";
	case PIXMAN_a1b1g1r1: return "a1b1g1r1";

	case PIXMAN_c4: return "c4";
	case PIXMAN_g4: return "g4";

	/* 1bpp formats */
	case PIXMAN_a1: return "a1";

	case PIXMAN_g1: return "g1";

	/* YUV formats */
	case PIXMAN_yuy2: return "yuy2";
	case PIXMAN_yv12: return "yv12";
	};


	return "<unknown format>";
};

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
	case DE_FORMAT_MONOCHROME:
		return "A1";

	case DE_FORMAT_UYVY:
		return "UYVY";
	case DE_FORMAT_YUY2:
		return "YUY2";
	case DE_FORMAT_YV12:
		return "YV12";
	
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

#endif

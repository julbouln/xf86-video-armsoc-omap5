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

#define VIV2D_DBG_MSG(fmt, ...)
/*#define VIV2D_DBG_MSG(fmt, ...) \
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
	unsigned int fmt;
	int swizzle;
	int alphaBits;
} Viv2DFormat;

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

	uint8_t src_alpha;
	uint8_t dst_alpha;
	Bool src_alpha_mode_global;
	Bool dst_alpha_mode_global;

	Viv2DPixmapPrivPtr src;
	Viv2DPixmapPrivPtr msk;
	Viv2DPixmapPrivPtr dst;

	Viv2DPixmapPrivRec tmp;

	Viv2DFormat msk_fmt;

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

	Viv2DOp *op;

	struct etna_bo *bo;
	int width;
	int height;

} Viv2DRec, *Viv2DPtr;

#endif
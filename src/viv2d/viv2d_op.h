#ifndef VIV2D_OP_H
#define VIV2D_OP_H
#include "etnaviv_drmif.h"
#include "etnaviv_drm.h"

#include "state.xml.h"
#include "state_2d.xml.h"
#include "cmdstream.xml.h"

#include "viv2d.h"

// etnaviv utils

static inline void etna_emit_load_state(struct etna_cmd_stream *stream,
                                        const uint16_t offset, const uint16_t count)
{
	uint32_t v;
	v = 	(VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE | VIV_FE_LOAD_STATE_HEADER_OFFSET(offset) |
	         (VIV_FE_LOAD_STATE_HEADER_COUNT(count) & VIV_FE_LOAD_STATE_HEADER_COUNT__MASK));
	etna_cmd_stream_emit(stream, v);
}

static inline void etna_set_state(struct etna_cmd_stream *stream, uint32_t address, uint32_t value)
{
//	etna_cmd_stream_reserve(stream, 2);
	etna_emit_load_state(stream, address >> 2, 1);
	etna_cmd_stream_emit(stream, value);
}

static inline void etna_set_state_from_bo(struct etna_cmd_stream *stream,
        uint32_t address, struct etna_bo *bo)
{
//	etna_cmd_stream_reserve(stream, 2);
	etna_emit_load_state(stream, address >> 2, 1);
	etna_cmd_stream_reloc(stream, &(struct etna_reloc) {
		.bo = bo,
		 .flags = ETNA_RELOC_READ,
		  .offset = 0,
	});
}

static inline void _Viv2DOpAddRect(Viv2DOp *op, int x, int y, int width, int height) {
	Viv2DRect rect;
	rect.x1 = x;
	rect.y1 = y;
	rect.x2 = x + width;
	rect.y2 = y + height;
	op->rects[op->cur_rect] = rect;
	op->cur_rect++;
}

static inline Viv2DOp *_Viv2DOpCreate(void) {
	Viv2DOp *op = (Viv2DOp *)calloc(sizeof(*op), 1);

	op->blend_op = NULL;
	op->prev_src_x = -1;
	op->prev_src_y = -1;
	op->cur_rect = 0;

	return op;
}

static inline void _Viv2DOpDestroy(Viv2DOp *op) {
	free(op);
}

static inline void _Viv2DStreamReserve(struct etna_cmd_stream *stream, size_t n)
{
	if (etna_cmd_stream_avail(stream) < n) {
		VIV2D_DBG_MSG("_Viv2DStreamReserve %d < %d (%d)", etna_cmd_stream_avail(stream), n, stream->offset);
		etna_cmd_stream_flush(stream);
//		etna_cmd_stream_finish(stream);
	}
}

static inline void _Viv2DStreamReserveComp(struct etna_cmd_stream *stream, int src_type, int cur_rect, Bool blend) {
	int reserve = 0;
	switch (src_type) {
	case viv2d_src_1x1_repeat:
		reserve += 12 + 4;
		break;
	case viv2d_src_solid:
		reserve += 8;
		break;
	default:
		reserve += 12;
		break;
	}
	reserve += 14; // dest
	if (blend)
		reserve += 10; // blend
	else
		reserve += 2;
	reserve += cur_rect * 2 + 2;

	_Viv2DStreamReserve(stream, reserve);
}

static inline void _Viv2DStreamCommit(Viv2DPtr v2d) {
//	VIV2D_INFO_MSG("_Viv2DStreamCommit %d",v2d->stream->offset);
	/*
		_Viv2DStreamReserve(v2d->stream,40);
		int i;

		for (i = 0; i < 20; i++) {
			etna_cmd_stream_emit(v2d->stream, VIV_FE_NOP_HEADER_OP_NOP);
			etna_cmd_stream_emit(v2d->stream, 0);
		}
	*/
//	etna_cmd_stream_finish(v2d->stream);
	etna_cmd_stream_flush(v2d->stream);
}

static inline void _Viv2DStreamFlush(Viv2DPtr v2d) {
	etna_cmd_stream_flush(v2d->stream);
}


static inline uint32_t Viv2DSrcConfig(Viv2DFormat *format) {
	uint32_t src_cfg = VIVS_DE_SRC_CONFIG_SOURCE_FORMAT(format->fmt) |
	                   VIVS_DE_SRC_CONFIG_SWIZZLE(format->swizzle) |
	                   VIVS_DE_SRC_CONFIG_LOCATION_MEMORY |
	                   VIVS_DE_SRC_CONFIG_PE10_SOURCE_FORMAT(format->fmt);

	return src_cfg;
}
static inline void _Viv2DStreamSrcWithFormat(Viv2DPtr v2d, Viv2DPixmapPrivPtr src, int srcX, int srcY, int width, int height, Viv2DFormat *format) {
//	_Viv2DStreamReserve(v2d->stream, 12);
	etna_set_state_from_bo(v2d->stream, VIVS_DE_SRC_ADDRESS, src->bo);
	etna_set_state(v2d->stream, VIVS_DE_SRC_STRIDE, src->pitch);
	etna_set_state(v2d->stream, VIVS_DE_SRC_ROTATION_CONFIG, 0);
	etna_set_state(v2d->stream, VIVS_DE_SRC_CONFIG, Viv2DSrcConfig(format));
	etna_set_state(v2d->stream, VIVS_DE_SRC_ORIGIN,
	               VIVS_DE_SRC_ORIGIN_X(srcX) |
	               VIVS_DE_SRC_ORIGIN_Y(srcY));
	etna_set_state(v2d->stream, VIVS_DE_SRC_SIZE,
	               VIVS_DE_SRC_SIZE_X(width) |
	               VIVS_DE_SRC_SIZE_Y(height)
	              ); // source size is ignored
	VIV2D_DBG_MSG("_Viv2DStreamSrcWithFormat src:%p x:%d y:%d width:%d height:%d fmt:%s/%s",
	              src, srcX, srcY, width, height, Viv2DFormatColorStr(format), Viv2DFormatSwizzleStr(format));
}
static inline void _Viv2DStreamSrc(Viv2DPtr v2d, Viv2DPixmapPrivPtr src, int srcX, int srcY, int width, int height) {
	_Viv2DStreamSrcWithFormat( v2d,  src,  srcX,  srcY,  width,  height, &src->format);
}

static inline void _Viv2DStreamDst(Viv2DPtr v2d, Viv2DPixmapPrivPtr dst, int cmd, Viv2DRect *clip) {
//	_Viv2DStreamReserve(v2d->stream, 14);

	etna_set_state_from_bo(v2d->stream, VIVS_DE_DEST_ADDRESS, dst->bo);
	etna_set_state(v2d->stream, VIVS_DE_DEST_STRIDE, dst->pitch);
	etna_set_state(v2d->stream, VIVS_DE_DEST_ROTATION_CONFIG, 0);
	etna_set_state(v2d->stream, VIVS_DE_DEST_CONFIG,
	               VIVS_DE_DEST_CONFIG_FORMAT(dst->format.fmt) |
	               VIVS_DE_DEST_CONFIG_SWIZZLE(dst->format.swizzle) |
	               cmd |
	               VIVS_DE_DEST_CONFIG_TILED_DISABLE |
	               VIVS_DE_DEST_CONFIG_MINOR_TILED_DISABLE
	              );
	etna_set_state(v2d->stream, VIVS_DE_ROP,
	               VIVS_DE_ROP_ROP_FG(ROP_SRC) | VIVS_DE_ROP_ROP_BG(ROP_SRC) | VIVS_DE_ROP_TYPE_ROP4);

	if (clip) {
		etna_set_state(v2d->stream, VIVS_DE_CLIP_TOP_LEFT,
		               VIVS_DE_CLIP_TOP_LEFT_X(clip->x1) |
		               VIVS_DE_CLIP_TOP_LEFT_Y(clip->y1)
		              );
		etna_set_state(v2d->stream, VIVS_DE_CLIP_BOTTOM_RIGHT,
		               VIVS_DE_CLIP_BOTTOM_RIGHT_X(clip->x2) |
		               VIVS_DE_CLIP_BOTTOM_RIGHT_Y(clip->y2)
		              );

	} else {
		etna_set_state(v2d->stream, VIVS_DE_CLIP_TOP_LEFT,
		               VIVS_DE_CLIP_TOP_LEFT_X(0) |
		               VIVS_DE_CLIP_TOP_LEFT_Y(0)
		              );
		etna_set_state(v2d->stream, VIVS_DE_CLIP_BOTTOM_RIGHT,
		               VIVS_DE_CLIP_BOTTOM_RIGHT_X(dst->width) |
		               VIVS_DE_CLIP_BOTTOM_RIGHT_Y(dst->height)
		              );
	}
	VIV2D_DBG_MSG("_Viv2DStreamDst dst:%p fmt:%s/%s",
	              dst, Viv2DFormatColorStr(&dst->format), Viv2DFormatSwizzleStr(&dst->format));
}

static inline void _Viv2DStreamStretch(Viv2DPtr v2d, Viv2DPixmapPrivPtr src, Viv2DPixmapPrivPtr dst) {
//	_Viv2DStreamReserve(v2d->stream, 4);
	etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_LOW,
	               VIVS_DE_STRETCH_FACTOR_LOW_X(((src->width - 1) << 16) / (dst->width - 1)));
	etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_HIGH,
	               VIVS_DE_STRETCH_FACTOR_HIGH_Y(((src->height - 1) << 16) / (dst->height - 1)));

}

static inline void _Viv2DStreamRects(Viv2DPtr v2d, Viv2DRect *rects, int cur_rect) {
	if (cur_rect > 0) {
		int i;
//	VIV2D_INFO_MSG("stream rects cur_rect:%d",cur_rect);
//		_Viv2DStreamReserve(v2d->stream, cur_rect * 2 + 2);
		etna_cmd_stream_emit(v2d->stream,
		                     VIV_FE_DRAW_2D_HEADER_OP_DRAW_2D |
		                     VIV_FE_DRAW_2D_HEADER_COUNT(cur_rect)
		                    );
		etna_cmd_stream_emit(v2d->stream, 0x0); /* rectangles start aligned */

		for (i = 0; i < cur_rect; i++) {
			Viv2DRect tmprect = rects[i];
			etna_cmd_stream_emit(v2d->stream, VIV_FE_DRAW_2D_TOP_LEFT_X(tmprect.x1) |
			                     VIV_FE_DRAW_2D_TOP_LEFT_Y(tmprect.y1));
			etna_cmd_stream_emit(v2d->stream, VIV_FE_DRAW_2D_BOTTOM_RIGHT_X(tmprect.x2) |
			                     VIV_FE_DRAW_2D_BOTTOM_RIGHT_Y(tmprect.y2));
		}
	} else {
//		VIV2D_ERR_MSG("empty cur_rect!");

	}
}

static inline void _Viv2DStreamBlendOp(Viv2DPtr v2d, Viv2DBlendOp *blend_op, uint8_t src_alpha, uint8_t dst_alpha, Bool src_global, Bool dst_global) {
	if (blend_op) {
		uint32_t alpha_mode = VIVS_DE_ALPHA_MODES_GLOBAL_SRC_ALPHA_MODE_NORMAL |
		                      VIVS_DE_ALPHA_MODES_GLOBAL_DST_ALPHA_MODE_NORMAL;

		if (src_global)
			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_SRC_ALPHA_MODE_GLOBAL;

		if (dst_global)
			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_DST_ALPHA_MODE_GLOBAL;

//		_Viv2DStreamReserve(v2d->stream, 10);
		etna_set_state(v2d->stream, VIVS_DE_ALPHA_CONTROL,
		               VIVS_DE_ALPHA_CONTROL_ENABLE_ON |
		               VIVS_DE_ALPHA_CONTROL_PE10_GLOBAL_SRC_ALPHA(src_alpha) |
		               VIVS_DE_ALPHA_CONTROL_PE10_GLOBAL_DST_ALPHA(dst_alpha));

		etna_set_state(v2d->stream, VIVS_DE_ALPHA_MODES,
		               alpha_mode |
		               VIVS_DE_ALPHA_MODES_SRC_BLENDING_MODE(blend_op->srcBlendMode) |
		               VIVS_DE_ALPHA_MODES_DST_BLENDING_MODE(blend_op->dstBlendMode));

		etna_set_state(v2d->stream, VIVS_DE_GLOBAL_SRC_COLOR, src_alpha << 24);
		etna_set_state(v2d->stream, VIVS_DE_GLOBAL_DEST_COLOR, dst_alpha << 24);

		etna_set_state(v2d->stream, VIVS_DE_COLOR_MULTIPLY_MODES, /* PE20 */
		               VIVS_DE_COLOR_MULTIPLY_MODES_SRC_PREMULTIPLY_DISABLE |
		               VIVS_DE_COLOR_MULTIPLY_MODES_DST_PREMULTIPLY_DISABLE |
		               VIVS_DE_COLOR_MULTIPLY_MODES_SRC_GLOBAL_PREMULTIPLY_DISABLE |
		               VIVS_DE_COLOR_MULTIPLY_MODES_DST_DEMULTIPLY_DISABLE);

		VIV2D_DBG_MSG("_Viv2DStreamBlendOp op:%s", pix_op_name(blend_op->op));

	} else {
//		_Viv2DStreamReserve(v2d->stream, 2);
		etna_set_state(v2d->stream, VIVS_DE_ALPHA_CONTROL,
		               VIVS_DE_ALPHA_CONTROL_ENABLE_OFF);
		VIV2D_DBG_MSG("_Viv2DStreamBlendOp disabled");
	}
}

static inline void _Viv2DStreamColor(Viv2DPtr v2d, unsigned int color) {
//	_Viv2DStreamReserve(v2d->stream, 8);
	/* Clear color PE20 */
	etna_set_state(v2d->stream, VIVS_DE_CLEAR_PIXEL_VALUE32, color );
	/* Clear color PE10 */
	etna_set_state(v2d->stream, VIVS_DE_CLEAR_BYTE_MASK, 0xff);
	etna_set_state(v2d->stream, VIVS_DE_CLEAR_PIXEL_VALUE_LOW, color);
	etna_set_state(v2d->stream, VIVS_DE_CLEAR_PIXEL_VALUE_HIGH, color);
	VIV2D_DBG_MSG("_Viv2DStreamColor color:%x", color);
}

#endif
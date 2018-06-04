#ifndef VIV2D_OP_H
#define VIV2D_OP_H

#include "etnaviv_drmif.h"
#include "etnaviv_drm.h"

#include "state.xml.h"
#include "state_2d.xml.h"
#include "cmdstream.xml.h"

#include "viv2d.h"

#define ETNAVIV_WAIT_PIPE_MS 1000

#define PAGE_SHIFT      12
#define PAGE_SIZE       (1UL << PAGE_SHIFT)
#define PAGE_MASK       (~(PAGE_SIZE-1))
#define PAGE_ALIGN(addr)        (((addr)+PAGE_SIZE-1)&PAGE_MASK)

extern void* neon_memcpy(void* dest, const void* source, unsigned int numBytes);
extern void* neon_memset(void * ptr, int value, size_t num);

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
	etna_emit_load_state(stream, address >> 2, 1);
	etna_cmd_stream_emit(stream, value);
}

static inline void etna_set_state_from_bo(struct etna_cmd_stream *stream,
        uint32_t address, struct etna_bo *bo, int flags)
{
	etna_emit_load_state(stream, address >> 2, 1);
	etna_cmd_stream_reloc(stream, &(struct etna_reloc) {
		.bo = bo,
		 .flags = flags,
		  .offset = 0,
	});
}

static inline void etna_set_state_multi(struct etna_cmd_stream *stream, uint32_t base, uint32_t num, const uint32_t *values)
{
	int i;
	if (num == 0) return;
	etna_emit_load_state(stream, base >> 2, num);
	for (i = 0; i < num; i++) {
		etna_cmd_stream_emit(stream, values[i]);
	}
}

static inline void etna_load_state(struct etna_cmd_stream *stream, uint32_t address, const uint16_t count) {
	stream->buffer[stream->offset++] = (VIV_FE_LOAD_STATE_HEADER_OP_LOAD_STATE |
	                                    VIV_FE_LOAD_STATE_HEADER_OFFSET((address) >> 2) |
	                                    VIV_FE_LOAD_STATE_HEADER_COUNT(count));
}

static inline void etna_add_state_from_bo(struct etna_cmd_stream *stream, struct etna_bo *bo, int flags) {
	etna_cmd_stream_reloc(stream, &(struct etna_reloc) {
		.bo = bo,
		 .flags = flags,
		  .offset = 0,
	});
}

static inline void etna_add_state(struct etna_cmd_stream *stream, uint32_t value) {
	stream->buffer[stream->offset++] = value;
}

#define VIV2D_SRC_RES 6
#define VIV2D_SRC_ORIGIN_RES 4

#define VIV2D_SRC_PIX_RES VIV2D_SRC_RES + VIV2D_SRC_ORIGIN_RES
#define VIV2D_SRC_EMPTY_RES 4 + VIV2D_SRC_ORIGIN_RES

#define VIV2D_SRC_SOLID_RES 2

#define VIV2D_SRC_BRUSH_FILL_RES 8
#define VIV2D_SRC_1X1_RES 16
#define VIV2D_DEST_RES 10
#define VIV2D_BLEND_ON_RES 8
#define VIV2D_BLEND_OFF_RES 2
#ifdef VIV2D_CACHE_FLUSH_OPS
#define VIV2D_CACHE_FLUSH_RES 2
#else
#define VIV2D_CACHE_FLUSH_RES 0
#endif
#define VIV2D_RECTS_RES(cnt) cnt*2+2

static inline Bool _Viv2DSetFormat(unsigned int depth, unsigned int bpp, Viv2DFormat *fmt)
{
	fmt->bpp = bpp;
	fmt->depth = depth;
	fmt->swizzle = DE_SWIZZLE_ARGB;
	switch (bpp) {
#ifdef VIV2D_SUPPORT_MONO
	case 1:
		fmt->fmt = DE_FORMAT_MONOCHROME;
		break;
#endif
	case 8:
		fmt->fmt = DE_FORMAT_A8;
		break;
	case 16:
		if (depth == 15)
			fmt->fmt = DE_FORMAT_X1R5G5B5;
		else
			fmt->fmt = DE_FORMAT_R5G6B5;
		break;
	case 32:
		if (depth == 24)
			fmt->fmt = DE_FORMAT_X8R8G8B8;
		else
			fmt->fmt = DE_FORMAT_A8R8G8B8;
		break;
	default:
		return FALSE;
	}

	return TRUE;
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

static inline void _Viv2DOpInit(Viv2DOp *op) {
	op->has_mask = FALSE;
	op->has_component_alpha = FALSE;
	op->blend_op = NULL;
	op->prev_src_x = -1;
	op->prev_src_y = -1;
	op->cur_rect = 0;
	op->dst = NULL;
	op->src = NULL;
	op->msk = NULL;
	op->fg = 0;
	op->mask = 0;
}

static inline int _Viv2DStreamWait(Viv2DPtr v2d) {
	etna_bo_cache_clean(v2d->dev);
//	VIV2D_DBG_MSG("_Viv2DStreamCommit pipe wait start");
	int ret = etna_pipe_wait(v2d->pipe, etna_cmd_stream_timestamp(v2d->stream), ETNAVIV_WAIT_PIPE_MS);
	if (ret != 0) {
		VIV2D_INFO_MSG("wait pipe failed");
	}
	return ret;
//	VIV2D_DBG_MSG("_Viv2DStreamCommit pipe wait end");
}

static inline void _Viv2DStreamCommit(Viv2DPtr v2d, Bool async) {
//	VIV2D_DBG_MSG("_Viv2DStreamCommit %d %d (%d)", async, etna_cmd_stream_avail(v2d->stream), v2d->stream->offset);
	if (etna_cmd_stream_offset(v2d->stream) > 0) {
		VIV2D_DBG_MSG("_Viv2DStreamCommit flush start %d (%d)", etna_cmd_stream_avail(v2d->stream), v2d->stream->offset);
		etna_cmd_stream_flush(v2d->stream);
//		VIV2D_DBG_MSG("_Viv2DStreamCommit flush end %d (%d)", etna_cmd_stream_avail(v2d->stream), v2d->stream->offset);
	}

	if (!async) {
		_Viv2DStreamWait(v2d);
	}
}

static inline void _Viv2DStreamReserve(Viv2DPtr v2d, size_t n)
{
	if (etna_cmd_stream_avail(v2d->stream) < n) {
		VIV2D_OP_DBG_MSG("_Viv2DStreamReserve %d < %d (%d)", etna_cmd_stream_avail(v2d->stream), n, v2d->stream->offset);
		etna_cmd_stream_flush(v2d->stream);
	}
}

static inline uint32_t Viv2DSrcConfig(Viv2DFormat *format) {
	uint32_t src_cfg = VIVS_DE_SRC_CONFIG_SOURCE_FORMAT(format->fmt) |
	                   VIVS_DE_SRC_CONFIG_SWIZZLE(format->swizzle) |
	                   VIVS_DE_SRC_CONFIG_LOCATION_MEMORY |
	                   VIVS_DE_SRC_CONFIG_PE10_SOURCE_FORMAT(format->fmt);

	return src_cfg;
}

static inline void _Viv2DStreamSrcWithFormat(Viv2DPtr v2d, Viv2DPixmapPrivPtr src, Viv2DFormat *format) {
//	_Viv2DStreamReserve(v2d, 8);
#if 1
	etna_set_state_from_bo(v2d->stream, VIVS_DE_SRC_ADDRESS, src->bo, ETNA_RELOC_READ);
	etna_load_state(v2d->stream, VIVS_DE_SRC_STRIDE, 3);
	etna_add_state(v2d->stream, src->pitch); // VIVS_DE_SRC_STRIDE
	etna_add_state(v2d->stream, VIVS_DE_SRC_ROTATION_CONFIG_ROTATION_DISABLE); // VIVS_DE_SRC_ROTATION_CONFIG
	etna_add_state(v2d->stream, Viv2DSrcConfig(format)); // VIVS_DE_SRC_CONFIG
#endif
#if 0
//	_Viv2DStreamReserve(v2d->stream, 12);
	etna_set_state_from_bo(v2d->stream, VIVS_DE_SRC_ADDRESS, src->bo, ETNA_RELOC_READ);
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
#endif
//	VIV2D_DBG_MSG("_Viv2DStreamSrcWithFormat src:%p x:%d y:%d width:%d height:%d fmt:%s/%s",
//	              src, srcX, srcY, width, height, Viv2DFormatColorStr(format), Viv2DFormatSwizzleStr(format));
}

static inline void _Viv2DStreamSrc(Viv2DPtr v2d, Viv2DPixmapPrivPtr src) {
	_Viv2DStreamSrcWithFormat( v2d,  src, &src->format);
}

static inline void _Viv2DStreamSrcOrigin(Viv2DPtr v2d, int srcX, int srcY, int width, int height) {
	etna_set_state(v2d->stream, VIVS_DE_SRC_ORIGIN, VIVS_DE_SRC_ORIGIN_X(srcX) | VIVS_DE_SRC_ORIGIN_Y(srcY)); // VIVS_DE_SRC_ORIGIN
	etna_set_state(v2d->stream, VIVS_DE_SRC_SIZE, VIVS_DE_SRC_SIZE_X(width) | VIVS_DE_SRC_SIZE_Y(height)); // VIVS_DE_SRC_SIZE
}


static inline void _Viv2DStreamEmptySrc(Viv2DPtr v2d) {
	etna_load_state(v2d->stream, VIVS_DE_SRC_STRIDE, 3);
	etna_add_state(v2d->stream, 0); // VIVS_DE_SRC_STRIDE
	etna_add_state(v2d->stream, 0); // VIVS_DE_SRC_ROTATION_CONFIG
	etna_add_state(v2d->stream, 0); // VIVS_DE_SRC_CONFIG
#if 0
//	_Viv2DStreamReserve(v2d->stream, 10);
//	etna_set_state(v2d->stream, VIVS_DE_SRC_ADDRESS, 0);
	etna_set_state(v2d->stream, VIVS_DE_SRC_STRIDE, 0);
	etna_set_state(v2d->stream, VIVS_DE_SRC_ROTATION_CONFIG, 0);
	etna_set_state(v2d->stream, VIVS_DE_SRC_CONFIG, 0);
	etna_set_state(v2d->stream, VIVS_DE_SRC_ORIGIN,
	               VIVS_DE_SRC_ORIGIN_X(srcX) |
	               VIVS_DE_SRC_ORIGIN_Y(srcY));
	etna_set_state(v2d->stream, VIVS_DE_SRC_SIZE,
	               VIVS_DE_SRC_SIZE_X(width) |
	               VIVS_DE_SRC_SIZE_Y(height)
	              );
#endif
}

static inline void _Viv2DStreamDst(Viv2DPtr v2d, Viv2DPixmapPrivPtr dst, int cmd, int rop, Viv2DRect *clip) {
//	_Viv2DStreamReserve(v2d->stream, 14);
#if 1
	etna_set_state_from_bo(v2d->stream, VIVS_DE_DEST_ADDRESS, dst->bo, ETNA_RELOC_WRITE);
	etna_load_state(v2d->stream, VIVS_DE_DEST_STRIDE, 3);
	etna_add_state(v2d->stream, dst->pitch); // VIVS_DE_DEST_STRIDE
	etna_add_state(v2d->stream, 0); // VIVS_DE_DEST_ROTATION_CONFIG
	etna_add_state(v2d->stream,
	               VIVS_DE_DEST_CONFIG_FORMAT(dst->format.fmt) |
	               VIVS_DE_DEST_CONFIG_SWIZZLE(dst->format.swizzle) |
	               cmd |
	               VIVS_DE_DEST_CONFIG_TILED_DISABLE |
	               VIVS_DE_DEST_CONFIG_MINOR_TILED_DISABLE
	              ); // VIVS_DE_DEST_CONFIG

	etna_load_state(v2d->stream, VIVS_DE_ROP, 3);
	etna_add_state(v2d->stream,
	               VIVS_DE_ROP_ROP_FG(rop) | VIVS_DE_ROP_ROP_BG(rop) | VIVS_DE_ROP_TYPE_ROP4); // VIVS_DE_ROP

	if (clip) {
		etna_add_state(v2d->stream,
		               VIVS_DE_CLIP_TOP_LEFT_X(clip->x1) |
		               VIVS_DE_CLIP_TOP_LEFT_Y(clip->y1)
		              ); // VIVS_DE_CLIP_TOP_LEFT
		etna_add_state(v2d->stream,
		               VIVS_DE_CLIP_BOTTOM_RIGHT_X(clip->x2) |
		               VIVS_DE_CLIP_BOTTOM_RIGHT_Y(clip->y2)
		              ); // VIVS_DE_CLIP_BOTTOM_RIGHT

	} else {
		etna_add_state(v2d->stream,
		               VIVS_DE_CLIP_TOP_LEFT_X(0) |
		               VIVS_DE_CLIP_TOP_LEFT_Y(0)
		              ); // VIVS_DE_CLIP_TOP_LEFT
		etna_add_state(v2d->stream,
		               VIVS_DE_CLIP_BOTTOM_RIGHT_X(dst->width) |
		               VIVS_DE_CLIP_BOTTOM_RIGHT_Y(dst->height)
		              ); // VIVS_DE_CLIP_BOTTOM_RIGHT
	}
#endif
#if 0
	etna_set_state_from_bo(v2d->stream, VIVS_DE_DEST_ADDRESS, dst->bo, ETNA_RELOC_WRITE);
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
	               VIVS_DE_ROP_ROP_FG(rop) | VIVS_DE_ROP_ROP_BG(rop) | VIVS_DE_ROP_TYPE_ROP4);

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
#endif

	VIV2D_OP_DBG_MSG("_Viv2DStreamDst dst:%p fmt:%s/%s",
	              dst, Viv2DFormatColorStr(&dst->format), Viv2DFormatSwizzleStr(&dst->format));

}

static inline void _Viv2DStreamBrushFill(Viv2DPtr v2d, uint32_t color) {
//	_Viv2DStreamReserve(v2d, 10);
	/*	etna_set_state(v2d->stream, VIVS_DE_PATTERN_MASK_LOW, 0xffffffff);
		etna_set_state(v2d->stream, VIVS_DE_PATTERN_MASK_HIGH, 0xffffffff);
		etna_set_state(v2d->stream, VIVS_DE_PATTERN_BG_COLOR, 0);
		etna_set_state(v2d->stream, VIVS_DE_PATTERN_FG_COLOR, color);
	*/
	etna_load_state(v2d->stream, VIVS_DE_PATTERN_HIGH, 5);
	etna_add_state(v2d->stream, 0);
	etna_add_state(v2d->stream, 0xffffffff);
	etna_add_state(v2d->stream, 0xffffffff);
	etna_add_state(v2d->stream, 0);
	etna_add_state(v2d->stream, color);

	etna_set_state(v2d->stream, VIVS_DE_PATTERN_CONFIG, VIVS_DE_PATTERN_CONFIG_INIT_TRIGGER(3));
}


static inline void _Viv2DStreamStretch(Viv2DPtr v2d, Viv2DPixmapPrivPtr src, Viv2DPixmapPrivPtr dst) {
//	_Viv2DStreamReserve(v2d->stream, 4);

	etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_LOW,
	               VIVS_DE_STRETCH_FACTOR_LOW_X(((src->width) << 16) / (dst->width)));
	etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_HIGH,
	               VIVS_DE_STRETCH_FACTOR_HIGH_Y(((src->height) << 16) / (dst->height)));
	VIV2D_OP_DBG_MSG("_Viv2DStreamStretch %dx%d / %dx%d", src->width, src->height, dst->width, dst->height);

}

static inline void _Viv2DStreamRects(Viv2DPtr v2d, Viv2DRect *rects, int cur_rect) {
	if (cur_rect > 0) {
//	VIV2D_INFO_MSG("stream rects cur_rect:%d",cur_rect);
//		_Viv2DStreamReserve(v2d->stream, cur_rect * 2 + 2);
		etna_cmd_stream_emit(v2d->stream,
		                     VIV_FE_DRAW_2D_HEADER_OP_DRAW_2D |
		                     VIV_FE_DRAW_2D_HEADER_COUNT(cur_rect)
		                    );
		etna_cmd_stream_emit(v2d->stream, 0x0); /* rectangles start aligned */

		for (int i = 0; i < cur_rect; i++) {
			Viv2DRect tmprect = rects[i];
			etna_cmd_stream_emit(v2d->stream, VIV_FE_DRAW_2D_TOP_LEFT_X(tmprect.x1) |
			                     VIV_FE_DRAW_2D_TOP_LEFT_Y(tmprect.y1));
			etna_cmd_stream_emit(v2d->stream, VIV_FE_DRAW_2D_BOTTOM_RIGHT_X(tmprect.x2) |
			                     VIV_FE_DRAW_2D_BOTTOM_RIGHT_Y(tmprect.y2));
		}
	} else {
//		VIV2D_ERR_MSG("empty cur_rect!");

	}

	VIV2D_OP_DBG_MSG("_Viv2DStreamRects %d", cur_rect);
}

static inline void _Viv2DStreamBlendOp(Viv2DPtr v2d, Viv2DBlendOp *blend_op,
                                       Bool src_global, uint8_t src_alpha, Bool dst_global, uint8_t dst_alpha) {
	if (blend_op) {
		uint32_t alpha_mode = VIVS_DE_ALPHA_MODES_GLOBAL_SRC_ALPHA_MODE_NORMAL |
		                      VIVS_DE_ALPHA_MODES_GLOBAL_DST_ALPHA_MODE_NORMAL;

		uint32_t src_alpha_color = src_alpha;
		uint32_t dst_alpha_color = dst_alpha;

		uint32_t premultiply = VIVS_DE_COLOR_MULTIPLY_MODES_SRC_PREMULTIPLY_DISABLE |
		                       VIVS_DE_COLOR_MULTIPLY_MODES_DST_PREMULTIPLY_DISABLE |
		                       VIVS_DE_COLOR_MULTIPLY_MODES_SRC_GLOBAL_PREMULTIPLY_DISABLE |
		                       VIVS_DE_COLOR_MULTIPLY_MODES_DST_DEMULTIPLY_DISABLE;

		if (src_global) {
			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_SRC_ALPHA_MODE_GLOBAL;
//			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_SRC_ALPHA_MODE_SCALED;

		}
		if (dst_global) {
			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_DST_ALPHA_MODE_GLOBAL;
//			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_DST_ALPHA_MODE_SCALED;
		}

//		_Viv2DStreamReserve(v2d->stream, 10);
		etna_set_state(v2d->stream, VIVS_DE_ALPHA_CONTROL,
		               VIVS_DE_ALPHA_CONTROL_ENABLE_ON |
		               VIVS_DE_ALPHA_CONTROL_PE10_GLOBAL_SRC_ALPHA(src_alpha) |
		               VIVS_DE_ALPHA_CONTROL_PE10_GLOBAL_DST_ALPHA(dst_alpha));

		etna_set_state(v2d->stream, VIVS_DE_ALPHA_MODES,
		               alpha_mode |
		               VIVS_DE_ALPHA_MODES_SRC_BLENDING_MODE(blend_op->src_blend_mode) |
		               VIVS_DE_ALPHA_MODES_DST_BLENDING_MODE(blend_op->dst_blend_mode));

		etna_load_state(v2d->stream, VIVS_DE_GLOBAL_SRC_COLOR, 3);
		etna_add_state(v2d->stream, src_alpha_color << 24); // VIVS_DE_GLOBAL_SRC_COLOR
		etna_add_state(v2d->stream, dst_alpha_color << 24); // VIVS_DE_GLOBAL_DEST_COLOR
		etna_add_state(v2d->stream, /* PE20 */
		               premultiply); // VIVS_DE_COLOR_MULTIPLY_MODES


#if 0
		etna_set_state(v2d->stream, VIVS_DE_GLOBAL_SRC_COLOR, src_alpha_color << 24);
		etna_set_state(v2d->stream, VIVS_DE_GLOBAL_DEST_COLOR, dst_alpha_color << 24);

		etna_set_state(v2d->stream, VIVS_DE_COLOR_MULTIPLY_MODES, /* PE20 */
		               premultiply);
#endif
		VIV2D_OP_DBG_MSG("_Viv2DStreamBlendOp op:%s", pix_op_name(blend_op->op));

	} else {
//		_Viv2DStreamReserve(v2d->stream, 10);
		etna_set_state(v2d->stream, VIVS_DE_ALPHA_CONTROL,
		               VIVS_DE_ALPHA_CONTROL_ENABLE_OFF);
		/*		etna_set_state(v2d->stream, VIVS_DE_ALPHA_MODES, 0);
				etna_set_state(v2d->stream, VIVS_DE_GLOBAL_SRC_COLOR, 0);
				etna_set_state(v2d->stream, VIVS_DE_GLOBAL_DEST_COLOR, 0);
				etna_set_state(v2d->stream, VIVS_DE_COLOR_MULTIPLY_MODES, 0);
				*/
		VIV2D_OP_DBG_MSG("_Viv2DStreamBlendOp disabled");
	}
}

static inline void _Viv2DStreamColor(Viv2DPtr v2d, uint32_t color) {
//	_Viv2DStreamReserve(v2d->stream, 8);
	/* Clear color PE20 */
	etna_set_state(v2d->stream, VIVS_DE_CLEAR_PIXEL_VALUE32, color );
	/* Clear color PE10 */
	/*	etna_set_state(v2d->stream, VIVS_DE_CLEAR_BYTE_MASK, 0xff);
		etna_set_state(v2d->stream, VIVS_DE_CLEAR_PIXEL_VALUE_LOW, color);
		etna_set_state(v2d->stream, VIVS_DE_CLEAR_PIXEL_VALUE_HIGH, color);
	*/
	VIV2D_OP_DBG_MSG("_Viv2DStreamColor color:%x", color);
}

// higher level helpers

static inline void _Viv2DStreamCacheFlush(Viv2DPtr v2d) {
#ifdef VIV2D_CACHE_FLUSH_OPS
	etna_set_state(v2d->stream, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_PE2D);
#endif
}

static inline void _Viv2DStreamReserveComp(Viv2DPtr v2d, int src_type, int cur_rect, Bool blend) {
	int reserve = 0;
	switch (src_type) {
	case viv2d_src_1x1_repeat:
		reserve += VIV2D_SRC_1X1_RES;
		break;
	case viv2d_src_brush_fill:
		reserve += VIV2D_SRC_BRUSH_FILL_RES + VIV2D_SRC_EMPTY_RES;
		break;
	case viv2d_src_solid:
		reserve += VIV2D_SRC_SOLID_RES + VIV2D_SRC_EMPTY_RES;
		break;
	default:
		reserve += VIV2D_SRC_PIX_RES;
		break;
	}
	reserve += VIV2D_DEST_RES; // dest
	if (blend)
		reserve += VIV2D_BLEND_ON_RES; // blend
	else
		reserve += VIV2D_BLEND_OFF_RES;

	reserve += VIV2D_RECTS_RES(cur_rect);

	reserve += VIV2D_CACHE_FLUSH_RES;

	_Viv2DStreamReserve(v2d, reserve);
}

static inline void _Viv2DStreamSolid(Viv2DPtr v2d, Viv2DPixmapPrivPtr dst, uint32_t color, Viv2DRect *rects, int cur_rect) {
	_Viv2DStreamReserve(v2d, VIV2D_DEST_RES + VIV2D_BLEND_OFF_RES + VIV2D_SRC_SOLID_RES + VIV2D_SRC_EMPTY_RES + VIV2D_RECTS_RES(cur_rect) + VIV2D_CACHE_FLUSH_RES);
	_Viv2DStreamEmptySrc(v2d);
	_Viv2DStreamSrcOrigin(v2d, 0, 0, 0, 0);
	_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, ROP_SRC, NULL);
	_Viv2DStreamBlendOp(v2d, NULL, FALSE, 0, FALSE, 0); // reset blend
	_Viv2DStreamColor(v2d, color);
	_Viv2DStreamRects(v2d, rects, cur_rect);
	_Viv2DStreamCacheFlush(v2d);
}

static inline void _Viv2DStreamBrushSolid(Viv2DPtr v2d, Viv2DPixmapPrivPtr dst, uint32_t color, Viv2DRect *rects, int cur_rect) {
	_Viv2DStreamReserve(v2d, VIV2D_DEST_RES + VIV2D_BLEND_OFF_RES + VIV2D_SRC_BRUSH_FILL_RES + VIV2D_SRC_EMPTY_RES + VIV2D_RECTS_RES(cur_rect) + VIV2D_CACHE_FLUSH_RES);
	_Viv2DStreamEmptySrc(v2d);
	_Viv2DStreamSrcOrigin(v2d, 0, 0, 0, 0);
	_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, 0xf0, NULL);
	_Viv2DStreamBlendOp(v2d, NULL, FALSE, 0, FALSE, 0); // reset blend
	_Viv2DStreamBrushFill(v2d, color);
	_Viv2DStreamRects(v2d, rects, cur_rect);
	_Viv2DStreamCacheFlush(v2d);
}

static inline void _Viv2DStreamCompAlpha(Viv2DPtr v2d, int src_type, Viv2DPixmapPrivPtr src, Viv2DFormat *src_fmt, int color,
        Viv2DPixmapPrivPtr dst, Viv2DBlendOp *blend_op,
        Bool src_global, uint8_t src_alpha,
        Bool dst_global, uint8_t dst_alpha,
        int x, int y, int w, int h, Viv2DRect *rects, int cur_rect) {

	Bool blend = blend_op != NULL ? TRUE : FALSE;
	_Viv2DStreamReserveComp(v2d, src_type, cur_rect, blend);

	switch (src_type) {
	case viv2d_src_1x1_repeat:
		_Viv2DStreamSrcWithFormat(v2d, src, src_fmt);
		_Viv2DStreamSrcOrigin(v2d, 0, 0, 1, 1);
		_Viv2DStreamStretch(v2d, src, dst);
		_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_STRETCH_BLT, ROP_SRC, NULL);
		break;
	case viv2d_src_solid:
		_Viv2DStreamEmptySrc(v2d);
		_Viv2DStreamSrcOrigin(v2d, 0, 0, 0, 0);
		_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, ROP_SRC, NULL);
		_Viv2DStreamColor(v2d, color);
		break;
	case viv2d_src_brush_fill:
		_Viv2DStreamEmptySrc(v2d);
		_Viv2DStreamSrcOrigin(v2d, 0, 0, 0, 0);
		_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, 0xf0, NULL);
		_Viv2DStreamBrushFill(v2d, color);
		break;

	default:
		_Viv2DStreamSrcWithFormat(v2d, src, src_fmt);
		_Viv2DStreamSrcOrigin(v2d, x, y, w, h);
		_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, ROP_SRC, NULL);
		break;
	}

	_Viv2DStreamBlendOp(v2d, blend_op, src_global, src_alpha, dst_global, dst_alpha);
	_Viv2DStreamRects(v2d, rects, cur_rect);
	_Viv2DStreamCacheFlush(v2d);
}

static inline void _Viv2DStreamComp(Viv2DPtr v2d, int src_type, Viv2DPixmapPrivPtr src, Viv2DFormat *src_fmt, int color,
                                    Viv2DPixmapPrivPtr dst, Viv2DBlendOp *blend_op,
                                    int x, int y, int w, int h, Viv2DRect *rects, int cur_rect) {

	_Viv2DStreamCompAlpha(v2d, src_type, src, src_fmt, color, dst, blend_op, FALSE, 0, FALSE, 0, x, y, w, h, rects, cur_rect);
}


static inline void _Viv2DStreamClear(Viv2DPtr v2d, Viv2DPixmapPrivPtr pix) {
	if (pix && pix->bo) {
		Viv2DRect rect[1];
		rect[0].x1 = 0;
		rect[0].y1 = 0;
		rect[0].x2 = pix->width;
		rect[0].y2 = pix->height;

		_Viv2DStreamSolid(v2d, pix, 0xffffffff, rect, 1);
	}
}

static inline void _Viv2DOpDelTmpPix(Viv2DPtr v2d, Viv2DPixmapPrivPtr tmp) {
	etna_bo_cache_del(v2d->dev, tmp->bo);
	free(tmp);
}

static inline Viv2DPixmapPrivPtr _Viv2DOpCreateTmpPix(Viv2DPtr v2d, int width, int height, int bpp) {
	Viv2DPixmapPrivPtr tmp;
	int pitch;

	tmp = calloc(sizeof(*tmp), 1);
	pitch = ALIGN(width * ((bpp + 7) / 8), VIV2D_PITCH_ALIGN);
	tmp->bo = etna_bo_cache_new(v2d->dev, pitch * height, ETNA_BO_WC);

	VIV2D_OP_DBG_MSG("_Viv2DOpCreateTmpPix bo:%p %dx%d %d", tmp->bo, width, height, pitch * height);
	tmp->width = width;
	tmp->height = height;
	tmp->pitch = pitch;

	return tmp;
}

#ifdef VIV2D_TRACE
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* debug */
void _Viv2DPixToBmp(Viv2DPixmapPrivPtr pix, const char *filename) {
	stbi_write_png(filename, pix->width, pix->height, pix->format.bpp / 8, etna_bo_map(pix->bo), pix->pitch);
}

void _Viv2DPixTrace(Viv2DPixmapPrivPtr pix, const char *tag) {
	struct timeval te;

	char *autime, *s;
	char *tmpBuf;
	int len;

	len = 22 + 20 + 1 + 4 + 1 + 8 + 4 + 1;
	tmpBuf = malloc(len);
	if (!tmpBuf)
		return;

	gettimeofday(&te, NULL); // get current time
	long long milliseconds = te.tv_sec * 1000LL + te.tv_usec / 1000; // calculate milliseconds

	snprintf(tmpBuf, len, "/home/julbouln/traces/%020llu_%s_%08x.png", milliseconds, tag, pix);

	VIV2D_OP_DBG_MSG("_Viv2DPixTrace time:%020llu tag:%s pix:%p bo:%p", milliseconds, tag, pix, pix->bo);

	_Viv2DPixToBmp(pix, tmpBuf);
	free(tmpBuf);
}
#endif
#endif
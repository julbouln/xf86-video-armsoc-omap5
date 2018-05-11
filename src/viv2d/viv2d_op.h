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

static inline void etna_nop(struct etna_cmd_stream *stream) {
	etna_cmd_stream_emit(stream, VIV_FE_NOP_HEADER_OP_NOP);
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

static inline void etna_set_state_multi(struct etna_cmd_stream *stream, uint32_t base, uint32_t num, const uint32_t *values)
{
	int i;
	if (num == 0) return;
//	etna_cmd_stream_reserve(stream, 1 + num); /* 1 extra for potential alignment */
	etna_emit_load_state(stream, base >> 2, num);
	for (i = 0; i < num; i++) {
		etna_cmd_stream_emit(stream, values[i]);
	}

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

static inline void _Viv2DStreamWait(Viv2DPtr v2d) {
	VIV2D_DBG_MSG("_Viv2DStreamCommit pipe wait start");
	int ret = etna_pipe_wait(v2d->pipe, etna_cmd_stream_timestamp(v2d->stream), ETNAVIV_WAIT_PIPE_MS);
	if (ret != 0) {
		VIV2D_INFO_MSG("wait pipe failed");
	}
	VIV2D_DBG_MSG("_Viv2DStreamCommit pipe wait end");
}

static inline void _Viv2DStreamCommit(Viv2DPtr v2d, Bool async) {
	VIV2D_DBG_MSG("_Viv2DStreamCommit %d %d (%d)", async, etna_cmd_stream_avail(v2d->stream), v2d->stream->offset);
	if (etna_cmd_stream_offset(v2d->stream) > 0) {
//		for(int i=0;i<20;i++) {
//			etna_nop(v2d->stream);
//		}
		VIV2D_DBG_MSG("_Viv2DStreamCommit flush start %d (%d)", etna_cmd_stream_avail(v2d->stream), v2d->stream->offset);
		etna_cmd_stream_flush(v2d->stream);
		VIV2D_DBG_MSG("_Viv2DStreamCommit flush end %d (%d)", etna_cmd_stream_avail(v2d->stream), v2d->stream->offset);
	}

	if (!async) {
		_Viv2DStreamWait(v2d);
	}
}

static inline void _Viv2DStreamReserve(Viv2DPtr v2d, size_t n)
{
	if (etna_cmd_stream_avail(v2d->stream) < n) {
		VIV2D_DBG_MSG("_Viv2DStreamReserve %d < %d (%d)", etna_cmd_stream_avail(v2d->stream), n, v2d->stream->offset);
		etna_cmd_stream_flush(v2d->stream);
//		int ret = etna_pipe_wait(v2d->pipe, etna_cmd_stream_timestamp(v2d->stream), ETNAVIV_WAIT_PIPE_MS);
//		if (ret != 0) {
//			VIV2D_INFO_MSG("wait pipe failed");
//		}
	}
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
	               VIVS_DE_STRETCH_FACTOR_LOW_X(((src->width) << 16) / (dst->width)));
	etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_HIGH,
	               VIVS_DE_STRETCH_FACTOR_HIGH_Y(((src->height) << 16) / (dst->height)));
	VIV2D_DBG_MSG("_Viv2DStreamStretch %dx%d / %dx%d", src->width, src->height, dst->width, dst->height);

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

	VIV2D_DBG_MSG("_Viv2DStreamRects %d", cur_rect);
}

static inline void _Viv2DStreamBlendOp(Viv2DPtr v2d, Viv2DBlendOp *blend_op, uint8_t src_alpha, uint8_t dst_alpha, Bool src_global, Bool dst_global) {
	if (blend_op) {
		uint32_t alpha_mode = VIVS_DE_ALPHA_MODES_GLOBAL_SRC_ALPHA_MODE_NORMAL |
		                      VIVS_DE_ALPHA_MODES_GLOBAL_DST_ALPHA_MODE_NORMAL;

		if (src_global)
			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_SRC_ALPHA_MODE_GLOBAL;
//			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_SRC_ALPHA_MODE_SCALED;
		if (dst_global)
			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_DST_ALPHA_MODE_GLOBAL;
//			alpha_mode |= VIVS_DE_ALPHA_MODES_GLOBAL_DST_ALPHA_MODE_SCALED;

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
//		_Viv2DStreamReserve(v2d->stream, 10);
		etna_set_state(v2d->stream, VIVS_DE_ALPHA_CONTROL,
		               VIVS_DE_ALPHA_CONTROL_ENABLE_OFF);
		/*		etna_set_state(v2d->stream, VIVS_DE_ALPHA_MODES, 0);
				etna_set_state(v2d->stream, VIVS_DE_GLOBAL_SRC_COLOR, 0);
				etna_set_state(v2d->stream, VIVS_DE_GLOBAL_DEST_COLOR, 0);
				etna_set_state(v2d->stream, VIVS_DE_COLOR_MULTIPLY_MODES, 0);
				*/
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

// higher level helpers

static inline void _Viv2DStreamFlush(Viv2DPtr v2d) {
	etna_set_state(v2d->stream, VIVS_GL_FLUSH_CACHE, VIVS_GL_FLUSH_CACHE_PE2D);
}

#define VIV2D_SRC_PIX_RES 12
#define VIV2D_SRC_SOLID_RES 8
#define VIV2D_SRC_1X1_RES 16
#define VIV2D_DEST_RES 14
#define VIV2D_BLEND_ON_RES 10
#define VIV2D_BLEND_OFF_RES 2
#define VIV2D_FLUSH_RES 2
#define VIV2D_RECTS_RES(cnt) cnt*2+2

static inline void _Viv2DStreamReserveComp(Viv2DPtr v2d, int src_type, int cur_rect, Bool blend) {
	int reserve = 0;
	switch (src_type) {
	case viv2d_src_1x1_repeat:
		reserve += VIV2D_SRC_1X1_RES;
		break;
	case viv2d_src_solid:
		reserve += VIV2D_SRC_SOLID_RES;
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

	reserve += VIV2D_FLUSH_RES;

	_Viv2DStreamReserve(v2d, reserve);
}

static inline void _Viv2DStreamSolid(Viv2DPtr v2d, Viv2DPixmapPrivPtr dst, uint32_t color, Viv2DRect *rects, int cur_rect) {
	_Viv2DStreamReserve(v2d, VIV2D_DEST_RES + VIV2D_BLEND_OFF_RES + VIV2D_SRC_SOLID_RES + VIV2D_RECTS_RES(cur_rect) + VIV2D_FLUSH_RES);
	_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, NULL);
	// FIXME cause a bug
	_Viv2DStreamBlendOp(v2d, NULL, 0, 0, FALSE, FALSE); // reset blend
	_Viv2DStreamColor(v2d, color);
	_Viv2DStreamRects(v2d, rects, cur_rect);
	_Viv2DStreamFlush(v2d);
}

static inline void _Viv2DStreamCopy(Viv2DPtr v2d, Viv2DPixmapPrivPtr src, Viv2DPixmapPrivPtr dst, Viv2DBlendOp *blend_op,
                                    int x, int y, int w, int h, Viv2DRect *rects, int cur_rect) {
	_Viv2DStreamReserve(v2d, VIV2D_DEST_RES + VIV2D_SRC_PIX_RES + VIV2D_BLEND_ON_RES + VIV2D_RECTS_RES(cur_rect) + VIV2D_FLUSH_RES);
	_Viv2DStreamSrc(v2d, src, x, y, w, h);
	_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, NULL);
	_Viv2DStreamBlendOp(v2d, blend_op, 0, 0, FALSE, FALSE);
	_Viv2DStreamRects(v2d, rects, cur_rect);
	_Viv2DStreamFlush(v2d);
}

static inline void _Viv2DStreamComp(Viv2DPtr v2d, int src_type, Viv2DPixmapPrivPtr src, Viv2DFormat *src_fmt, int color,
                                    Viv2DPixmapPrivPtr dst, Viv2DBlendOp *blend_op,
                                    int x, int y, int w, int h, Viv2DRect *rects, int cur_rect) {
	Bool blend = blend_op != NULL ? TRUE : FALSE;
	Bool src_global = FALSE;
	Bool dst_global = FALSE;
	Bool src_alpha = 0;
	Bool dst_alpha = 0;
	/*
		if(dst->format.fmt == DE_FORMAT_X8R8G8B8) {
			dst_global=TRUE;
			dst_alpha=0xff;
			dst->format.fmt = DE_FORMAT_A8R8G8B8;
		}

		if(src_fmt->fmt == DE_FORMAT_X8R8G8B8) {
			src_global=TRUE;
			src_alpha=0xff;
			src_fmt->fmt = DE_FORMAT_A8R8G8B8;
		}
		*/
	_Viv2DStreamReserveComp(v2d, src_type, cur_rect, blend);

	switch (src_type) {
	case viv2d_src_1x1_repeat:
		_Viv2DStreamSrcWithFormat(v2d, src, 0, 0, 1, 1, src_fmt);
		_Viv2DStreamStretch(v2d, src, dst);
		_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_STRETCH_BLT, NULL);
		break;
	case viv2d_src_solid:
		_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, NULL);
		_Viv2DStreamColor(v2d, color);
		break;
	default:
		_Viv2DStreamSrcWithFormat(v2d, src, x, y, w, h, src_fmt);
		_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, NULL);
		break;
	}

	_Viv2DStreamBlendOp(v2d, blend_op, src_alpha, dst_alpha, src_global, dst_global);
	_Viv2DStreamRects(v2d, rects, cur_rect);
	_Viv2DStreamFlush(v2d);

}

static inline void _Viv2DStreamClear(Viv2DPtr v2d, Viv2DPixmapPrivPtr pix) {
	if (pix && pix->bo) {
		Viv2DRect rect[1];
		rect[0].x1 = 0;
		rect[0].y1 = 0;
		rect[0].x2 = pix->width;
		rect[0].y2 = pix->height;

		_Viv2DStreamSolid(v2d, pix, 0x00000000, rect, 1);
	}
}


#ifdef VIV2D_CACHE_BO
static inline void Viv2DCacheInit(Viv2DPtr v2d) {
	int i;
	v2d->cache_size = 0;
	for (i = 0; i < VIV2D_CACHE_SIZE; i++) {
		v2d->cache[i].bo = NULL;
		v2d->cache[i].size = 0;
		v2d->cache[i].used = 0;
	}
}

static inline struct etna_bo *Viv2DCacheNewBo(Viv2DPtr v2d, int size) {
	struct etna_bo *bo;
	int i;
	bo = NULL;

	for (i = 0; i < VIV2D_CACHE_SIZE; i++) {
		// free to use
		if (!v2d->cache[i].used && ALIGN(size, 4096) == v2d->cache[i].size) {
//			VIV2D_INFO_MSG("Viv2DCacheNewBo: reuse %d %ld %p %d->%d", i, v2d->cache_size, v2d->cache[i].bo, v2d->cache[i].size, size);
			v2d->cache[i].used = 1;
			bo = v2d->cache[i].bo;
#ifdef VIV2D_CPU_BO_CLEAR
			// FIXME: the GPU should do that
			etna_bo_cpu_prep(bo, DRM_ETNA_PREP_WRITE);
			char *buf = etna_bo_map(bo);
			neon_memset(buf, 0, size);
			etna_bo_cpu_fini(bo);
#endif
			return bo;
		}
		// empty entry
		if (v2d->cache[i].size == 0) {
			v2d->cache[i].used = 1;
			v2d->cache[i].size = ALIGN(size, 4096);
			v2d->cache_size += v2d->cache[i].size;
			bo = v2d->cache[i].bo = etna_bo_new(v2d->dev, ALIGN(size, 4096), ETNA_BO_UNCACHED);
//			VIV2D_INFO_MSG("Viv2DCacheNewBo: create %d %ld %p %d->%d", i, v2d->cache_size, v2d->cache[i].bo, v2d->cache[i].size, size);
			return bo;
		}
	}

	if (v2d->cache_size > VIV2D_CACHE_MAX) {
		for (i = 0; i < VIV2D_CACHE_SIZE; i++) {
			if (!v2d->cache[i].used) {
//				VIV2D_INFO_MSG("Viv2DCacheNewBo: clean %d %ld %p %d->%d", i, v2d->cache_size, v2d->cache[i].bo, v2d->cache[i].size, ALIGN(size, 4096));
				etna_bo_del(v2d->cache[i].bo);
				v2d->cache_size -= v2d->cache[i].size;
				v2d->cache[i].size = 0;
			}
		}
	}

	// recycle
	if (!bo) {
		for (i = 0; i < VIV2D_CACHE_SIZE; i++) {
			if (!v2d->cache[i].used) {
//				VIV2D_INFO_MSG("Viv2DCacheNewBo: recycle %d %ld %p %d->%d", i, v2d->cache_size, v2d->cache[i].bo, v2d->cache[i].size, ALIGN(size, 4096));
//				etna_bo_wait(v2d->dev, v2d->pipe, v2d->cache[i].bo);
				etna_bo_del(v2d->cache[i].bo);
				v2d->cache_size -= v2d->cache[i].size;
				v2d->cache[i].used = 1;
				v2d->cache[i].size = ALIGN(size, 4096);
				v2d->cache_size += v2d->cache[i].size;
				bo = v2d->cache[i].bo = etna_bo_new(v2d->dev, ALIGN(size, 4096), ETNA_BO_UNCACHED);
				return bo;
			}
		}

	}

	VIV2D_INFO_MSG("Viv2DCacheNewBo: cannot get a cached bo");
	return bo;
}

static inline void Viv2DCacheDelBo(Viv2DPtr v2d, struct etna_bo *bo) {
	int i;
	for (i = 0; i < VIV2D_CACHE_SIZE; i++) {
		if (v2d->cache[i].bo == bo) {
			v2d->cache[i].used = 0;
#ifdef VIV2D_GPU_BO_CLEAR
//			etna_bo_wait(v2d->dev, v2d->pipe, bo);
			int asize = ALIGN(v2d->cache[i].size, 4096) / 4;
			Viv2DPixmapPrivRec pix;
			pix.bo = bo;
			pix.width = 32;
			pix.height = asize / 32;
			pix.pitch = 32 * 4;
			pix.format.bpp = 32;
			pix.format.depth = 32;
			pix.format.swizzle = DE_SWIZZLE_ARGB;
			pix.format.fmt = DE_FORMAT_A8R8G8B8;
			_Viv2DStreamClear(v2d, &pix);
#endif
			return;
		}
	}
}
#endif

static inline void _Viv2OpClearTmpPix(Viv2DPtr v2d) {
	if (v2d->tmp_pix_cnt > 0) {
		do {
			Viv2DPixmapPrivPtr tmp;
			v2d->tmp_pix_cnt--;
			tmp = &v2d->tmp_pix[v2d->tmp_pix_cnt];

//			etna_bo_wait(v2d->dev, v2d->pipe, tmp->bo);

			VIV2D_DBG_MSG("_Viv2OpClearTmpPix %p %d", tmp->bo, v2d->tmp_pix_cnt);
#ifdef VIV2D_CACHE_BO
			Viv2DCacheDelBo(v2d, tmp->bo);
#else
			etna_bo_del(tmp->bo);
#endif
		} while (v2d->tmp_pix_cnt);
	}
}

static inline Viv2DPixmapPrivPtr _Viv2DOpCreateTmpPix(Viv2DPtr v2d, int width, int height, int bpp) {
	Viv2DPixmapPrivPtr tmp;
	int pitch;

	if (v2d->tmp_pix_cnt == VIV2D_MAX_TMP_PIX) {
//		VIV2D_INFO_MSG("_Viv2DOpCreateTmpPix max tmp pix achieved %d %d", v2d->tmp_pix_cnt, v2d->stream->offset);
		_Viv2DStreamCommit(v2d, FALSE);
		_Viv2OpClearTmpPix(v2d);
//		VIV2D_INFO_MSG("_Viv2DOpCreateTmpPix max tmp pix commit %d", v2d->tmp_pix_cnt);
	}

	tmp = &v2d->tmp_pix[v2d->tmp_pix_cnt];
	pitch = ALIGN(width * ((bpp + 7) / 8), VIV2D_PITCH_ALIGN);
#ifdef VIV2D_CACHE_BO
	tmp->bo = Viv2DCacheNewBo(v2d, pitch * height);
#else
	tmp->bo = etna_bo_new(v2d->dev, pitch * height, ETNA_BO_UNCACHED);
#endif

	VIV2D_DBG_MSG("_Viv2DOpCreateTmpPix %p %dx%d %d", tmp->bo, width, height, pitch * height);
	tmp->width = width;
	tmp->height = height;
	tmp->pitch = pitch;
	v2d->tmp_pix_cnt++;

	return tmp;
}


#endif
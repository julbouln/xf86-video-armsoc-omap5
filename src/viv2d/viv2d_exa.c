
/*
 * Copyright © 2016 Julien Boulnois
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Julien Boulnois <jboulnois@gmail.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <mipict.h>

#include "armsoc_driver.h"
#include "armsoc_exa.h"

#include "etnaviv_drmif.h"
#include "etnaviv_drm.h"

#include "exa.h"

#include "viv2d.h"
#include "viv2d_exa.h"
#include "viv2d_op.h"

#include "viv2d_config.h"

/*
For each pixel, the four channels of the image are computed with:

	C = Ca * Fa + Cb * Fb

where C, Ca, Cb are the values of the respective channels and Fa and Fb
come from the following table:

	PictOp			Fa			Fb
	--------------------------------------------------
	Clear		0			0
	Src			1			0
	Dst			0			1
	Over		1			1-Aa
	OverReverse	1-Ab		1
	In			Ab			0
	InReverse	0			Aa
	Out			1-Ab		0
	OutReverse	0			1-Aa
	Atop		Ab			1-Aa
	AtopReverse	1-Ab		Aa
	Xor			1-Ab		1-Aa
	Add			1			1
	Saturate	min(1,(1-Ab)/Aa)	1
*/

static Viv2DBlendOp viv2d_blend_op[] = {
	{PictOpClear,			DE_BLENDMODE_ZERO, 				DE_BLENDMODE_ZERO},
	{PictOpSrc,				DE_BLENDMODE_ONE, 				DE_BLENDMODE_ZERO},
	{PictOpDst,				DE_BLENDMODE_ZERO, 				DE_BLENDMODE_ONE},
	{PictOpOver,			DE_BLENDMODE_ONE,				DE_BLENDMODE_INVERSED},
	{PictOpOverReverse,		DE_BLENDMODE_INVERSED, 			DE_BLENDMODE_ONE},
	{PictOpIn,				DE_BLENDMODE_NORMAL,			DE_BLENDMODE_ZERO},
	{PictOpInReverse,		DE_BLENDMODE_ZERO,				DE_BLENDMODE_NORMAL},
	{PictOpOut,				DE_BLENDMODE_INVERSED,			DE_BLENDMODE_ZERO},
	{PictOpOutReverse,		DE_BLENDMODE_ZERO,				DE_BLENDMODE_INVERSED},
	{PictOpAtop,			DE_BLENDMODE_NORMAL,			DE_BLENDMODE_INVERSED},
	{PictOpAtopReverse,		DE_BLENDMODE_INVERSED,			DE_BLENDMODE_NORMAL},
	{PictOpXor,				DE_BLENDMODE_INVERSED,			DE_BLENDMODE_INVERSED},
	{PictOpAdd,				DE_BLENDMODE_ONE,				DE_BLENDMODE_ONE},
	{PictOpSaturate,		DE_BLENDMODE_SATURATED_ALPHA,	DE_BLENDMODE_ONE} // does not work ?
};

#define BLEND_SIZE PictOpAdd

#define NO_PICT_FORMAT -1
/**
 * Picture Formats and their counter parts
 */
#ifdef VIV2D_SUPPORT_MONO
#define VIV2D_PICT_FORMAT_COUNT 19
#else
#define VIV2D_PICT_FORMAT_COUNT 18
#endif
static const Viv2DFormat
viv2d_pict_format[] = {
	{PICT_a8r8g8b8, 32, 32, DE_FORMAT_A8R8G8B8, DE_SWIZZLE_ARGB, 8},
	{PICT_x8r8g8b8, 32, 24, DE_FORMAT_X8R8G8B8, DE_SWIZZLE_ARGB, 0},
	{PICT_a8b8g8r8, 32, 32, DE_FORMAT_A8R8G8B8, DE_SWIZZLE_ABGR, 8},
	{PICT_x8b8g8r8, 32, 24, DE_FORMAT_X8R8G8B8,	DE_SWIZZLE_ABGR, 0},
	{PICT_b8g8r8a8, 32, 32, DE_FORMAT_A8R8G8B8,	DE_SWIZZLE_BGRA, 8},
	{PICT_b8g8r8x8, 32, 24, DE_FORMAT_X8R8G8B8,	DE_SWIZZLE_BGRA, 8},
	{PICT_r5g6b5, 16, 16, DE_FORMAT_R5G6B5, DE_SWIZZLE_ARGB, 0},
	{PICT_b5g6r5, 16, 16, DE_FORMAT_R5G6B5,	DE_SWIZZLE_ABGR, 0},
	{PICT_a1r5g5b5, 16, 16, DE_FORMAT_A1R5G5B5, DE_SWIZZLE_ARGB, 1},
	{PICT_x1r5g5b5, 16, 15, DE_FORMAT_X1R5G5B5, DE_SWIZZLE_ARGB, 0},
	{PICT_a1b5g5r5, 16, 16, DE_FORMAT_A1R5G5B5,	DE_SWIZZLE_ABGR, 1},
	{PICT_x1b5g5r5, 16,	15, DE_FORMAT_X1R5G5B5, DE_SWIZZLE_ABGR, 0},
	{PICT_a4r4g4b4, 16, 16, DE_FORMAT_A4R4G4B4, DE_SWIZZLE_ARGB, 4},
	{PICT_x4r4g4b4, 16, 12, DE_FORMAT_X4R4G4B4, DE_SWIZZLE_ARGB, 0},
	{PICT_a4b4g4r4, 16, 16, DE_FORMAT_A4R4G4B4, DE_SWIZZLE_ABGR, 4},
	{PICT_x4b4g4r4, 16, 12, DE_FORMAT_X4R4G4B4, DE_SWIZZLE_ABGR, 0},
	{PICT_a8, 8, 8, DE_FORMAT_A8, DE_SWIZZLE_ARGB, 8},
//	{PICT_c8, 8, 8, DE_FORMAT_INDEX8, DE_SWIZZLE_ARGB, 8},
#ifdef VIV2D_SUPPORT_MONO
	{PICT_a1, 1, 1, DE_FORMAT_MONOCHROME, DE_SWIZZLE_ARGB, 1},
#endif
	{NO_PICT_FORMAT, 0, 0, 0}
	/*END*/
};

static void Viv2DFlush(struct ARMSOCEXARec *exa) {
	Viv2DEXAPtr v2d_exa = (Viv2DEXAPtr)(exa);
	Viv2DRec *v2d = v2d_exa->v2d;

//	VIV2D_INFO_MSG("Viv2DFlush");
	if (!_Viv2DStreamWait(v2d)) {
		_Viv2DStreamCommit(v2d, TRUE);
	}

}

static void Viv2DAllocBuf(struct ARMSOCEXARec *exa, int width, int height, int depth, int bpp, struct ARMSOCEXABuf *buf) {
	Viv2DEXAPtr v2d_exa = (Viv2DEXAPtr)(exa);
	Viv2DRec *v2d = v2d_exa->v2d;

	int pitch = ALIGN(width * ((bpp + 7) / 8), VIV2D_PITCH_ALIGN);
	int size = pitch * height;

	VIV2D_DBG_MSG("Viv2DAllocBuf: buf:%p size:%d", buf, ALIGN(size, 4096));
	Viv2DFormat fmt;

	// do not create etna bo if too small or unsupported format
	if (size > VIV2D_MIN_SIZE && size < VIV2D_MAX_SIZE && _Viv2DSetFormat(depth, bpp, &fmt)) {
		struct etna_bo *bo;
		//	VIV2D_INFO_MSG("Viv2DAllocBuf size:%d pitch:%d", pitch * height, pitch);
		bo = etna_bo_cache_new(v2d->dev, size);
		buf->priv = (void *)bo;
		buf->buf = etna_bo_map(bo);
	} else {
		VIV2D_DBG_MSG("Viv2DAllocBuf: use CPU only memory buf:%p size:%d", buf, size);
		if (size > 0) {
			buf->priv = NULL;
			buf->buf = malloc(size);
		} else {
			buf->priv = NULL;
			buf->buf = NULL;
		}
	}

	buf->pitch = pitch;
	buf->size = size;
}

static void Viv2DFreeBuf(struct ARMSOCEXARec *exa, struct ARMSOCEXABuf *buf) {
	VIV2D_DBG_MSG("Viv2DFreeBuf buf:%p size:%d", buf, ALIGN(buf->size, 4096));
	if (buf->priv) {
		Viv2DEXAPtr v2d_exa = (Viv2DEXAPtr)(exa);
		Viv2DRec *v2d = v2d_exa->v2d;
		struct etna_bo *bo = (struct etna_bo *)buf->priv;
		etna_bo_cache_del(v2d->dev, bo);
	} else {
		VIV2D_DBG_MSG("Viv2DFreeBuf CPU only memory buf:%p size:%d", buf, buf->size);
		if (buf->buf)
			free(buf->buf);
	}
	buf->priv = NULL;
	buf->buf = NULL;
	buf->pitch = 0;
	buf->size = 0;
}

static Bool Viv2DMapUsermemBuf(struct ARMSOCEXARec *exa, int width, int height, int pitch, void *data, struct ARMSOCEXABuf *buf) {
	int size = pitch * height;

	if (((uintptr_t)data % 4096) == 0 && (size % 4096) == 0) {
		Viv2DEXAPtr v2d_exa = (Viv2DEXAPtr)(exa);
		Viv2DRec *v2d = v2d_exa->v2d;

		struct etna_bo *aligned_bo = etna_bo_from_usermem_prot(v2d->dev, data, size, ETNA_USERPTR_READ | ETNA_USERPTR_WRITE);
		VIV2D_INFO_MSG("Viv2DMapUsermemBuf bo:%p buf:%p", aligned_bo, data);

		buf->priv = aligned_bo;
		buf->buf = data;
		buf->size = size;
		buf->pitch = pitch;
		return TRUE;
	} else {
		return FALSE;
	}
}

static void Viv2DUnmapUsermemBuf(struct ARMSOCEXARec *exa, struct ARMSOCEXABuf *buf) {
	if (buf->priv) {
		int err = 0;
		Viv2DEXAPtr v2d_exa = (Viv2DEXAPtr)(exa);
		Viv2DRec *v2d = v2d_exa->v2d;
		struct etna_bo *bo = (struct etna_bo *)buf->priv;
//		_Viv2DStreamCommit(v2d, TRUE);
//		_Viv2DStreamWait(v2d);
//		err = etna_bo_wait(v2d->dev, v2d->pipe, bo, 5000000000);
		VIV2D_INFO_MSG("Viv2DUnmapUsermemBuf bo:%p buf:%p err:%d", bo, buf->buf, err);
//		etna_bo_del(bo);
		etna_bo_cache_usermem_del(v2d->dev, bo);

		buf->priv = NULL;
		buf->buf = NULL;
		buf->size = 0;
		buf->pitch = 0;
	}
}

static inline void Viv2DDetachBo(struct ARMSOCRec *pARMSOC, struct ARMSOCPixmapPrivRec *armsocPix) {
	if (armsocPix) {
		Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
		Viv2DPixmapPrivPtr pix = armsocPix->priv;

		if (armsocPix->bo == pARMSOC->scanout) {
		} else {
			if (armsocPix->bo && pix->bo) {
				VIV2D_DBG_MSG("Viv2DDetachBo detach pix:%p bo:%p dumbBo:%p refcnt:%d", pix, pix->bo, armsocPix->bo, pix->refcnt);
				etna_bo_del(pix->bo);
			}
			pix->bo = NULL;
		}
	}
}

static inline Bool Viv2DAttachBo(struct ARMSOCRec *pARMSOC, struct ARMSOCPixmapPrivRec *armsocPix) {
	if (armsocPix) {
		Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
		Viv2DPixmapPrivPtr pix = armsocPix->priv;

		if (armsocPix->bo) {
			if (armsocPix->bo == pARMSOC->scanout) {
				pix->bo = v2d->bo;
			} else {
				int fd = armsoc_bo_get_dmabuf(armsocPix->bo);
				if (fd) {
					pix->bo = etna_bo_from_dmabuf(v2d->dev, fd);
					close(fd);
					VIV2D_DBG_MSG("Viv2DAttachBo attach from dmabuf pix:%p bo:%p dumbBo:%p", pix, pix->bo, armsocPix->bo);
				} else {
					VIV2D_ERR_MSG("Viv2DAttachBo error cannot attach bo : %d", fd);
				}
			}
		} else {
			if (armsocPix->buf.priv) {
				pix->bo = (struct etna_bo *)armsocPix->buf.priv;
				VIV2D_DBG_MSG("Viv2DAttachBo attach from armsoc buf pix:%p bo:%p buf:%p size:%d", pix, pix->bo, armsocPix->buf.buf, armsocPix->buf.size);
			} else {
				VIV2D_DBG_MSG("Viv2DAttachBo CPU only memory pix:%p buf:%p size:%d", pix, &armsocPix->buf, armsocPix->buf.size);
				pix->bo = NULL;
			}
		}
		return TRUE;
	} else {
		VIV2D_INFO_MSG("Viv2DAttachBo failed no armsoc pix");
	}
	return FALSE;
}

static void Viv2DReattach(PixmapPtr pPixmap, int width, int height, int pitch) {
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCPixmapPrivRec *armsocPix = exaGetPixmapDriverPrivate(pPixmap);

	Viv2DPixmapPrivPtr pix = armsocPix->priv;
	VIV2D_DBG_MSG("Viv2DReattach pix %p", pix);

	pix->width = width;
	pix->height = height;
	pix->pitch = pitch;

	Viv2DDetachBo(pARMSOC, armsocPix);
	Viv2DAttachBo(pARMSOC, armsocPix);
}

// from libyuv
static inline void ARGBExtractAlphaRow_NEON(const uint8_t* src_argb,
        uint8_t* dst_a,
        int width) {
	asm volatile(
	    "1:                                        \n"
	    "vld4.8     {d0, d2, d4, d6}, [%0]!        \n"  // load 8 ARGB pixels
	    "vld4.8     {d1, d3, d5, d7}, [%0]!        \n"  // load next 8 ARGB pixels
	    "subs       %2, %2, #16                    \n"  // 16 processed per loop
	    "vst1.8     {q3}, [%1]!                    \n"  // store 16 A's.
	    "bgt       1b                              \n"
	    : "+r"(src_argb),  // %0
	    "+r"(dst_a),     // %1
	    "+r"(width)      // %2
	    :
	    : "cc", "memory", "q0", "q1", "q2", "q3"  // Clobber List
	);
}

static inline uint32_t Viv2DScale16(uint32_t val, int bits)
{
	val <<= (16 - bits);
	while (bits < 16) {
		val |= val >> bits;
		bits <<= 1;
	}
	return val >> 8;
}

static inline uint32_t Viv2DColour(Pixel pixel, int depth) {
	uint32_t colour;
	switch (depth) {
	case 15: /* A1R5G5B5 */
		colour = (pixel & 0x8000 ? 0xff000000 : 0) |
		         Viv2DScale16((pixel & 0x7c00) >> 10, 5) << 16 |
		         Viv2DScale16((pixel & 0x03e0) >> 5, 5) << 8 |
		         Viv2DScale16((pixel & 0x001f), 5);
		break;
	case 16: /* R5G6B5 */
		colour = 0xff000000 |
		         Viv2DScale16((pixel & 0xf800) >> 11, 5) << 16 |
		         Viv2DScale16((pixel & 0x07e0) >> 5, 6) << 8 |
		         Viv2DScale16((pixel & 0x001f), 5);
		break;
	case 24: /* A8R8G8B8 */
		colour = 0xff000000 | pixel;
		break;
	default:
		colour = pixel;
		break;
	}
	return colour;
}

#ifdef VIV2D_1X1_REPEAT_AS_SOLID
static CARD32 Viv2DGetFirstPixel(DrawablePtr pDraw)
{
	union { CARD32 c32; CARD16 c16; CARD8 c8; char c; } pixel;

	pDraw->pScreen->GetImage(pDraw, 0, 0, 1, 1, ZPixmap, ~0, &pixel.c);

	switch (pDraw->bitsPerPixel) {
	case 32:
		return pixel.c32;
	case 16:
		return pixel.c16;
	case 8:
	case 4:
	case 1:
		return pixel.c8;
	default:
		assert(0);
	}
}
#endif

static inline Bool Viv2DNonAlpha(Viv2DFormat *fmt) {
	switch (fmt->fmt) {
	case DE_FORMAT_X4R4G4B4:
	case DE_FORMAT_X1R5G5B5:
	case DE_FORMAT_X8R8G8B8:
	case DE_FORMAT_R5G6B5:
		return TRUE;
	default:
		return FALSE;
	}
}

static inline uint32_t idx2op(int index)
{
	switch (index) {
	case EXA_PREPARE_SRC:
	case EXA_PREPARE_MASK:
	case EXA_PREPARE_AUX_SRC:
	case EXA_PREPARE_AUX_MASK:
		return DRM_ETNA_PREP_READ;
	case EXA_PREPARE_AUX_DEST:
	case EXA_PREPARE_DEST:
	default:
		return DRM_ETNA_PREP_WRITE;
	}
}

#ifdef VIV2D_FLUSH_CALLBACK
static void Viv2DFlushCallback(CallbackListPtr *list, pointer user_data,
                               pointer call_data)
{
	ScrnInfoPtr pScrn = user_data;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);

//	VIV2D_INFO_MSG("Viv2DFlushCallback");
	if (!_Viv2DStreamWait(v2d)) {
		_Viv2DStreamCommit(v2d, TRUE);
	}

}
#endif

static int Viv2DMarkSync(ScreenPtr pScreen)
{
	return 0;
}

static void Viv2DWaitMarker(ScreenPtr pScreen, int marker)
{
}

/**
 * PrepareAccess() is called before CPU access to an offscreen pixmap.
 *
 * @param pPix the pixmap being accessed
 * @param index the index of the pixmap being accessed.
 *
 * PrepareAccess() will be called before CPU access to an offscreen pixmap.
 * This can be used to set up hardware surfaces for byteswapping or
 * untiling, or to adjust the pixmap's devPrivate.ptr for the purpose of
 * making CPU access use a different aperture.
 *
 * The index is one of #EXA_PREPARE_DEST, #EXA_PREPARE_SRC,
 * #EXA_PREPARE_MASK, #EXA_PREPARE_AUX_DEST, #EXA_PREPARE_AUX_SRC, or
 * #EXA_PREPARE_AUX_MASK. Since only up to #EXA_NUM_PREPARE_INDICES pixmaps
 * will have PrepareAccess() called on them per operation, drivers can have
 * a small, statically-allocated space to maintain state for PrepareAccess()
 * and FinishAccess() in.  Note that PrepareAccess() is only called once per
 * pixmap and operation, regardless of whether the pixmap is used as a
 * destination and/or source, and the index may not reflect the usage.
 *
 * PrepareAccess() may fail.  An example might be the case of hardware that
 * can set up 1 or 2 surfaces for CPU access, but not 3.  If PrepareAccess()
 * fails, EXA will migrate the pixmap to system memory.
 * DownloadFromScreen() must be implemented and must not fail if a driver
 * wishes to fail in PrepareAccess().  PrepareAccess() must not fail when
 * pPix is the visible screen, because the visible screen can not be
 * migrated.
 *
 * @return TRUE if PrepareAccess() successfully prepared the pixmap for CPU
 * drawing.
 * @return FALSE if PrepareAccess() is unsuccessful and EXA should use
 * DownloadFromScreen() to migrate the pixmap out.
 */
static Bool
Viv2DPrepareAccess(PixmapPtr pPixmap, int index) {
	Viv2DRec *v2d = Viv2DPrivFromPixmap(pPixmap);
	struct ARMSOCPixmapPrivRec *armsocPix = exaGetPixmapDriverPrivate(pPixmap);
	Viv2DPixmapPrivPtr pix = armsocPix->priv;

//	VIV2D_DBG_MSG("Viv2DPrepareAccess %p (%dx%d) %d (%d)", pPixmap, pix->width, pix->height, index, pix->refcnt);

	// only if pixmap has been used
	if (pix->refcnt > 0) {
		// flush if remaining state
		if (pix->bo) {
			if (!etna_bo_ready(pix->bo))
				_Viv2DStreamCommit(v2d, TRUE);
			etna_bo_cpu_prep(pix->bo, idx2op(index), 5000000000);
#ifdef VIV2D_TRACE
			_Viv2DPixTrace(pix, "prep");
#endif
			VIV2D_DBG_MSG("Viv2DPrepareAccess pix:%p/%p(%dx%d) bo:%p index:%d refcnt:(%d)", pPixmap, pix, pix->width, pix->height, pix->bo, index, pix->refcnt);
		}
		pix->refcnt = -1;
	}

	return ARMSOCPrepareAccess(pPixmap, index);
}

/**
 * FinishAccess() is called after CPU access to an offscreen pixmap.
 *
 * @param pPix the pixmap being accessed
 * @param index the index of the pixmap being accessed.
 *
 * FinishAccess() will be called after finishing CPU access of an offscreen
 * pixmap set up by PrepareAccess().  Note that the FinishAccess() will not be
 * called if PrepareAccess() failed and the pixmap was migrated out.
 */
static void
Viv2DFinishAccess(PixmapPtr pPixmap, int index)
{
	struct ARMSOCPixmapPrivRec *armsocPix = exaGetPixmapDriverPrivate(pPixmap);
	Viv2DPixmapPrivPtr pix = armsocPix->priv;

	ARMSOCFinishAccess(pPixmap, index);

	if (pix->refcnt == -1) {
		if (pix->bo) {
			VIV2D_DBG_MSG("Viv2DFinishAccess pix:%p/%p(%dx%d) bo:%p index:%d refcnt:(%d)", pPixmap, pix, pix->width, pix->height, pix->bo, index, pix->refcnt);
			etna_bo_cpu_fini(pix->bo);
		}
		pix->refcnt = 0;
	}
}

/**
 * PixmapIsOffscreen() is an optional driver replacement to
 * exaPixmapHasGpuCopy(). Set to NULL if you want the standard behaviour
 * of exaPixmapHasGpuCopy().
 *
 * @param pPix the pixmap
 * @return TRUE if the given drawable is in framebuffer memory.
 *
 * exaPixmapHasGpuCopy() is used to determine if a pixmap is in
 * offscreen memory, meaning that acceleration could probably be done
 * to it, and that it will need to be wrapped by PrepareAccess()/
 * FinishAccess() when accessing it from the CPU.
 */
static Bool
Viv2DPixmapIsOffscreen(PixmapPtr pPixmap)
{
	struct ARMSOCPixmapPrivRec *armsocPix = exaGetPixmapDriverPrivate(pPixmap);
	Viv2DPixmapPrivPtr pix = armsocPix->priv;

	/* offscreen means in 'gpu accessible memory', not that it's off
	 * the visible screen.  We currently have no special constraints,
	 * since compatible ARM CPUS have a flat memory model (no separate
	 * GPU memory). If an individual EXA implementation has additional
	 * constraints, like buffer size or mapping in GPU MMU, it should
	 * wrap this function.
	 */
	return pix && (pix->bo || armsocPix->buf.buf);
}

static void *
Viv2DCreatePixmap2 (ScreenPtr pScreen, int width, int height,
                    int depth, int usage_hint, int bitsPerPixel,
                    int *new_fb_pitch)
{
	struct ARMSOCPixmapPrivRec *armsocPix = ARMSOCCreatePixmap2(pScreen, width, height, depth,
	                                        usage_hint, bitsPerPixel, new_fb_pitch);
	Viv2DPixmapPrivPtr pix = calloc(sizeof(Viv2DPixmapPrivRec), 1);
	VIV2D_DBG_MSG("Viv2DCreatePixmap pix %p", pix);

	_Viv2DSetFormat(32, 32, &pix->format);

	armsocPix->priv = pix;
	pix->armsocPix = armsocPix;

	return armsocPix;
}

static void
Viv2DDestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCPixmapPrivRec *armsocPix = driverPriv;

	Viv2DPixmapPrivPtr pix = armsocPix->priv;
	VIV2D_DBG_MSG("Viv2DDestroyPixmap pix %p", pix);

	Viv2DDetachBo(pARMSOC, armsocPix);

	free(pix);
	armsocPix->priv = NULL;
	ARMSOCDestroyPixmap(pScreen, armsocPix);
}

static Bool
Viv2DModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
                        int depth, int bitsPerPixel, int devKind,
                        pointer pPixData)
{
	Viv2DRec *v2d = Viv2DPrivFromPixmap(pPixmap);
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCPixmapPrivRec *armsocPix = exaGetPixmapDriverPrivate(pPixmap);

	Viv2DPixmapPrivPtr pix = armsocPix->priv;

	VIV2D_DBG_MSG("Viv2DModifyPixmapHeader pix:%p %dx%d depth:%d bpp:%d pix_depth:%d pix_bpp:%d", pix, width, height, depth, bitsPerPixel, pPixmap->drawable.depth, pPixmap->drawable.bitsPerPixel);

	if (ARMSOCModifyPixmapHeader(pPixmap, width, height, depth, bitsPerPixel, devKind, pPixData)) {
		if (armsocPix->bo && pPixData == armsoc_bo_map(pARMSOC->scanout) && pix->bo != v2d->bo) {
			VIV2D_DBG_MSG("Viv2DModifyPixmapHeader pix bo scanout %p", pPixmap);
			pix->width = armsoc_bo_width(pARMSOC->scanout);
			pix->height = armsoc_bo_height(pARMSOC->scanout);
			pix->pitch = armsoc_bo_pitch(pARMSOC->scanout);
			pix->bo = v2d->bo;
		} else {
			if (armsocPix->bo) {
				if (pix->width != armsoc_bo_width(armsocPix->bo) ||
				        pix->height != armsoc_bo_height(armsocPix->bo) ||
				        pix->pitch != armsoc_bo_pitch(armsocPix->bo)) {

					VIV2D_DBG_MSG("Viv2DModifyPixmapHeader pixmap:%p armsocPix:%p pix:%p %dx%d[%d] -> %dx%d[%d] depth:%d bpp:%d", pPixmap, armsocPix, pix,
					              pix->width, pix->height, pix->pitch,
					              armsoc_bo_width(armsocPix->bo), armsoc_bo_height(armsocPix->bo), armsoc_bo_pitch(armsocPix->bo),
					              armsoc_bo_depth(armsocPix->bo), armsoc_bo_bpp(armsocPix->bo)
					             );
					pix->width = armsoc_bo_width(armsocPix->bo);
					pix->height = armsoc_bo_height(armsocPix->bo);
					pix->pitch = armsoc_bo_pitch(armsocPix->bo);
					Viv2DDetachBo(pARMSOC, armsocPix);
					Bool ret = Viv2DAttachBo(pARMSOC, armsocPix);
					return ret;
				}
			} else {
				if (pix->width != width ||
				        pix->height != height ||
				        pix->pitch != armsocPix->buf.pitch
				   ) {
					VIV2D_DBG_MSG("Viv2DModifyPixmapHeader native pixmap:%p armsocPix:%p pix:%p", pPixmap, armsocPix, pix);
					pix->width = width;
					pix->height = height;
					pix->pitch = armsocPix->buf.pitch;
					Viv2DDetachBo(pARMSOC, armsocPix);
					Bool ret = Viv2DAttachBo(pARMSOC, armsocPix);
					return ret;
				}
			}
		}
	} else {
		VIV2D_DBG_MSG("Viv2DModifyPixmapHeader soft pixmap:%p armsocPix:%p pix:%p %dx%d %d/%d", pPixmap, armsocPix, pix, width, height, depth, bitsPerPixel);
		Viv2DDetachBo(pARMSOC, armsocPix);
		return FALSE;
	}
	return TRUE;
}

/**
 * UploadToScreen() loads a rectangle of data from src into pDst.
 *
 * @param pDst destination pixmap
 * @param x destination X coordinate.
 * @param y destination Y coordinate
 * @param width width of the rectangle to be copied
 * @param height height of the rectangle to be copied
 * @param src pointer to the beginning of the source data
 * @param src_pitch pitch (in bytes) of the lines of source data.
 *
 * UploadToScreen() copies data in system memory beginning at src (with
 * pitch src_pitch) into the destination pixmap from (x, y) to
 * (x + width, y + height).  This is typically done with hostdata uploads,
 * where the CPU sets up a blit command on the hardware with instructions
 * that the blit data will be fed through some sort of aperture on the card.
 *
 * If UploadToScreen() is performed asynchronously, it is up to the driver
 * to call exaMarkSync().  This is in contrast to most other acceleration
 * calls in EXA.
 *
 * UploadToScreen() can aid in pixmap migration, but is most important for
 * the performance of exaGlyphs() (antialiased font drawing) by allowing
 * pipelining of data uploads, avoiding a sync of the card after each glyph.
 *
 * @return TRUE if the driver successfully uploaded the data.  FALSE
 * indicates that EXA should fall back to doing the upload in software.
 *
 * UploadToScreen() is not required, but is recommended if Composite
 * acceleration is supported.
 */

#ifdef VIV2D_UPLOAD_TO_SCREEN
static Bool Viv2DUploadToScreen(PixmapPtr pDst,
                                int x, int y, int w, int h, char *src, int src_pitch) {
	ScrnInfoPtr pScrn = pix2scrn(pDst);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DPixmapPrivPtr dst = Viv2DPixmapPrivFromPixmap(pDst);
	Viv2DRect rects[1];
	int height = h;
	Viv2DPixmapPrivPtr tmp;
	int pitch;
	char *src_buf, *buf;
	Viv2DFormat tmp_fmt;
	int use_usermem = 0;
	int src_x = 0;
	int src_y = 0;

	if (w * h < 4)
		return FALSE;

	if (!dst->bo)
		return FALSE;

	if (!_Viv2DSetFormat(pDst->drawable.depth, pDst->drawable.bitsPerPixel, &dst->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DUploadToScreen unsupported dst format %d/%d %p", pDst->drawable.depth, pDst->drawable.bitsPerPixel, src);
		return FALSE;
	}

	if (!_Viv2DSetFormat(pDst->drawable.depth, pDst->drawable.bitsPerPixel, &tmp_fmt)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DUploadToScreen unsupported format %d/%d %p", pDst->drawable.depth, pDst->drawable.bitsPerPixel, src);
		return FALSE;
	}

#ifndef VIV2D_SUPPORT_A8_DST
	if (dst->format.fmt == DE_FORMAT_A8) {
//		VIV2D_UNSUPPORTED_MSG("Viv2DUploadToScreen unsupported dst A8 dst:%p/%p", pDst, dst);
		return FALSE;
	}
#endif

#ifdef VIV2D_USERPTR

	uintptr_t page_start = ((uintptr_t)src / (PAGE_SIZE * src_pitch)) * (PAGE_SIZE * src_pitch);
	char *start_buf = (char *)page_start;

	src_x = (((uintptr_t)src - page_start) % src_pitch) / 4;
	src_y = (((uintptr_t)src - page_start) / src_pitch);
	size_t aligned_size = PAGE_ALIGN(src_pitch * (h + src_y + 1));

	VIV2D_INFO_MSG("Viv2DUploadToScreen page aligned %p/%p %dx%d %x", start_buf, src, src_x, src_y, aligned_size);

//	src_x = 0;
//	src_y = 0;
	if (aligned_size < 1024 * 1024 * 16) {

		VIV2D_INFO_MSG("Viv2DUploadToScreen page aligned %p %dx%d", src, src_x, src_y);
		struct etna_bo *aligned_bo = etna_bo_from_usermem_prot(v2d->dev, start_buf, aligned_size, ETNA_USERPTR_READ);
		if (aligned_bo) {
			tmp = calloc(sizeof (*tmp), 1);
			VIV2D_INFO_MSG("Viv2DUploadToScreen create usermem %p/%p %d %dx%d:%dx%d %x", start_buf, src, src_pitch, src_x, src_y, w, h, aligned_size);
			tmp->bo = aligned_bo;
			tmp->width = w;
			tmp->height = h;
			tmp->pitch = src_pitch;
			use_usermem = 1;
		} else {
			src_x = 0;
			src_y = 0;
			VIV2D_INFO_MSG("Viv2DUploadToScreen cannot create usermem");
			tmp = _Viv2DOpCreateTmpPix(v2d, w, h, pDst->drawable.bitsPerPixel);
		}
	} else {
		src_x = 0;
		src_y = 0;
		VIV2D_INFO_MSG("Viv2DUploadToScreen cannot create usermem : two large alignement %ld", aligned_size);
		tmp = _Viv2DOpCreateTmpPix(v2d, w, h, pDst->drawable.bitsPerPixel);
	}
#else
	tmp = _Viv2DOpCreateTmpPix(v2d, w, h, pDst->drawable.bitsPerPixel);
#endif

	tmp->format = tmp_fmt;

	dst->refcnt++;

	if (!use_usermem) {
		pitch = tmp->pitch;

		src_buf = src ;
		buf = (char *) etna_bo_map(tmp->bo);

		while (height--) {
			neon_memcpy(buf, src_buf, pitch);
//		memcpy(buf, src_buf, pitch);
			src_buf += src_pitch;
			buf += pitch;
		}
	}

	rects[0].x1 = x;
	rects[0].y1 = y;
	rects[0].x2 = x + w;
	rects[0].y2 = y + h;

	_Viv2DStreamReserve(v2d, VIV2D_SRC_PIX_RES + VIV2D_DEST_RES + VIV2D_BLEND_OFF_RES + VIV2D_RECTS_RES(1) + VIV2D_FLUSH_RES);
	_Viv2DStreamSrc(v2d, tmp, src_x, src_y, tmp->width, tmp->height); // tmp source
	_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, ROP_SRC, NULL);
	_Viv2DStreamBlendOp(v2d, NULL, 0, 0, FALSE, FALSE);
	_Viv2DStreamRects(v2d, rects, 1);

#ifdef VIV2D_FLUSH_OPS
	_Viv2DStreamFlush(v2d);
#endif

	VIV2D_INFO_MSG("Viv2DUploadToScreen blit done dst:%p/%p bo:%p buf:%p src_buf:%p(%d/%d) %dx%d(%dx%d) %dx%d %d/%d",
	               pDst, dst, dst->bo, etna_bo_map(dst->bo),
	               src, src_pitch, tmp->pitch, x, y, w, h,
	               pDst->drawable.width, pDst->drawable.height,
	               pDst->drawable.depth, pDst->drawable.bitsPerPixel);

#ifdef VIV2D_USERPTR
	if (use_usermem) {
		_Viv2DStreamCommit(v2d, TRUE);
		etna_bo_wait(v2d->dev, v2d->pipe, tmp->bo, 5000000000);
		etna_bo_del(tmp->bo);
		free(tmp);
	} else {
		_Viv2DOpDelTmpPix(v2d, tmp);
	}
#else
	_Viv2DOpDelTmpPix(v2d, tmp);
#endif

	return TRUE;
}
#endif

#ifdef VIV2D_DOWNLOAD_FROM_SCREEN
/**
 * DownloadFromScreen() loads a rectangle of data from pSrc into dst
 *
 * @param pSrc source pixmap
 * @param x source X coordinate.
 * @param y source Y coordinate
 * @param width width of the rectangle to be copied
 * @param height height of the rectangle to be copied
 * @param dst pointer to the beginning of the destination data
 * @param dst_pitch pitch (in bytes) of the lines of destination data.
 *
 * DownloadFromScreen() copies data from offscreen memory in pSrc from
 * (x, y) to (x + width, y + height), to system memory starting at
 * dst (with pitch dst_pitch).  This would usually be done
 * using scatter-gather DMA, supported by a DRM call, or by blitting to AGP
 * and then synchronously reading from AGP.  Because the implementation
 * might be synchronous, EXA leaves it up to the driver to call
 * exaMarkSync() if DownloadFromScreen() was asynchronous.  This is in
 * contrast to most other acceleration calls in EXA.
 *
 * DownloadFromScreen() can aid in the largest bottleneck in pixmap
 * migration, which is the read from framebuffer when evicting pixmaps from
 * framebuffer memory.  Thus, it is highly recommended, even though
 * implementations are typically complicated.
 *
 * @return TRUE if the driver successfully downloaded the data.  FALSE
 * indicates that EXA should fall back to doing the download in software.
 *
 * DownloadFromScreen() is not required, but is highly recommended.
 */

static Bool Viv2DDownloadFromScreen(PixmapPtr pSrc,
                                    int x, int y,
                                    int w, int h, char *dst, int dst_pitch) {

	int i, pitch;
	ScrnInfoPtr pScrn = pix2scrn(pSrc);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DPixmapPrivPtr src = Viv2DPixmapPrivFromPixmap(pSrc);
	Viv2DRect rects[1];
	Viv2DPixmapPrivPtr tmp;
	char *dst_buf, *src_buf, *buf;
	Viv2DFormat tmp_fmt;

	if (w * h < 4)
		return FALSE;

	if (!src->bo)
		return FALSE;

	if (!_Viv2DSetFormat(pSrc->drawable.depth, pSrc->drawable.bitsPerPixel, &src->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DUploadToScreen unsupported src format %d/%d %p", pSrc->drawable.depth, pSrc->drawable.bitsPerPixel, src);
		return FALSE;
	}

	if (!_Viv2DSetFormat(pSrc->drawable.depth, pSrc->drawable.bitsPerPixel, &tmp_fmt)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DUploadToScreen unsupported format %d/%d %p", pSrc->drawable.depth, pSrc->drawable.bitsPerPixel, src);
		return FALSE;
	}

	tmp = _Viv2DOpCreateTmpPix(v2d, w, h, pSrc->drawable.bitsPerPixel);
	tmp->format = tmp_fmt;

	pitch = tmp->pitch;

	rects[0].x1 = x;
	rects[0].y1 = y;
	rects[0].x2 = x + w;
	rects[0].y2 = y + h;

	_Viv2DStreamReserve(v2d, VIV2D_SRC_PIX_RES + VIV2D_DEST_RES + VIV2D_BLEND_OFF_RES + VIV2D_RECTS_RES(1));
	_Viv2DStreamSrc(v2d, src, 0, 0, tmp->width, tmp->height);
	_Viv2DStreamDst(v2d, tmp, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, ROP_SRC, NULL);
	_Viv2DStreamBlendOp(v2d, NULL, 0, 0, FALSE, FALSE);
	_Viv2DStreamRects(v2d, rects, 1);

	_Viv2DStreamCommit(v2d, TRUE);
	_Viv2DStreamWait(v2d);

	etna_bo_cpu_prep(tmp->bo, DRM_ETNA_PREP_READ, 5000000000);

	dst_buf = dst;
	src_buf = (char *) etna_bo_map(tmp->bo);
	buf = src_buf;

	for (i = 0; i < h; i++) {
//		memcpy(dst_buf, buf, pitch);
		neon_memcpy(dst_buf, buf, pitch);
		dst_buf += dst_pitch;
		buf += pitch;
	}

	etna_bo_cpu_fini(tmp->bo);

//	munmap(src_buf, etna_bo_size(tmp->bo));

	VIV2D_INFO_MSG("Viv2DDownloadFromScreen blit done %p %p %p(%d/%d) %dx%d(%dx%d) %dx%d %d/%d", pSrc, etna_bo_map(src->bo), src, pitch, tmp->pitch, x, y, w, h,
	               pSrc->drawable.width, pSrc->drawable.height,
	               pSrc->drawable.depth, pSrc->drawable.bitsPerPixel);

	_Viv2DOpDelTmpPix(v2d, tmp);

	return TRUE;
}
#endif

#ifdef VIV2D_SOLID
/** @name Solid
 * @{
 */
/**
 * PrepareSolid() sets up the driver for doing a solid fill.
 * @param pPixmap Destination pixmap
 * @param alu raster operation
 * @param planemask write mask for the fill
 * @param fg "foreground" color for the fill
 *
 * This call should set up the driver for doing a series of solid fills
 * through the Solid() call.  The alu raster op is one of the GX*
 * graphics functions listed in X.h, and typically maps to a similar
 * single-byte "ROP" setting in all hardware.  The planemask controls
 * which bits of the destination should be affected, and will only represent
 * the bits up to the depth of pPixmap.  The fg is the pixel value of the
 * foreground color referred to in ROP descriptions.
 *
 * Note that many drivers will need to store some of the data in the driver
 * private record, for sending to the hardware with each drawing command.
 *
 * The PrepareSolid() call is required of all drivers, but it may fail for any
 * reason.  Failure results in a fallback to software rendering.
 */
static Bool Viv2DPrepareSolid (PixmapPtr pPixmap,
                               int alu, Pixel planemask, Pixel fg) {
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DPixmapPrivPtr dst = Viv2DPixmapPrivFromPixmap(pPixmap);

	if (!dst->bo) {
		// CPU only
		return FALSE;
	}

	if (alu != GXcopy) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareSolid unsupported alu dst:%p/%p alu:%d", pPixmap, dst, alu);
		return FALSE;
	}

	if (!EXA_PM_IS_SOLID(&pPixmap->drawable, planemask)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareSolid unsupported planemask dst:%p/%p mask:%x", pPixmap, dst, (uint32_t)planemask);
		return FALSE;
	}

	if (!_Viv2DSetFormat(pPixmap->drawable.depth, pPixmap->drawable.bitsPerPixel, &dst->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareSolid dst:%p/%p unsupported format for depth:%d bpp:%d", pPixmap, dst, pPixmap->drawable.depth, pPixmap->drawable.bitsPerPixel);
		return FALSE;
	}

#ifndef VIV2D_SUPPORT_A8_DST
	if (dst->format.fmt == DE_FORMAT_A8) {
//		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareSolid unsupported dst A8 dst:%p/%p  fg:%x", pPixmap, dst, fg);
		return FALSE;
	}
#endif

	dst->refcnt++;

	_Viv2DOpInit(&v2d->op);
	v2d->op.mask = (uint32_t)planemask;
	v2d->op.fg = Viv2DColour(fg, pPixmap->drawable.depth);
	v2d->op.dst = dst;

	VIV2D_DBG_MSG("Viv2DPrepareSolid dst:%p/%p %dx%d, fg:%08x mask:%08x depth:%d alu:%d", pPixmap,
	              dst, pPixmap->drawable.width, pPixmap->drawable.height, v2d->op.fg ,
	              v2d->op.mask, pPixmap->drawable.depth, alu);

	return TRUE;
}

/**
 * Solid() performs a solid fill set up in the last PrepareSolid() call.
 *
 * @param pPixmap destination pixmap
 * @param x1 left coordinate
 * @param y1 top coordinate
 * @param x2 right coordinate
 * @param y2 bottom coordinate
 *
 * Performs the fill set up by the last PrepareSolid() call, covering the
 * area from (x1,y1) to (x2,y2) in pPixmap.  Note that the coordinates are
 * in the coordinate space of the destination pixmap, so the driver will
 * need to set up the hardware's offset and pitch for the destination
 * coordinates according to the pixmap's offset and pitch within
 * framebuffer.  This likely means using exaGetPixmapOffset() and
 * exaGetPixmapPitch().
 *
 * This call is required if PrepareSolid() ever succeeds.
 */
static void Viv2DSolid (PixmapPtr pPixmap, int x1, int y1, int x2, int y2) {
	Viv2DRec *v2d = Viv2DPrivFromPixmap(pPixmap);
	if (v2d->op.cur_rect >= VIV2D_MAX_RECTS)
	{
		_Viv2DStreamSolid(v2d, v2d->op.dst, v2d->op.fg, v2d->op.rects, v2d->op.cur_rect);
		v2d->op.cur_rect = 0;
	}

	_Viv2DOpAddRect(&v2d->op, x1, y1, x2 - x1, y2 - y1);
	VIV2D_DBG_MSG("Viv2DSolid dst:%p %dx%d:%dx%d %d", v2d->op.dst, x1, y1, x2, y2, v2d->op.cur_rect);
}

/**
 * DoneSolid() finishes a set of solid fills.
 *
 * @param pPixmap destination pixmap.
 *
 * The DoneSolid() call is called at the end of a series of consecutive
 * Solid() calls following a successful PrepareSolid().  This allows drivers
 * to finish up emitting drawing commands that were buffered, or clean up
 * state from PrepareSolid().
 *
 * This call is required if PrepareSolid() ever succeeds.
 */
static void Viv2DDoneSolid (PixmapPtr pPixmap) {
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);

	if (v2d->op.cur_rect > 0) {
		_Viv2DStreamSolid(v2d, v2d->op.dst, v2d->op.fg, v2d->op.rects, v2d->op.cur_rect);
	}

	VIV2D_DBG_MSG("Viv2DDoneSolid dst:%p/%p %d", pPixmap, v2d->op.dst, v2d->stream->offset);

#ifdef VIV2D_TRACE
	_Viv2DStreamCommit(v2d, TRUE);
	etna_bo_cpu_prep(v2d->op.dst->bo, DRM_ETNA_PREP_READ, 5000000000);
	_Viv2DPixTrace(v2d->op.dst, "soli");
	etna_bo_cpu_fini(v2d->op.dst->bo);
#endif
}

/** @} */
#else
static Bool
PrepareSolidFail(PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fill_colour)
{
	return FALSE;
}
#endif

#ifdef VIV2D_COPY
/** @name Copy
 * @{
 */
/**
 * PrepareCopy() sets up the driver for doing a copy within video
 * memory.
 *
 * @param pSrcPixmap source pixmap
 * @param pDstPixmap destination pixmap
 * @param dx X copy direction
 * @param dy Y copy direction
 * @param alu raster operation
 * @param planemask write mask for the fill
 *
 * This call should set up the driver for doing a series of copies from the
 * the pSrcPixmap to the pDstPixmap.  The dx flag will be positive if the
 * hardware should do the copy from the left to the right, and dy will be
 * positive if the copy should be done from the top to the bottom.  This
 * is to deal with self-overlapping copies when pSrcPixmap == pDstPixmap.
 * If your hardware can only support blits that are (left to right, top to
 * bottom) or (right to left, bottom to top), then you should set
 * #EXA_TWO_BITBLT_DIRECTIONS, and EXA will break down Copy operations to
 * ones that meet those requirements.  The alu raster op is one of the GX*
 * graphics functions listed in X.h, and typically maps to a similar
 * single-byte "ROP" setting in all hardware.  The planemask controls which
 * bits of the destination should be affected, and will only represent the
 * bits up to the depth of pPixmap.
 *
 * Note that many drivers will need to store some of the data in the driver
 * private record, for sending to the hardware with each drawing command.
 *
 * The PrepareCopy() call is required of all drivers, but it may fail for any
 * reason.  Failure results in a fallback to software rendering.
 */
static Bool Viv2DPrepareCopy (PixmapPtr pSrcPixmap,
                              PixmapPtr pDstPixmap,
                              int dx, int dy, int alu, Pixel planemask) {
	ScrnInfoPtr pScrn = pix2scrn(pDstPixmap);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DPixmapPrivPtr src = Viv2DPixmapPrivFromPixmap(pSrcPixmap);
	Viv2DPixmapPrivPtr dst = Viv2DPixmapPrivFromPixmap(pDstPixmap);

	if (!src->bo || !dst->bo) {
		// CPU only
		return FALSE;
	}

	if (alu != GXcopy) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareCopy unsupported alu src:%p/%p dst:%p/%p alu:%d", pSrcPixmap, src, pDstPixmap, dst, alu);
		return FALSE;
	}

	if (!_Viv2DSetFormat(pSrcPixmap->drawable.depth, pSrcPixmap->drawable.bitsPerPixel, &src->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareCopy unsupported format src:%p/%p depth:%d bpp:%d", pSrcPixmap, src, pSrcPixmap->drawable.depth, pSrcPixmap->drawable.bitsPerPixel);
		return FALSE;
	}
	if (!_Viv2DSetFormat(pDstPixmap->drawable.depth, pDstPixmap->drawable.bitsPerPixel, &dst->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareCopy unsupported format dst:%p/%p depth:%d bpp:%d", pDstPixmap, dst, pDstPixmap->drawable.depth, pDstPixmap->drawable.bitsPerPixel);
		return FALSE;
	}

#ifndef VIV2D_SUPPORT_A8_DST
	if (dst->format.fmt == DE_FORMAT_A8) {
//		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareCopy unsupported dst A8 dst:%p/%p", pDstPixmap, dst);
		return FALSE;
	}
#endif

	dst->refcnt++;

	_Viv2DOpInit(&v2d->op);
	v2d->op.mask = (uint32_t)planemask;
	v2d->op.src = src;
	v2d->op.dst = dst;
#ifdef VIV2D_COPY_BLEND
	v2d->op.blend_op = &viv2d_blend_op[PictOpSrc];
#else
	v2d->op.blend_op = NULL;
#endif

	VIV2D_DBG_MSG("Viv2DPrepareCopy  src:%p/%p(%dx%d)[%s/%s] dst:%p/%p(%dx%d)[%s/%s] dir:%dx%d alu:%d planemask:%x",
	              pSrcPixmap, src, src->width, src->height, Viv2DFormatColorStr(&src->format), Viv2DFormatSwizzleStr(&src->format),
	              pDstPixmap, dst, dst->width, dst->height, Viv2DFormatColorStr(&dst->format), Viv2DFormatSwizzleStr(&dst->format),
	              dx, dy, alu, (uint32_t)planemask);

	return TRUE;
};

/**
 * Copy() performs a copy set up in the last PrepareCopy call.
 *
 * @param pDstPixmap destination pixmap
 * @param srcX source X coordinate
 * @param srcY source Y coordinate
 * @param dstX destination X coordinate
 * @param dstY destination Y coordinate
 * @param width width of the rectangle to be copied
 * @param height height of the rectangle to be copied.
 *
 * Performs the copy set up by the last PrepareCopy() call, copying the
 * rectangle from (srcX, srcY) to (srcX + width, srcY + width) in the source
 * pixmap to the same-sized rectangle at (dstX, dstY) in the destination
 * pixmap.  Those rectangles may overlap in memory, if
 * pSrcPixmap == pDstPixmap.  Note that this call does not receive the
 * pSrcPixmap as an argument -- if it's needed in this function, it should
 * be stored in the driver private during PrepareCopy().  As with Solid(),
 * the coordinates are in the coordinate space of each pixmap, so the driver
 * will need to set up source and destination pitches and offsets from those
 * pixmaps, probably using exaGetPixmapOffset() and exaGetPixmapPitch().
 *
 * This call is required if PrepareCopy ever succeeds.
 */
static void Viv2DCopy (PixmapPtr pDstPixmap,
                       int srcX,
                       int srcY, int dstX, int dstY, int width, int height) {
	Viv2DRec *v2d = Viv2DPrivFromPixmap(pDstPixmap);

	// new srcX,srcY group
	if (v2d->op.prev_src_x != srcX || v2d->op.prev_src_y != srcY || v2d->op.cur_rect >= VIV2D_MAX_RECTS) {
		// stream previous rects
		if (v2d->op.prev_src_x > -1) {
			// create states for srcX,srcY group
			_Viv2DStreamCopy(v2d, v2d->op.src, v2d->op.dst, v2d->op.blend_op,
			                 v2d->op.src_alpha_mode_global, v2d->op.src_alpha,
			                 v2d->op.dst_alpha_mode_global, v2d->op.dst_alpha,

			                 v2d->op.prev_src_x, v2d->op.prev_src_y, v2d->op.prev_width, v2d->op.prev_height,
			                 v2d->op.rects, v2d->op.cur_rect);

			v2d->op.cur_rect = 0;
		}

	}

	_Viv2DOpAddRect(&v2d->op, dstX, dstY, width, height);

	v2d->op.prev_src_x = srcX;
	v2d->op.prev_src_y = srcY;
	v2d->op.prev_width = width;
	v2d->op.prev_height = height;

	VIV2D_DBG_MSG("Viv2DCopy src:%p(%dx%d) dst:%p/%p(%dx%d) : %dx%d %d", v2d->op.src, srcX, srcY, pDstPixmap, v2d->op.dst, dstX, dstY, width, height, v2d->op.cur_rect);
}

/**
 * DoneCopy() finishes a set of copies.
 *
 * @param pPixmap destination pixmap.
 *
 * The DoneCopy() call is called at the end of a series of consecutive
 * Copy() calls following a successful PrepareCopy().  This allows drivers
 * to finish up emitting drawing commands that were buffered, or clean up
 * state from PrepareCopy().
 *
 * This call is required if PrepareCopy() ever succeeds.
 */
static void Viv2DDoneCopy (PixmapPtr pDstPixmap) {
	ScrnInfoPtr pScrn = pix2scrn(pDstPixmap);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);

	_Viv2DStreamCopy(v2d, v2d->op.src, v2d->op.dst, v2d->op.blend_op,
	                 v2d->op.src_alpha_mode_global, v2d->op.src_alpha,
	                 v2d->op.dst_alpha_mode_global, v2d->op.dst_alpha,

	                 v2d->op.prev_src_x, v2d->op.prev_src_y, v2d->op.prev_width, v2d->op.prev_height,
	                 v2d->op.rects, v2d->op.cur_rect);

	VIV2D_DBG_MSG("Viv2DDoneCopy dst:%p/%p %d", pDstPixmap, v2d->op.dst, v2d->stream->offset);

#ifdef VIV2D_TRACE
	_Viv2DStreamCommit(v2d, TRUE);
	etna_bo_cpu_prep(v2d->op.dst->bo, DRM_ETNA_PREP_READ, 5000000000);
	_Viv2DPixTrace(v2d->op.dst, "copy");
	etna_bo_cpu_fini(v2d->op.dst->bo);
#endif

}
/** @} */
#else
static Bool
PrepareCopyFail(PixmapPtr pSrc, PixmapPtr pDst, int xdir, int ydir,
                int alu, Pixel planemask)
{
	return FALSE;
}
#endif

#ifdef VIV2D_COMPOSITE

static PixmapPtr
GetDrawablePixmap(DrawablePtr pDrawable) {
	/* Make sure there is a drawable. */
	if (NULL == pDrawable) {
		return NULL;
	}

	/* Check for a backing pixmap. */
	if (DRAWABLE_WINDOW == pDrawable->type) {

		WindowPtr pWindow = (WindowPtr) pDrawable;
		return pDrawable->pScreen->GetWindowPixmap(pWindow);
	}

	/* Otherwise, it's a regular pixmap. */
	return (PixmapPtr) pDrawable;
}

static Bool Viv2DGetPictureFormat(int exa_fmt, Viv2DFormat * fmt) {
	int i;
	Bool isFound = FALSE;
	int size = VIV2D_PICT_FORMAT_COUNT;

	for (i = 0; i < size && !isFound; i++) {
		if (exa_fmt == viv2d_pict_format[i].exaFmt) {
			*fmt = (viv2d_pict_format[i]);
			isFound = TRUE;
		}
	}
	/*May be somehow usable*/
	if (!isFound) {
		*fmt = viv2d_pict_format[size - 1];
		fmt->exaFmt = exa_fmt;
	}
	return isFound;
}

/**
 * CheckComposite() checks to see if a composite operation could be
 * accelerated.
 *
 * @param op Render operation
 * @param pSrcPicture source Picture
 * @param pMaskPicture mask picture
 * @param pDstPicture destination Picture
 *
 * The CheckComposite() call checks if the driver could handle acceleration
 * of op with the given source, mask, and destination pictures.  This allows
 * drivers to check source and destination formats, supported operations,
 * transformations, and component alpha state, and send operations it can't
 * support to software rendering early on.  This avoids costly pixmap
 * migration to the wrong places when the driver can't accelerate
 * operations.  Note that because migration hasn't happened, the driver
 * can't know during CheckComposite() what the offsets and pitches of the
 * pixmaps are going to be.
 *
 * See PrepareComposite() for more details on likely issues that drivers
 * will have in accelerating Composite operations.
 *
 * The CheckComposite() call is recommended if PrepareComposite() is
 * implemented, but is not required.
 */
static Bool
Viv2DCheckComposite (int op,
                     PicturePtr pSrcPicture,
                     PicturePtr pMaskPicture, PicturePtr pDstPicture) {
	PixmapPtr pSrc = GetDrawablePixmap(pSrcPicture->pDrawable);
	PixmapPtr pMask = NULL;
	PixmapPtr pDst = GetDrawablePixmap(pDstPicture->pDrawable);

	Viv2DFormat src_fmt;
	Viv2DFormat msk_fmt;
	Viv2DFormat dst_fmt;

	if (pDst == NULL) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported dest is not a drawable");
		return FALSE;
	}

	/*Format Checks*/
	if (!Viv2DGetPictureFormat(pSrcPicture->format, &src_fmt)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported src format %s", pix_format_name(pSrcPicture->format));
		return FALSE;
	}

#ifndef VIV2D_SUPPORT_A8_SRC
	// src A8 seems to have a problem
	if (src_fmt.fmt == DE_FORMAT_A8 && PICT_FORMAT_A(pDstPicture->format) != 0)
	{
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported src A8 with dest %s", pix_format_name(pDstPicture->format));
		return FALSE;
	}
#endif

	if (!Viv2DGetPictureFormat(pDstPicture->format, &dst_fmt)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite dst:%p unsupported dst format %s", pDst, pix_format_name(pDstPicture->format));
		return FALSE;
	}

#ifndef VIV2D_SUPPORT_A8_DST
	if (dst_fmt.fmt == DE_FORMAT_A8) {
//		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported dst A8 dst:%p", pDst);
		return FALSE;
	}
#endif

	/*For forward compatibility*/
	if (op > BLEND_SIZE) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported op : dst:%p op:%s", pDst, pix_op_name(op));
		return FALSE;
	}

	if (pMaskPicture) {
		pMask = GetDrawablePixmap(pMaskPicture->pDrawable);
	}

	if (pSrc == NULL) {
#ifdef VIV2D_SOLID_PICTURE_SRC
		SourcePict *sp = pSrcPicture->pSourcePict;

		if (sp->type != SourcePictTypeSolidFill) {
			VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported src is not SourcePictTypeSolidFill type:%d", sp->type);
			return FALSE;
		}
#else
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported src is not a drawable");
		return FALSE;
#endif
	}

	if ( pSrcPicture->transform ) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite src transform unsupported");
		return FALSE;
	}

	if (pMaskPicture) {

		if (!Viv2DGetPictureFormat(pMaskPicture->format, &msk_fmt)) {
			VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported mask format msk:%p %s", pMask, pix_format_name(pMaskPicture->format));
			return FALSE;
		}

#ifndef VIV2D_SUPPORT_A8_MASK
		if (msk_fmt.fmt == DE_FORMAT_A8 && PICT_FORMAT_A(pDstPicture->format) != 0)
		{
			VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported mask A8 with dest %s", pix_format_name(pDstPicture->format));
			return FALSE;
		}
#endif

		if (pMaskPicture->transform) {
			VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite mask transform unsupported");
			return FALSE;
		}

		if (pMask == NULL) {
#ifdef VIV2D_SOLID_PICTURE_MSK
			SourcePict *sp = pMaskPicture->pSourcePict;

			if (sp->type != SourcePictTypeSolidFill) {
				VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported msk is not SourcePictTypeSolidFill type:%d", sp->type);
				return FALSE;
			}
#else
			VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported mask is not a drawable");
			return FALSE;
#endif
		}

		if (pMaskPicture->repeat && pMask) {
			if (pMask->drawable.width == 1 && pMask->drawable.height == 1) {
				// 1x1 stretch
			} else {
				VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite mask repeat > 1x1 unsupported");
				return FALSE;
			}

		}
	}
	if ( pSrcPicture->repeat && pSrc) {
#ifdef VIV2D_REPEAT
#ifndef VIV2D_REPEAT_WITH_MASK
		if (pMaskPicture != NULL) {
			VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite repeat with mask unsupported");
			return FALSE;
		}
#endif

		if (pSrc->drawable.width == 1 && pSrc->drawable.height == 1) {
			// 1x1 stretch
		} else {
			VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite repeat > 1x1 unsupported");
			return FALSE;
		}
#else
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite repeat unsupported");
		return FALSE;
#endif
	}

#ifndef VIV2D_MASK_SUPPORT
	if ((pMaskPicture != NULL)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite mask unsupported");
		return FALSE;
	}
#endif

	return TRUE;
}

/**
 * PrepareComposite() sets up the driver for doing a Composite operation
 * described in the Render extension protocol spec.
 *
 * @param op Render operation
 * @param pSrcPicture source Picture
 * @param pMaskPicture mask picture
 * @param pDstPicture destination Picture
 * @param pSrc source pixmap
 * @param pMask mask pixmap
 * @param pDst destination pixmap
 *
 * This call should set up the driver for doing a series of Composite
 * operations, as described in the Render protocol spec, with the given
 * pSrcPicture, pMaskPicture, and pDstPicture.  The pSrc, pMask, and
 * pDst are the pixmaps containing the pixel data, and should be used for
 * setting the offset and pitch used for the coordinate spaces for each of
 * the Pictures.
 *
 * Notes on interpreting Picture structures:
 * - The Picture structures will always have a valid pDrawable.
 * - The Picture structures will never have alphaMap set.
 * - The mask Picture (and therefore pMask) may be NULL, in which case the
 *   operation is simply src OP dst instead of src IN mask OP dst, and
 *   mask coordinates should be ignored.
 * - pMarkPicture may have componentAlpha set, which greatly changes
 *   the behavior of the Composite operation.  componentAlpha has no effect
 *   when set on pSrcPicture or pDstPicture.
 * - The source and mask Pictures may have a transformation set
 *   (Picture->transform != NULL), which means that the source coordinates
 *   should be transformed by that transformation, resulting in scaling,
 *   rotation, etc.  The PictureTransformPoint() call can transform
 *   coordinates for you.  Transforms have no effect on Pictures when used
 *   as a destination.
 * - The source and mask pictures may have a filter set.  PictFilterNearest
 *   and PictFilterBilinear are defined in the Render protocol, but others
 *   may be encountered, and must be handled correctly (usually by
 *   PrepareComposite failing, and falling back to software).  Filters have
 *   no effect on Pictures when used as a destination.
 * - The source and mask Pictures may have repeating set, which must be
 *   respected.  Many chipsets will be unable to support repeating on
 *   pixmaps that have a width or height that is not a power of two.
 *
 * If your hardware can't support source pictures (textures) with
 * non-power-of-two pitches, you should set #EXA_OFFSCREEN_ALIGN_POT.
 *
 * Note that many drivers will need to store some of the data in the driver
 * private record, for sending to the hardware with each drawing command.
 *
 * The PrepareComposite() call is not required.  However, it is highly
 * recommended for performance of antialiased font rendering and performance
 * of cairo applications.  Failure results in a fallback to software
 * rendering.
 */
static Bool
Viv2DPrepareComposite(int rop, PicturePtr pSrcPicture,
                      PicturePtr pMaskPicture,
                      PicturePtr pDstPicture,
                      PixmapPtr pSrc, PixmapPtr pMask, PixmapPtr pDst) {
	ScrnInfoPtr pScrn = pix2scrn(pDst);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DPixmapPrivPtr src = NULL;
	Viv2DPixmapPrivPtr msk = NULL;
	Viv2DPixmapPrivPtr dst = Viv2DPixmapPrivFromPixmap(pDst);

	Viv2DFormat src_fmt;
	Viv2DFormat msk_fmt;

	if (pSrc != NULL) {
		src = Viv2DPixmapPrivFromPixmap(pSrc);
	}

	if ((src && !src->bo) || !dst->bo) {
		// CPU only
		return FALSE;
	}

	if (pSrcPicture != NULL) {
		if (!Viv2DGetPictureFormat(pSrcPicture->format, &src_fmt)) {
			VIV2D_UNSUPPORTED_MSG("Viv2DPrepareComposite unsupported src format %s", pix_format_name(pSrcPicture->format));
			return FALSE;
		}
		if (pSrcPicture->filter) {
			VIV2D_UNSUPPORTED_MSG("Viv2DPrepareComposite unsupported src filter %d", pMaskPicture->filter);
			return FALSE;
		}
	}

	if (pMask != NULL) {
		msk = Viv2DPixmapPrivFromPixmap(pMask);
	}

	if (msk && !msk->bo) {
		// CPU only
		return FALSE;
	}

	if (pMaskPicture != NULL) {
		if (!Viv2DGetPictureFormat(pMaskPicture->format, &msk_fmt)) {
			VIV2D_UNSUPPORTED_MSG("Viv2DPrepareComposite unsupported msk format %s", pix_format_name(pMaskPicture->format));
			return FALSE;
		}

		if (pMaskPicture->filter) {
			VIV2D_UNSUPPORTED_MSG("Viv2DPrepareComposite unsupported msk filter %d", pMaskPicture->filter);
			return FALSE;
		}

#ifndef VIV2D_MASK_COMPONENT_SUPPORT
		if (pMaskPicture->componentAlpha) {
			VIV2D_UNSUPPORTED_MSG("Viv2DPrepareComposite unsupported msk component alpha");
			return FALSE;
		}
#endif
	}

	if (!Viv2DGetPictureFormat(pDstPicture->format, &dst->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareComposite unsupported dst format %s", pix_format_name(pDstPicture->format));
		return FALSE;
	}

	dst->refcnt++;

	_Viv2DOpInit(&v2d->op);

	v2d->op.blend_op = &viv2d_blend_op[rop];

	if (pMaskPicture != NULL) {
		v2d->op.has_mask = TRUE;
	}

	v2d->op.src_alpha_mode_global = FALSE;
	v2d->op.msk_alpha_mode_global = FALSE;
	v2d->op.dst_alpha_mode_global = FALSE;
	v2d->op.src_alpha = 0;
	v2d->op.msk_alpha = 0;
	v2d->op.dst_alpha = 0;

	// src type
	v2d->op.src_type = viv2d_src_pix;

	if (pSrc != NULL && pSrcPicture->repeat && pSrc->drawable.width == 1 && pSrc->drawable.height == 1) {
#ifdef VIV2D_1X1_REPEAT_AS_SOLID
// armada way
		v2d->op.src_type = viv2d_src_solid;
		v2d->op.fg = Viv2DColour(Viv2DGetFirstPixel(&pSrc->drawable), src_fmt.depth);
#else
		v2d->op.src_type = viv2d_src_1x1_repeat;
#endif
	}

	if (pSrc == NULL && pSrcPicture->pSourcePict->type == SourcePictTypeSolidFill) {
		v2d->op.src_type = viv2d_src_solid;
		v2d->op.fg = Viv2DColour(pSrcPicture->pSourcePict->solidFill.color, src_fmt.depth);
	}

	if (pMaskPicture != NULL) {
		// msk type
		v2d->op.msk_type = viv2d_src_pix;

		if (pMask != NULL && pMaskPicture->repeat && pMask->drawable.width == 1 && pMask->drawable.height == 1) {
#ifdef VIV2D_1X1_REPEAT_AS_SOLID
// armada way
			v2d->op.msk_type = viv2d_src_solid;
			v2d->op.mask = Viv2DColour(Viv2DGetFirstPixel(&pMask->drawable), msk_fmt.depth);
#else
			v2d->op.msk_type = viv2d_src_1x1_repeat;
#endif
		}

		if (pMask == NULL && pMaskPicture->pSourcePict->type == SourcePictTypeSolidFill) {
			v2d->op.msk_type = viv2d_src_solid;
			v2d->op.mask = Viv2DColour(pMaskPicture->pSourcePict->solidFill.color, msk_fmt.depth);
		}
	}

	if (src != NULL)
		src->format = src_fmt;

	v2d->op.msk_fmt = msk_fmt;
	v2d->op.src_fmt = src_fmt;

	v2d->op.src = src;
	v2d->op.dst = dst;
	v2d->op.msk = msk;

#ifdef VIV2D_MASK_COMPONENT_SUPPORT
	if (pMaskPicture && pMaskPicture->componentAlpha) {
		v2d->op.has_component_alpha = TRUE;
	}
#endif

	if (v2d->op.src_type == viv2d_src_solid) {
		v2d->op.src_alpha = v2d->op.fg >> 24;

		// VIV2D_INFO_MSG("set src global alpha %d has_mask:%d", v2d->op.src_alpha, v2d->op.has_mask);
	}

#ifdef VIV2D_DEBUG

	if (pSrcPicture != NULL) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite srcPicture:%s/%s",
		              Viv2DFormatColorStr(&src_fmt), Viv2DFormatSwizzleStr(&src_fmt));
	}

	if (src != NULL) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite src:%p/%p(%dx%d) depth:%d bpp:%d",
		              pSrc, src, src->width, src->height, pSrc->drawable.depth, pSrc->drawable.bitsPerPixel);
	}

	if (pMaskPicture != NULL) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite maskPicture:%s/%s",
		              Viv2DFormatColorStr(&msk_fmt), Viv2DFormatSwizzleStr(&msk_fmt));
	}

	if (msk != NULL) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite msk:%p/%p(%dx%d)",
		              pMask, msk, msk->width, msk->height);
	}

	if (v2d->op.src_type == viv2d_src_solid) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite solid src fg:%08x", v2d->op.fg);
	}

	if (v2d->op.msk_type == viv2d_src_solid) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite solid msk %08x", v2d->op.mask);
	}

	VIV2D_DBG_MSG("Viv2DPrepareComposite dst:%p/%p(%dx%d)[%s/%s] op:%d(%s)", pDst,
	              dst, dst->width, dst->height,
	              Viv2DFormatColorStr(&dst->format), Viv2DFormatSwizzleStr(&dst->format),
	              rop, pix_op_name(rop));

	if (msk) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite msk:%p/%p(%dx%d)[%s/%s] op:%d(%s)", pMask,
		              msk, msk->width, msk->height,
		              Viv2DFormatColorStr(&msk->format), Viv2DFormatSwizzleStr(&msk->format),
		              rop, pix_op_name(rop));

	} else {
		if (pMaskPicture != NULL) {
			VIV2D_DBG_MSG("Viv2DPrepareComposite solid msk %d %x[%s/%s] op:%d(%s)", v2d->op.msk_type, v2d->op.mask,
			              Viv2DFormatColorStr(&msk_fmt), Viv2DFormatSwizzleStr(&msk_fmt),
			              rop, pix_op_name(rop));
		}
	}

	if (src) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite src:%p/%p(%dx%d)[%s/%s] op:%d(%s)", pSrc,
		              src, src->width, src->height,
		              Viv2DFormatColorStr(&src->format), Viv2DFormatSwizzleStr(&src->format),
		              rop, pix_op_name(rop));
	} else {
		VIV2D_DBG_MSG("Viv2DPrepareComposite solid src %d %x[%s/%s] op:%d(%s)", v2d->op.src_type, v2d->op.fg,
		              Viv2DFormatColorStr(&src_fmt), Viv2DFormatSwizzleStr(&src_fmt),
		              rop, pix_op_name(rop));
	}
#endif

	return TRUE;
}

/**
     * Composite() performs a Composite operation set up in the last
     * PrepareComposite() call.
     *
     * @param pDstPixmap destination pixmap
     * @param srcX source X coordinate
     * @param srcY source Y coordinate
     * @param maskX source X coordinate
     * @param maskY source Y coordinate
     * @param dstX destination X coordinate
     * @param dstY destination Y coordinate
     * @param width destination rectangle width
     * @param height destination rectangle height
     *
     * Performs the Composite operation set up by the last PrepareComposite()
     * call, to the rectangle from (dstX, dstY) to (dstX + width, dstY + height)
     * in the destination Pixmap.  Note that if a transformation was set on
     * the source or mask Pictures, the source rectangles may not be the same
     * size as the destination rectangles and filtering.  Getting the coordinate
     * transformation right at the subpixel level can be tricky, and rendercheck
     * can test this for you.
     *
     * This call is required if PrepareComposite() ever succeeds.
     */
// dest = (source IN mask) OP dest
static void
Viv2DComposite(PixmapPtr pDst, int srcX, int srcY, int maskX, int maskY,
               int dstX, int dstY, int width, int height) {
	Viv2DRec *v2d = Viv2DPrivFromPixmap(pDst);
	Viv2DRect mrect[1], drect[1];

	mrect[0].x1 = 0;
	mrect[0].y1 = 0;
	mrect[0].x2 = width;
	mrect[0].y2 = height;

	drect[0].x1 = dstX;
	drect[0].y1 = dstY;
	drect[0].x2 = dstX + width;
	drect[0].y2 = dstY + height;

	if (v2d->op.has_mask) {
		// tmp 32bits argb pix
		Viv2DPixmapPrivPtr tmp;

		Viv2DBlendOp cpy_op = viv2d_blend_op[PictOpSrc];
		Viv2DBlendOp msk_op = viv2d_blend_op[PictOpInReverse];

#ifdef VIV2D_MASK_COMPONENT_SUPPORT
		if(v2d->op.has_component_alpha) {
			msk_op.src_blend_mode = DE_BLENDMODE_ZERO;
			msk_op.dst_blend_mode = DE_BLENDMODE_COLOR;			
		}
#endif

		tmp = _Viv2DOpCreateTmpPix(v2d, width, height, 32);
		_Viv2DSetFormat(32, 32, &tmp->format); // A8R8G8B8

		_Viv2DStreamCompAlpha(v2d, v2d->op.src_type, v2d->op.src, &v2d->op.src_fmt, v2d->op.fg, tmp, &cpy_op,
		                      v2d->op.src_alpha_mode_global, v2d->op.src_alpha,
		                      FALSE, 0,
		                      srcX, srcY, width, height, mrect, 1);

		_Viv2DStreamCompAlpha(v2d, v2d->op.msk_type, v2d->op.msk, &v2d->op.msk_fmt, v2d->op.mask, tmp, &msk_op,
		                      v2d->op.msk_alpha_mode_global, v2d->op.msk_alpha,
		                      FALSE, 0,
		                      maskX, maskY, width, height, mrect, 1);

		_Viv2DStreamCompAlpha(v2d, viv2d_src_pix, tmp, &tmp->format, 0, v2d->op.dst, v2d->op.blend_op,
		                      FALSE, 0,
		                      v2d->op.dst_alpha_mode_global, v2d->op.dst_alpha,
		                      0, 0, width, height, drect, 1);

		_Viv2DOpDelTmpPix(v2d, tmp);
	} else {
		// new srcX,srcY group
		if (v2d->op.prev_src_x != srcX || v2d->op.prev_src_y != srcY || v2d->op.cur_rect >= VIV2D_MAX_RECTS)
		{
			// stream previous rects
			if (v2d->op.prev_src_x > -1) {
				_Viv2DStreamCompAlpha(v2d, v2d->op.src_type, v2d->op.src, &v2d->op.src_fmt, v2d->op.fg, v2d->op.dst, v2d->op.blend_op,
				                      v2d->op.src_alpha_mode_global, v2d->op.src_alpha,
				                      v2d->op.dst_alpha_mode_global, v2d->op.dst_alpha,
				                      v2d->op.prev_src_x, v2d->op.prev_src_y, v2d->op.prev_width, v2d->op.prev_height,
				                      v2d->op.rects, v2d->op.cur_rect);

				v2d->op.cur_rect = 0;
			}
		}

		_Viv2DOpAddRect(&v2d->op, dstX, dstY, width, height);

		v2d->op.prev_src_x = srcX;
		v2d->op.prev_src_y = srcY;
		v2d->op.prev_width = width;
		v2d->op.prev_height = height;
	}

	VIV2D_DBG_MSG("Viv2DComposite (src:%p(%dx%d) IN msk:%dx%d) OP dst:%p(%dx%d:%dx%d) : %dx%d src_type:%d fg:%x msk_type:%d has_mask:%d mask:%x",
	              v2d->op.src, srcX, srcY, maskX, maskY,
	              v2d->op.dst, dstX, dstY, v2d->op.dst->width, v2d->op.dst->height,
	              width, height, v2d->op.src_type, v2d->op.fg, v2d->op.msk_type, v2d->op.has_mask, v2d->op.mask);

}

/**
     * DoneComposite() finishes a set of Composite operations.
     *
     * @param pPixmap destination pixmap.
     *
     * The DoneComposite() call is called at the end of a series of consecutive
     * Composite() calls following a successful PrepareComposite().  This allows
     * drivers to finish up emitting drawing commands that were buffered, or
     * clean up state from PrepareComposite().
     *
     * This call is required if PrepareComposite() ever succeeds.
     */
static void Viv2DDoneComposite (PixmapPtr pDst) {
	ScrnInfoPtr pScrn = pix2scrn(pDst);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);

	if (v2d->op.has_mask) {
		VIV2D_DBG_MSG("Viv2DDoneComposite with msk dst:%p %d", pDst, v2d->stream->offset);
		// already done masked operations
	} else {
		// last op
		_Viv2DStreamCompAlpha(v2d, v2d->op.src_type, v2d->op.src, &v2d->op.src_fmt, v2d->op.fg, v2d->op.dst, v2d->op.blend_op,
		                      v2d->op.src_alpha_mode_global, v2d->op.src_alpha,
		                      v2d->op.dst_alpha_mode_global, v2d->op.dst_alpha,
		                      v2d->op.prev_src_x, v2d->op.prev_src_y, v2d->op.prev_width, v2d->op.prev_height,
		                      v2d->op.rects, v2d->op.cur_rect);

		VIV2D_DBG_MSG("Viv2DDoneComposite dst:%p %d", pDst, v2d->stream->offset);
	}

#ifdef VIV2D_TRACE
	_Viv2DStreamCommit(v2d, TRUE);
	etna_bo_cpu_prep(v2d->op.dst->bo, DRM_ETNA_PREP_READ, 5000000000);
	_Viv2DPixTrace(v2d->op.dst, "comp");
	etna_bo_cpu_fini(v2d->op.dst->bo);
#endif
}

#else

static Bool
CheckCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
                   PicturePtr pDstPicture)
{
	return FALSE;
}

static Bool
PrepareCompositeFail(int op, PicturePtr pSrcPicture, PicturePtr pMaskPicture,
                     PicturePtr pDstPicture, PixmapPtr pSrc, PixmapPtr pMask, PixmapPtr pDst)
{
	return FALSE;
}

#endif

static Bool
CloseScreen(CLOSE_SCREEN_ARGS_DECL)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);

#ifdef VIV2D_FLUSH_CALLBACK
	DeleteCallback(&FlushCallback, Viv2DFlushCallback, pScrn);
#endif

	_Viv2DStreamCommit(v2d, FALSE);

	etna_bo_del(v2d->bo);
	etna_cmd_stream_del(v2d->stream);
	etna_pipe_del(v2d->pipe);
	etna_gpu_del(v2d->gpu);
	etna_device_del(v2d->dev);
	close(v2d->fd);

	exaDriverFini(pScreen);
	free(((Viv2DEXAPtr)pARMSOC->pARMSOCEXA)->exa);
	free(pARMSOC->pARMSOCEXA);
	pARMSOC->pARMSOCEXA = NULL;

	return TRUE;
}

static void
FreeScreen(FREE_SCREEN_ARGS_DECL)
{
}

// XV
#include "drm_fourcc.h"
static unsigned int Viv2DGetFormats(unsigned int *formats) {
	formats[0] = fourcc_code('U', 'Y', 'V', 'Y');
	formats[1] = fourcc_code('Y', 'U', 'Y', '2');
	formats[2] = fourcc_code('Y', 'V', '1', '2');
	formats[3] = fourcc_code('I', '4', '2', '0');
	return 4;
}

#define KERNEL_ROWS	17
#define KERNEL_INDICES	9
#define KERNEL_SIZE	(KERNEL_ROWS * KERNEL_INDICES)
#define KERNEL_STATE_SZ	((KERNEL_SIZE + 1) / 2)

static uint32_t xv_filter_kernel[KERNEL_STATE_SZ];

static inline float sinc(float x)
{
	return x != 0.0 ? sinf(x) / x : 1.0;
}

/*
 * Some interesting observations of the kernel.  According to the etnaviv
 * rnndb files:
 *  - there are 128 states which hold the kernel.
 *  - each entry contains 9 coefficients (one for each filter tap).
 *  - the entries are indexed by 5 bits from the fractional coordinate
 *    (which makes 32 entries.)
 *
 * As the kernel table is symmetrical around the centre of the fractional
 * coordinate, only half of the entries need to be stored.  In other words,
 * these pairs of indices should be the same:
 *
 *  00=31 01=30 02=29 03=28 04=27 05=26 06=25 07=24
 *  08=23 09=22 10=21 11=20 12=19 13=18 14=17 15=16
 *
 * This means that there are only 16 entries.  However, etnaviv
 * documentation says 17 are required.  What's the additional entry?
 *
 * The next issue is that the filter code always produces zero for the
 * ninth filter tap.  If this is always zero, what's the point of having
 * hardware deal with nine filter taps?  This makes no sense to me.
 */
static void etnaviv_init_filter_kernel(void)
{
	unsigned row, idx, i;
	int16_t kernel_val[KERNEL_STATE_SZ * 2];
	float row_ofs = 0.5;
	float radius = 4.0;

	/* Compute lanczos filter kernel */
	for (row = i = 0; row < KERNEL_ROWS; row++) {
		float kernel[KERNEL_INDICES] = { 0.0 };
		float sum = 0.0;

		for (idx = 0; idx < KERNEL_INDICES; idx++) {
			float x = idx - 4.0 + row_ofs;

			if (fabs(x) <= radius)
				kernel[idx] = sinc(M_PI * x) *
				              sinc(M_PI * x / radius);

			sum += kernel[idx];
		}

		/* normalise the row */
		if (sum)
			for (idx = 0; idx < KERNEL_INDICES; idx++)
				kernel[idx] /= sum;

		/* convert to 1.14 format */
		for (idx = 0; idx < KERNEL_INDICES; idx++) {
			int val = kernel[idx] * (float)(1 << 14);

			if (val < -0x8000)
				val = -0x8000;
			else if (val > 0x7fff)
				val = 0x7fff;

			kernel_val[i++] = val;
		}

		row_ofs -= 1.0 / ((KERNEL_ROWS - 1) * 2);
	}

	kernel_val[KERNEL_SIZE] = 0;

	/* Now convert the kernel values into state values */
	for (i = 0; i < KERNEL_STATE_SZ * 2; i += 2)
		xv_filter_kernel[i / 2] =
		    VIVS_DE_FILTER_KERNEL_COEFFICIENT0(kernel_val[i]) |
		    VIVS_DE_FILTER_KERNEL_COEFFICIENT1(kernel_val[i + 1]);
}


#ifdef VIV2D_PUT_TEXTURE_IMAGE
// NOTE: filter blit VIVS_DE_VR_SOURCE_IMAGE* does not work, so we need to convert to an intermediate surface before doing a standard bitblt
// there is room for optimization, since in case of clipping we convert the full source for each clip
static Bool Viv2DPutTextureImage(PixmapPtr pSrcPix, BoxPtr pSrcBox,
                                 PixmapPtr pOsdPix, BoxPtr pOsdBox,
                                 PixmapPtr pDstPix, BoxPtr pDstBox,
                                 BoxPtr fullDstBox,
                                 unsigned int extraCount, PixmapPtr *extraPix, unsigned int format) {
	Viv2DRec *v2d = Viv2DPrivFromPixmap(pDstPix);
	Viv2DPixmapPrivPtr src = Viv2DPixmapPrivFromPixmap(pSrcPix);
	Viv2DPixmapPrivPtr dst = Viv2DPixmapPrivFromPixmap(pDstPix);
	uint32_t v_scale, h_scale;
	Viv2DPixmapPrivPtr tmp, tmp_dest;
	int reserve;
	int s_w, s_h, d_w, d_h;

	tmp_dest = NULL;

	if (!src->bo || !dst->bo)
		return FALSE;

	s_w = pSrcPix->drawable.width;
	s_h = pSrcPix->drawable.height;
	d_w = fullDstBox->x2 - fullDstBox->x1;
	d_h = fullDstBox->y2 - fullDstBox->y1;

	tmp = _Viv2DOpCreateTmpPix(v2d, d_w, s_h, 32);

	_Viv2DSetFormat(32, 32, &tmp->format); // A8R8G8B8

	_Viv2DSetFormat(pSrcPix->drawable.depth, pSrcPix->drawable.bitsPerPixel, &src->format);
	_Viv2DSetFormat(pDstPix->drawable.depth, pDstPix->drawable.bitsPerPixel, &dst->format);

	switch (format) {
	case fourcc_code('U', 'Y', 'V', 'Y'):
		src->format.fmt = DE_FORMAT_UYVY;
		break;
	case fourcc_code('Y', 'U', 'Y', '2'):
		src->format.fmt = DE_FORMAT_YUY2;
		break;
	case fourcc_code('Y', 'V', '1', '2'):
		src->format.fmt = DE_FORMAT_YV12;
		break;
	case fourcc_code('I', '4', '2', '0'):
		src->format.fmt = DE_FORMAT_YV12;
		break;
	}

	h_scale = ((s_w - 1) << 16) / (d_w - 1);
	v_scale = ((s_h - 1) << 16) / (d_h - 1);

	reserve = 8 + 14 + 2 + 4 + 6 + 10; // horizontal
	if (extraCount > 0) // planar
		reserve += 8;
	reserve += 8 + 14 + 2 + 4 + 6 + 10; // vertical
	reserve += KERNEL_STATE_SZ + 1; // filter kernel

	_Viv2DStreamReserve(v2d, reserve);

	// KERNEL_STATE_SZ + 1
	etna_set_state_multi(v2d->stream, VIVS_DE_FILTER_KERNEL(0), KERNEL_STATE_SZ,
	                     xv_filter_kernel);

	// 8
	etna_set_state_from_bo(v2d->stream, VIVS_DE_SRC_ADDRESS, src->bo, ETNA_RELOC_READ);
	etna_set_state(v2d->stream, VIVS_DE_SRC_STRIDE, src->pitch);
	etna_set_state(v2d->stream, VIVS_DE_SRC_ROTATION_CONFIG, 0);
	etna_set_state(v2d->stream, VIVS_DE_SRC_CONFIG, Viv2DSrcConfig(&src->format));

	if (extraCount > 0) {
		// 8
		Viv2DPixmapPrivPtr upix = Viv2DPixmapPrivFromPixmap(extraPix[0]);
		Viv2DPixmapPrivPtr vpix = Viv2DPixmapPrivFromPixmap(extraPix[1]);

		etna_set_state_from_bo(v2d->stream, VIVS_DE_UPLANE_ADDRESS, upix->bo, ETNA_RELOC_READ);
		etna_set_state(v2d->stream, VIVS_DE_UPLANE_STRIDE, upix->pitch);
		etna_set_state_from_bo(v2d->stream, VIVS_DE_VPLANE_ADDRESS, vpix->bo, ETNA_RELOC_READ);
		etna_set_state(v2d->stream, VIVS_DE_VPLANE_STRIDE, vpix->pitch);
	}

	// 14
	_Viv2DStreamDst(v2d, tmp, VIVS_DE_DEST_CONFIG_COMMAND_HOR_FILTER_BLT, ROP_SRC, NULL);

	// 2
	etna_set_state(v2d->stream, VIVS_DE_ALPHA_CONTROL,
	               VIVS_DE_ALPHA_CONTROL_ENABLE_OFF);
	// 4
	etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_LOW,
	               VIVS_DE_STRETCH_FACTOR_LOW_X(h_scale));
	etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_HIGH,
	               VIVS_DE_STRETCH_FACTOR_HIGH_Y(1 << 16));

	// 6
	// Apparently, this does not work
	etna_set_state(v2d->stream, VIVS_DE_VR_CONFIG_EX, 0);
	etna_set_state(v2d->stream, VIVS_DE_VR_SOURCE_IMAGE_LOW,
	               VIVS_DE_VR_SOURCE_IMAGE_LOW_LEFT(0) |
	               VIVS_DE_VR_SOURCE_IMAGE_LOW_TOP(0));
	etna_set_state(v2d->stream, VIVS_DE_VR_SOURCE_IMAGE_HIGH,
	               VIVS_DE_VR_SOURCE_IMAGE_HIGH_RIGHT(s_w) |
	               VIVS_DE_VR_SOURCE_IMAGE_HIGH_BOTTOM(s_h));

	// 10
	etna_set_state(v2d->stream, VIVS_DE_VR_SOURCE_ORIGIN_LOW, VIVS_DE_VR_SOURCE_ORIGIN_LOW_X(0));
	etna_set_state(v2d->stream, VIVS_DE_VR_SOURCE_ORIGIN_HIGH, VIVS_DE_VR_SOURCE_ORIGIN_HIGH_Y(0));

	etna_set_state(v2d->stream, VIVS_DE_VR_TARGET_WINDOW_LOW,
	               VIVS_DE_VR_TARGET_WINDOW_LOW_LEFT(0) |
	               VIVS_DE_VR_TARGET_WINDOW_LOW_TOP(0));
	etna_set_state(v2d->stream, VIVS_DE_VR_TARGET_WINDOW_HIGH,
	               VIVS_DE_VR_TARGET_WINDOW_HIGH_RIGHT(tmp->width) |
	               VIVS_DE_VR_TARGET_WINDOW_HIGH_BOTTOM(tmp->height));

	etna_set_state(v2d->stream, VIVS_DE_VR_CONFIG, VIVS_DE_VR_CONFIG_START_HORIZONTAL_BLIT);

	// 8
	etna_set_state_from_bo(v2d->stream, VIVS_DE_SRC_ADDRESS, tmp->bo, ETNA_RELOC_READ);
	etna_set_state(v2d->stream, VIVS_DE_SRC_STRIDE, tmp->pitch);
	etna_set_state(v2d->stream, VIVS_DE_SRC_ROTATION_CONFIG, 0);
	etna_set_state(v2d->stream, VIVS_DE_SRC_CONFIG, Viv2DSrcConfig(&tmp->format));

	// 14
	_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_VER_FILTER_BLT, ROP_SRC, NULL);

	// 2
	etna_set_state(v2d->stream, VIVS_DE_ALPHA_CONTROL,
	               VIVS_DE_ALPHA_CONTROL_ENABLE_OFF);
	// 4
	etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_LOW,
	               VIVS_DE_STRETCH_FACTOR_LOW_X(1 << 16));
	etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_HIGH,
	               VIVS_DE_STRETCH_FACTOR_HIGH_Y(v_scale));

	// 6
	etna_set_state(v2d->stream, VIVS_DE_VR_CONFIG_EX, 0);
	etna_set_state(v2d->stream, VIVS_DE_VR_SOURCE_IMAGE_LOW,
	               VIVS_DE_VR_SOURCE_IMAGE_LOW_LEFT(0) |
	               VIVS_DE_VR_SOURCE_IMAGE_LOW_TOP(0));
	etna_set_state(v2d->stream, VIVS_DE_VR_SOURCE_IMAGE_HIGH,
	               VIVS_DE_VR_SOURCE_IMAGE_HIGH_RIGHT(tmp->width) |
	               VIVS_DE_VR_SOURCE_IMAGE_HIGH_BOTTOM(tmp->height));

	// 10
	etna_set_state(v2d->stream, VIVS_DE_VR_SOURCE_ORIGIN_LOW, VIVS_DE_VR_SOURCE_ORIGIN_LOW_X(0));
	etna_set_state(v2d->stream, VIVS_DE_VR_SOURCE_ORIGIN_HIGH, VIVS_DE_VR_SOURCE_ORIGIN_HIGH_Y(0));

	etna_set_state(v2d->stream, VIVS_DE_VR_TARGET_WINDOW_LOW,
	               VIVS_DE_VR_TARGET_WINDOW_LOW_LEFT(pDstBox->x1) |
	               VIVS_DE_VR_TARGET_WINDOW_LOW_TOP(pDstBox->y1));
	etna_set_state(v2d->stream, VIVS_DE_VR_TARGET_WINDOW_HIGH,
	               VIVS_DE_VR_TARGET_WINDOW_HIGH_RIGHT(pDstBox->x2) |
	               VIVS_DE_VR_TARGET_WINDOW_HIGH_BOTTOM(pDstBox->y2));
	etna_set_state(v2d->stream, VIVS_DE_VR_CONFIG, VIVS_DE_VR_CONFIG_START_VERTICAL_BLIT);

	_Viv2DStreamCommit(v2d, TRUE);
//	etna_cmd_stream_finish(v2d->stream);
	VIV2D_DBG_MSG("Viv2DPutTextureImage %d src:%p/%p(%dx%d) %d %dx%d:%dx%d %s/%s dst:%p/%p(%dx%d) %d %dx%d:%dx%d %s/%s full:%dx%d:%dx%d tmp:%dx%d %d : %dx%d",
	              reserve + KERNEL_STATE_SZ + 1,
	              pSrcPix, src, src->width, src->height, src->pitch,
	              pSrcBox->x1, pSrcBox->y1, pSrcBox->x2, pSrcBox->y2,
	              Viv2DFormatColorStr(&src->format), Viv2DFormatSwizzleStr(&src->format),
	              pDstPix, dst, dst->width, dst->height, dst->pitch,
	              pDstBox->x1, pDstBox->y1, pDstBox->x2, pDstBox->y2,
	              Viv2DFormatColorStr(&dst->format), Viv2DFormatSwizzleStr(&dst->format),
	              fullDstBox->x1, fullDstBox->y1, fullDstBox->x2, fullDstBox->y2,
	              tmp->width, tmp->height, tmp->pitch,
	              v_scale, h_scale);

	_Viv2DOpDelTmpPix(v2d, tmp);

	return TRUE;
}
#endif

struct ARMSOCEXARec *
InitViv2DEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd)
{
	Viv2DEXAPtr v2d_exa = calloc(sizeof (*v2d_exa), 1);
	struct ARMSOCEXARec *armsoc_exa = (struct ARMSOCEXARec *)v2d_exa;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	ExaDriverPtr exa;
	Viv2DPtr v2d = calloc(sizeof (*v2d), 1);
	int etnavivFD, scanoutFD;
	uint64_t model, revision;

	etnavivFD = ARMSOCDetectDevice("etnaviv");

	if (etnavivFD) {
		INFO_MSG("Viv2DEXA: Etnaviv driver found");
	} else {
		goto fail;
	}

	v2d->fd = etnavivFD;

	v2d->dev = etna_device_new(etnavivFD);
	if (!v2d->dev) {
		ERROR_MSG("Viv2DEXA: Failed to load device");
		goto fail;
	}

	v2d->gpu = etna_gpu_new(v2d->dev, 0);
	if (!v2d->gpu) {
		ERROR_MSG("Viv2DEXA: Failed to create gpu");
		goto fail;
	}
	etna_gpu_get_param(v2d->gpu, ETNA_GPU_MODEL, &model);
	etna_gpu_get_param(v2d->gpu, ETNA_GPU_REVISION, &revision);
	INFO_MSG("Viv2DEXA: Vivante GC%x GPU revision %x found !", (uint32_t)model, (uint32_t)revision);

	v2d->pipe = etna_pipe_new(v2d->gpu, ETNA_PIPE_2D);
	if (!v2d->pipe) {
		ERROR_MSG("Viv2DEXA: Failed to create pipe");
		goto fail;
	}

	v2d->stream = etna_cmd_stream_new(v2d->pipe, VIV2D_STREAM_SIZE, NULL, NULL);
	if (!v2d->stream) {
		ERROR_MSG("Viv2DEXA: Failed to create stream");
		goto fail;
	}

	scanoutFD = armsoc_bo_get_dmabuf(pARMSOC->scanout);
	v2d->bo = etna_bo_from_dmabuf(v2d->dev, scanoutFD);
	close(scanoutFD);

	v2d_exa->v2d = v2d;

	exa = exaDriverAlloc();
	if (!exa) {
		VIV2D_ERR_MSG("cannot alloc EXA driver");
		goto fail;
	}

	v2d_exa->exa = exa;

#ifdef VIV2D_FLUSH_CALLBACK
	if (!AddCallback(&FlushCallback, Viv2DFlushCallback, pScrn)) {
		VIV2D_ERR_MSG("cannot add flush callback");
		goto fail;
	}
#endif

	exa->exa_major = EXA_VERSION_MAJOR;
	exa->exa_minor = EXA_VERSION_MINOR;

	exa->pixmapOffsetAlign = 4096;
	exa->pixmapPitchAlign = 32;

	exa->flags = EXA_OFFSCREEN_PIXMAPS |
	             EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX;

	exa->maxX = 2048;
	exa->maxY = 2048;

	/* Required EXA functions: */
#ifdef VIV2D_MARKER
	exa->MarkSync = Viv2DMarkSync;
	exa->WaitMarker = Viv2DWaitMarker;
#else
	exa->WaitMarker = ARMSOCWaitMarker;
#endif

#ifdef VIV2D_PIXMAP
	exa->CreatePixmap2 = Viv2DCreatePixmap2;
	exa->DestroyPixmap = Viv2DDestroyPixmap;
	exa->ModifyPixmapHeader = Viv2DModifyPixmapHeader;
#else
	exa->CreatePixmap2 = ARMSOCCreatePixmap2;
	exa->DestroyPixmap = ARMSOCDestroyPixmap;
	exa->ModifyPixmapHeader = ARMSOCModifyPixmapHeader;
#endif

#ifdef VIV2D_ACCESS
	exa->PrepareAccess = Viv2DPrepareAccess;
	exa->FinishAccess = Viv2DFinishAccess;
	exa->PixmapIsOffscreen = Viv2DPixmapIsOffscreen;
#else
	exa->PrepareAccess = ARMSOCPrepareAccess;
	exa->FinishAccess = ARMSOCFinishAccess;
	exa->PixmapIsOffscreen = ARMSOCPixmapIsOffscreen;
#endif

#ifdef VIV2D_COPY
	exa->PrepareCopy = Viv2DPrepareCopy;
	exa->Copy = Viv2DCopy;
	exa->DoneCopy = Viv2DDoneCopy;
#else
	exa->PrepareCopy = PrepareCopyFail;
#endif

#ifdef VIV2D_SOLID
	exa->PrepareSolid = Viv2DPrepareSolid;
	exa->Solid = Viv2DSolid;
	exa->DoneSolid = Viv2DDoneSolid;
#else
	exa->PrepareSolid = PrepareSolidFail;
#endif

#ifdef VIV2D_COMPOSITE
	exa->CheckComposite = Viv2DCheckComposite;
	exa->PrepareComposite = Viv2DPrepareComposite;
	exa->Composite = Viv2DComposite;
	exa->DoneComposite = Viv2DDoneComposite;
#else
	exa->CheckComposite = CheckCompositeFail;
	exa->PrepareComposite = PrepareCompositeFail;
#endif

#ifdef VIV2D_UPLOAD_TO_SCREEN
	exa->UploadToScreen = Viv2DUploadToScreen;
#endif
#ifdef VIV2D_DOWNLOAD_FROM_SCREEN
	exa->DownloadFromScreen = Viv2DDownloadFromScreen;
#endif

	if (! exaDriverInit(pScreen, exa)) {
		ERROR_MSG("Viv2DEXA: exaDriverInit failed");
		goto fail;
	}

	armsoc_exa->CloseScreen = CloseScreen;
	armsoc_exa->FreeScreen = FreeScreen;

	etnaviv_init_filter_kernel();

	armsoc_exa->Flush = Viv2DFlush;
	armsoc_exa->AllocBuf = Viv2DAllocBuf;
	armsoc_exa->FreeBuf = Viv2DFreeBuf;
	armsoc_exa->MapUsermemBuf = Viv2DMapUsermemBuf;
	armsoc_exa->UnmapUsermemBuf = Viv2DUnmapUsermemBuf;
	armsoc_exa->Reattach = Viv2DReattach;
	armsoc_exa->GetFormats = Viv2DGetFormats;
#ifdef VIV2D_PUT_TEXTURE_IMAGE
	armsoc_exa->PutTextureImage = Viv2DPutTextureImage;
#endif
	INFO_MSG("Viv2DEXA: initialized.");

	return armsoc_exa;

fail:
	if (v2d_exa) {
		free(v2d_exa);
	}
	return NULL;
}


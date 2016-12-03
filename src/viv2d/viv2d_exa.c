
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

#include "armsoc_driver.h"
#include "armsoc_exa.h"

#include "etnaviv_drmif.h"
#include "etnaviv_drm.h"

#include "exa.h"

#include "viv2d.h"
#include "viv2d_exa.h"
#include "viv2d_op.h"


#define VIV2D_STREAM_SIZE 768

#define VIV2D_SOLID 1
#define VIV2D_COPY 1
#define VIV2D_COMPOSITE 1

#define VIV2D_MASK_SUPPORT 1
#define VIV2D_SOLID_PICTURE 1
#define VIV2D_REPEAT 1
#define VIV2D_UPLOAD_TO_SCREEN 1
#define VIV2D_REPEAT_WITH_MASK 1

#define VIV2D_MIN_HW_HEIGHT 64
#define VIV2D_MIN_HW_SIZE_24BIT (256 * 256)

#define ALIGN(val, align)	(((val) + (align) - 1) & ~((align) - 1))

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
	{PictOpSaturate,		DE_BLENDMODE_SATURATED_ALPHA,	DE_BLENDMODE_ONE} // ?
};

#define NO_PICT_FORMAT -1
/**
 * Picture Formats and their counter parts
 */
#define VIV2D_PICT_FORMAT_COUNT 19
static const Viv2DFormat
viv2d_pict_format[] = {
	{PICT_a8r8g8b8, 32, DE_FORMAT_A8R8G8B8, DE_SWIZZLE_ARGB, 8},
	{PICT_x8r8g8b8, 32, DE_FORMAT_X8R8G8B8, DE_SWIZZLE_ARGB, 0},
	{PICT_a8b8g8r8, 32, DE_FORMAT_A8R8G8B8, DE_SWIZZLE_ABGR, 8},
	{PICT_x8b8g8r8, 32, DE_FORMAT_X8R8G8B8,	DE_SWIZZLE_ABGR, 0},
	{PICT_b8g8r8a8, 32, DE_FORMAT_A8R8G8B8,	DE_SWIZZLE_BGRA, 8},
	{PICT_b8g8r8x8, 32, DE_FORMAT_X8R8G8B8,	DE_SWIZZLE_BGRA, 8},
	{PICT_r5g6b5, 16, DE_FORMAT_R5G6B5, DE_SWIZZLE_ARGB, 0},
	{PICT_b5g6r5, 16, DE_FORMAT_R5G6B5,	DE_SWIZZLE_ABGR, 0},
	{PICT_a1r5g5b5, 16, DE_FORMAT_A1R5G5B5, DE_SWIZZLE_ARGB, 1},
	{PICT_x1r5g5b5, 16, DE_FORMAT_X1R5G5B5, DE_SWIZZLE_ARGB, 0},
	{PICT_a1b5g5r5, 16, DE_FORMAT_A1R5G5B5,	DE_SWIZZLE_ABGR, 1},
	{PICT_x1b5g5r5, 16,	DE_FORMAT_X1R5G5B5, DE_SWIZZLE_ABGR, 0},
	{PICT_a4r4g4b4, 16, DE_FORMAT_A4R4G4B4, DE_SWIZZLE_ARGB, 4},
	{PICT_x4r4g4b4, 16, DE_FORMAT_X4R4G4B4, DE_SWIZZLE_ARGB, 0},
	{PICT_a4b4g4r4, 16, DE_FORMAT_A4R4G4B4, DE_SWIZZLE_ABGR, 4},
	{PICT_x4b4g4r4, 16, DE_FORMAT_X4R4G4B4, DE_SWIZZLE_ABGR, 0},
	{PICT_a8, 8, DE_FORMAT_A8, 8},
	{PICT_c8, 8, DE_FORMAT_INDEX8, 8},
	{NO_PICT_FORMAT, 0, 0, 0}
	/*END*/
};

// for debug
static const char *pix_op_name(int op) {
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
	}
}

static const char *pix_format_name(pixman_format_code_t format)
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

// others utils

static int VIV2DDetectDevice(const char *name)
{
	drmVersionPtr version;
	char buf[64];
	int minor, fd, rc;

	for (minor = 0; minor < 64; minor++) {
		snprintf(buf, sizeof(buf), "%s/card%d", DRM_DIR_NAME,
		         minor);

		fd = open(buf, O_RDWR);
		if (fd == -1)
			continue;

		version = drmGetVersion(fd);
		if (version) {
			rc = strcmp(version->name, name);
			drmFreeVersion(version);

			if (rc == 0) {
				VIV2D_INFO_MSG("VIV2DDetectDevice %s found at %s", name, buf);
				return fd;
			}
		}

		close(fd);
	}

	return -1;
}

static inline void Viv2DDetachBo(struct ARMSOCRec *pARMSOC, struct ARMSOCPixmapPrivRec *privPix) {
	if (privPix) {
		Viv2DPixmapPrivPtr pix = privPix->priv;

		if (privPix->bo == pARMSOC->scanout) {
		} else {
			if (pix->bo) {
				etna_bo_cpu_prep(pix->bo, DRM_ETNA_PREP_READ | DRM_ETNA_PREP_WRITE);
				VIV2D_DBG_MSG("Viv2DDetachBo del bo %p", pix->bo);
				etna_bo_del(pix->bo);
				pix->bo = NULL;
			}
		}
	}
}

static inline void Viv2DAttachBo(struct ARMSOCRec *pARMSOC, struct ARMSOCPixmapPrivRec *privPix) {
	Viv2DDetachBo(pARMSOC, privPix);
	if (privPix) {
		Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
		Viv2DPixmapPrivPtr pix = privPix->priv;

		if (privPix->bo == pARMSOC->scanout) {
			pix->bo = v2d->bo;
		} else {
			int fd = armsoc_bo_get_dmabuf(privPix->bo);
			if (fd) {
				pix->bo = etna_bo_from_dmabuf(v2d->dev, fd);
				close(fd);
				VIV2D_DBG_MSG("Viv2DAttachBo new bo %p", pix->bo);

			} else {
				VIV2D_ERR_MSG("Viv2D error: cannot attach bo : %d", fd);
			}
		}
	}
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
	default:
		colour = pixel;
		break;
	}
	return colour;
}

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

// EXA functions

static void *
Viv2DCreatePixmap (ScreenPtr pScreen, int width, int height,
                   int depth, int usage_hint, int bitsPerPixel,
                   int *new_fb_pitch)
{
	struct ARMSOCPixmapPrivRec *privPix = ARMSOCCreatePixmap2(pScreen, width, height, depth,
	                                      usage_hint, bitsPerPixel, new_fb_pitch);
	Viv2DPixmapPrivPtr pix = calloc(sizeof(Viv2DPixmapPrivRec), 1);
	privPix->priv = pix;
	pix->armsocPix = privPix;
	return privPix;
}

static void
Viv2DDestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCPixmapPrivRec *privPix = driverPriv;

	Viv2DPixmapPrivPtr pix = privPix->priv;

	Viv2DDetachBo(pARMSOC, privPix);

	free(pix);
	privPix->priv = NULL;
	ARMSOCDestroyPixmap(pScreen, privPix);
}

static Bool
Viv2DModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
                        int depth, int bitsPerPixel, int devKind,
                        pointer pPixData)
{
	Viv2DRec *v2d = Viv2DPrivFromPixmap(pPixmap);
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCPixmapPrivRec *privPix = exaGetPixmapDriverPrivate(pPixmap);

	if (ARMSOCModifyPixmapHeader(pPixmap, width, height, depth, bitsPerPixel, devKind, pPixData)) {
		Viv2DPixmapPrivPtr pix = privPix->priv;
		if (pPixData == armsoc_bo_map(pARMSOC->scanout) && pix->bo != v2d->bo) {
			pix->width = armsoc_bo_width(pARMSOC->scanout);
			pix->height = armsoc_bo_height(pARMSOC->scanout);
			pix->pitch = armsoc_bo_pitch(pARMSOC->scanout);
			pix->bo = v2d->bo;
			VIV2D_DBG_MSG("Viv2DModifyPixmapHeader pix bo scanout");
		} else {
			Viv2DDetachBo(pARMSOC, privPix);

			if (privPix->bo) {
				pix->width = armsoc_bo_width(privPix->bo);
				pix->height = armsoc_bo_height(privPix->bo);
				pix->pitch = armsoc_bo_pitch(privPix->bo);
			}
		}
	} else {
		if (privPix->bo) {
			Viv2DDetachBo(pARMSOC, privPix);

		}
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
static Bool Viv2DUploadToScreen(PixmapPtr pDst,
                         int x,
                         int y, int w, int h, char *src, int src_pitch) {
	ScrnInfoPtr pScrn = pix2scrn(pDst);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct ARMSOCPixmapPrivRec *armsocPix = exaGetPixmapDriverPrivate(pDst);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DPixmapPrivPtr dst = Viv2DPixmapPrivFromPixmap(pDst);
	Viv2DRect rects[1];
	int height = h;
	Viv2DPixmapPrivRec pix;
	int pitch, size;
	char *src_buf,*buf;

	if (pDst->drawable.width * pDst->drawable.height < VIV2D_MIN_HW_SIZE_24BIT) {
		VIV2D_UNSUPPORTED_MSG("Viv2DUploadToScreen dest drawable is too small %dx%d", pDst->drawable.width, pDst->drawable.height);
		return FALSE;
	}

	if (!Viv2DSetFormat(pDst->drawable.depth, pDst->drawable.bitsPerPixel, &dst->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DUploadToScreen unsupported dst format %d/%d %p", pDst->drawable.depth, pDst->drawable.bitsPerPixel, src);
		return FALSE;
	}

	if (!Viv2DSetFormat(pDst->drawable.depth, pDst->drawable.bitsPerPixel, &pix.format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DUploadToScreen unsupported format %d/%d %p", pDst->drawable.depth, pDst->drawable.bitsPerPixel, src);
		return FALSE;
	}

	if (dst->format.fmt == DE_FORMAT_A8)
	{
		VIV2D_UNSUPPORTED_MSG("Viv2DUploadToScreen unsupported dst A8");
		return FALSE;
	}

	pix.width = w;
	pix.height = h;
	pix.pitch = ALIGN(w * ((pDst->drawable.bitsPerPixel + 7) / 8), 32);
	pix.bo = NULL;

	pitch = pix.pitch;
	size = pitch * pix.height;
	pix.bo = etna_bo_new(v2d->dev, size, ETNA_BO_UNCACHED);

	src_buf = src ;
	buf = (char *) etna_bo_map(pix.bo);

	while (height--) {
		memcpy(buf, src_buf, pitch);
		src_buf += src_pitch;
		buf += pitch;
	}

	rects[0].x1 = x;
	rects[0].y1 = y;
	rects[0].x2 = x + w;
	rects[0].y2 = y + h;

	Viv2DAttachBo(pARMSOC, armsocPix);

	_Viv2DStreamSrc(v2d, &pix, 0, 0, pix.width, pix.height); // tmp source
	_Viv2DStreamDst(v2d, dst, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, NULL);
	_Viv2DStreamBlendOp(v2d, NULL, 0, 0, FALSE, FALSE);
	_Viv2DStreamRects(v2d, rects, 1);
	_Viv2DStreamCommit(v2d);

	Viv2DDetachBo(pARMSOC, armsocPix);

	VIV2D_DBG_MSG("Viv2DUploadToScreen blit done %p(%d) %dx%d(%dx%d) %dx%d %d/%d", src, src_pitch, x, y, w, h,
	              pDst->drawable.width, pDst->drawable.height,
	              pDst->drawable.depth, pDst->drawable.bitsPerPixel);

	etna_bo_cpu_prep(pix.bo, DRM_ETNA_PREP_READ | DRM_ETNA_PREP_WRITE);
	VIV2D_DBG_MSG("Viv2DUploadToScreen del bo %p", pix.bo);
	etna_bo_del(pix.bo);
	return TRUE;
}

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
#if 0
static Bool Viv2DDownloadFromScreen(PixmapPtr pSrc,
                             int x, int y,
                             int w, int h, char *dst, int dst_pitch) {
	return FALSE;
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
	struct ARMSOCPixmapPrivRec *privPix = exaGetPixmapDriverPrivate(pPixmap);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DPixmapPrivPtr dst = Viv2DPixmapPrivFromPixmap(pPixmap);

	Viv2DOp *op;

	if (alu != GXcopy) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareSolid unsupported alu %d", alu);
		return FALSE;
	}

	if (!EXA_PM_IS_SOLID(&pPixmap->drawable, planemask)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareSolid unsupported planemask %x", (uint32_t)planemask);
		return FALSE;
	}

	if (pPixmap->drawable.height < VIV2D_MIN_HW_HEIGHT || pPixmap->drawable.width * pPixmap->drawable.height < VIV2D_MIN_HW_SIZE_24BIT) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareSolid dest drawable is too small %dx%d", pPixmap->drawable.width, pPixmap->drawable.height);
		return FALSE;
	}

	if (!Viv2DSetFormat(pPixmap->drawable.depth, pPixmap->drawable.bitsPerPixel, &dst->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareSolid unsupported format for depth:%d bpp:%d", pPixmap->drawable.depth, pPixmap->drawable.bitsPerPixel);
		return FALSE;
	}

	op = _Viv2DOpCreate();
	op->fg = Viv2DColour(fg, pPixmap->drawable.depth);
	op->mask = (uint32_t)planemask;
	op->dst = dst;
	v2d->op = op;

	Viv2DAttachBo(pARMSOC, privPix);

	VIV2D_DBG_MSG("Viv2DPrepareSolid %p %dx%d, %x %x %d, %d %p", dst,
	              pPixmap->drawable.width, pPixmap->drawable.height, op->fg ,
	              op->mask, pPixmap->drawable.depth, alu, dst);

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
	Viv2DOp *op = v2d->op;
	if (op->cur_rect < VIV2D_MAX_RECTS)
	{
//	VIV2D_DBG_MSG("Viv2DSolid %dx%d:%dx%d %d", x1, y1, x2, y2, solidOp->cur_rect);
		_Viv2DOpAddRect(op, x1, y1, x2 - x1, y2 - y1);
	} else {
		_Viv2DStreamDst(v2d, op->dst, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, NULL);
		_Viv2DStreamColor(v2d, op->fg);
		_Viv2DStreamRects(v2d, op->rects, op->cur_rect);
		op->cur_rect = 0;
		_Viv2DOpAddRect(op, x1, y1, x2 - x1, y2 - y1);
	}
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
	struct ARMSOCPixmapPrivRec *privPix = exaGetPixmapDriverPrivate(pPixmap);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DOp *op = v2d->op;

	_Viv2DStreamDst(v2d, op->dst, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, NULL);
	_Viv2DStreamColor(v2d, op->fg);
	_Viv2DStreamRects(v2d, op->rects, op->cur_rect);

	VIV2D_DBG_MSG("Viv2DDoneSolid %d", v2d->stream->offset);

	_Viv2DStreamCommit(v2d);

	_Viv2DOpDestroy(op);
	v2d->op = NULL;

	Viv2DDetachBo(pARMSOC, privPix);

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
	struct ARMSOCPixmapPrivRec *armsocSrcPix = exaGetPixmapDriverPrivate(pSrcPixmap);
	struct ARMSOCPixmapPrivRec *armsocDstPix = exaGetPixmapDriverPrivate(pDstPixmap);
	ScrnInfoPtr pScrn = pix2scrn(pDstPixmap);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DPixmapPrivPtr src = Viv2DPixmapPrivFromPixmap(pSrcPixmap);
	Viv2DPixmapPrivPtr dst = Viv2DPixmapPrivFromPixmap(pDstPixmap);

	Viv2DOp *op;

	if (alu != GXcopy) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareCopy unsupported alu %d", alu);
		return FALSE;
	}

	if (pDstPixmap->drawable.height < VIV2D_MIN_HW_HEIGHT || pDstPixmap->drawable.width * pDstPixmap->drawable.height < VIV2D_MIN_HW_SIZE_24BIT) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareCopy dest drawable is too small %dx%d", pDstPixmap->drawable.width, pDstPixmap->drawable.height);
		return FALSE;
	}

	if (!Viv2DSetFormat(pSrcPixmap->drawable.depth, pSrcPixmap->drawable.bitsPerPixel, &src->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareCopy unsupported format for depth:%d bpp:%d", pSrcPixmap->drawable.depth, pSrcPixmap->drawable.bitsPerPixel);
		return FALSE;
	}
	if (!Viv2DSetFormat(pDstPixmap->drawable.depth, pDstPixmap->drawable.bitsPerPixel, &dst->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareCopy unsupported format for depth:%d bpp:%d", pDstPixmap->drawable.depth, pDstPixmap->drawable.bitsPerPixel);
		return FALSE;
	}

	if (dst->format.fmt == DE_FORMAT_A8)
	{
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareCopy unsupported dst A8");
		return FALSE;
	}

	op = _Viv2DOpCreate();
	op->mask = (uint32_t)planemask;
	op->src = src;
	op->dst = dst;
	v2d->op = op;

	Viv2DAttachBo(pARMSOC, armsocSrcPix);
	Viv2DAttachBo(pARMSOC, armsocDstPix);

	VIV2D_DBG_MSG("Viv2DPrepareCopy src:%p(%dx%d)[%s/%s] dst:%p(%dx%d)[%s/%s] dir:%dx%d alu:%d planemask:%x",
	              src, src->width, src->height, Viv2DFormatColorStr(&src->format), Viv2DFormatSwizzleStr(&src->format),
	              dst, dst->width, dst->height, Viv2DFormatColorStr(&dst->format), Viv2DFormatSwizzleStr(&dst->format),
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
	Viv2DOp *op = v2d->op;

	VIV2D_DBG_MSG("Viv2DCopy %p(%dx%d) -> %p(%dx%d) : %dx%d", op->src, srcX, srcY, op->dst, dstX, dstY, width, height);

// new srcX,srcY group
	if (op->prev_src_x != srcX || op->prev_src_y != srcY || op->cur_rect >= VIV2D_MAX_RECTS) {
		// stream previous rects
		if (op->prev_src_x > -1) {
			_Viv2DStreamRects(v2d, op->rects, op->cur_rect);
			op->cur_rect = 0;
		}

		// create states for srcX,srcY group
		_Viv2DStreamSrc(v2d, op->src, srcX, srcY, width, height);
		_Viv2DStreamDst(v2d, op->dst, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, NULL);
		_Viv2DStreamBlendOp(v2d, op->blend_op, 0, 0, FALSE, FALSE);
	}

	_Viv2DOpAddRect(op, dstX, dstY, width, height);

	op->prev_src_x = srcX;
	op->prev_src_y = srcY;
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
	Viv2DOp *op = v2d->op;

	_Viv2DStreamRects(v2d, op->rects, op->cur_rect);

	VIV2D_DBG_MSG("Viv2DDoneCopy %d", v2d->stream->offset);

	_Viv2DStreamCommit(v2d);

	Viv2DDetachBo(pARMSOC, op->src->armsocPix);
	Viv2DDetachBo(pARMSOC, op->dst->armsocPix);

	_Viv2DOpDestroy(op);
	v2d->op = NULL;
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

static Bool Viv2DGetPictureFormat(int exa_fmt, Viv2DFormat *fmt) {
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
	PixmapPtr pDst = GetDrawablePixmap(pDstPicture->pDrawable);

	Viv2DFormat src_fmt;
//	Viv2DFormat msk_fmt;
	Viv2DFormat dst_fmt;

	if (pSrc == NULL) {
#ifdef VIV2D_SOLID_PICTURE
		SourcePict *sp = pSrcPicture->pSourcePict;

		if (sp->type == SourcePictTypeSolidFill) {
		} else {
			VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported src is not a drawable : %d", sp->type);
			return FALSE;
		}
#else
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported src is not a drawable");
		return FALSE;
#endif
	}

	if (pDst == NULL) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported dest is not a drawable");
		return FALSE;
	}

	if (pDst->drawable.height < VIV2D_MIN_HW_HEIGHT || pDst->drawable.width * pDst->drawable.height < VIV2D_MIN_HW_SIZE_24BIT) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite dest drawable is too small %dx%d", pDst->drawable.width, pDst->drawable.height);
		return FALSE;
	}

	/*For forward compatibility*/
	if (op > PictOpSaturate) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite op unsupported : %d", op);
		return FALSE;
	}

	/*Format Checks*/
	if (!Viv2DGetPictureFormat(pSrcPicture->format, &src_fmt)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported src format %s", pix_format_name(pSrcPicture->format));
		return FALSE;
	}

	if (!Viv2DGetPictureFormat(pDstPicture->format, &dst_fmt)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported dst format %s", pix_format_name(pDstPicture->format));
		return FALSE;
	}

	if (dst_fmt.fmt == DE_FORMAT_A8)
	{
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite unsupported dst A8");
		return FALSE;
	}

	if (pMaskPicture && pMaskPicture->transform) {
		return FALSE;
	}

	if (pMaskPicture && pMaskPicture->componentAlpha) {
		return FALSE;
	}

	if (pMaskPicture && pMaskPicture->repeat) {
		return FALSE;
	}

	if ( pSrcPicture->transform ) {
		VIV2D_UNSUPPORTED_MSG("Viv2DCheckComposite transform unsupported");
		return FALSE;
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
	struct ARMSOCPixmapPrivRec *armsocSrcPix = NULL;
	struct ARMSOCPixmapPrivRec *armsocMaskPix = NULL;
	struct ARMSOCPixmapPrivRec *armsocDstPix = exaGetPixmapDriverPrivate(pDst);
	ScrnInfoPtr pScrn = pix2scrn(pDst);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

	Viv2DRec *v2d = Viv2DPrivFromARMSOC(pARMSOC);
	Viv2DPixmapPrivPtr src = NULL;
	Viv2DPixmapPrivPtr msk = NULL;
	Viv2DPixmapPrivPtr dst = Viv2DPixmapPrivFromPixmap(pDst);
	Viv2DOp *op;

	if (pSrc != NULL) {
		armsocSrcPix = exaGetPixmapDriverPrivate(pSrc);
		src = Viv2DPixmapPrivFromPixmap(pSrc);

		if (!Viv2DGetPictureFormat(pSrcPicture->format, &src->format)) {
			VIV2D_UNSUPPORTED_MSG("Viv2DPrepareComposite unsupported src format %s", pix_format_name(pSrcPicture->format));
			return FALSE;
		}

	}

	if (!Viv2DGetPictureFormat(pDstPicture->format, &dst->format)) {
		VIV2D_UNSUPPORTED_MSG("Viv2DPrepareComposite unsupported dst format %s", pix_format_name(pDstPicture->format));
		return FALSE;
	}

	op = _Viv2DOpCreate();

	if (pMask != NULL) {
		armsocMaskPix = exaGetPixmapDriverPrivate(pMask);
		msk = Viv2DPixmapPrivFromPixmap(pMask);

//		Viv2DGetPictureFormat(pMaskPicture->format, &op->msk_fmt);

		if (!Viv2DGetPictureFormat(pMaskPicture->format, &op->msk_fmt)) {
			VIV2D_UNSUPPORTED_MSG("Viv2DPrepareComposite unsupported msk format %s", pix_format_name(pMaskPicture->format));
			return FALSE;
		}

	}

	if (src != NULL) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite src:%p-%p(%dx%d)[%s/%s]",
		              pSrcPicture, src, src->width, src->height, Viv2DFormatColorStr(&src->format), Viv2DFormatSwizzleStr(&src->format));
	}
	if (msk != NULL) {
		VIV2D_DBG_MSG("Viv2DPrepareComposite msk:%p-%p(%dx%d)[%s/%s]",
		              pMaskPicture, msk, msk->width, msk->height, Viv2DFormatColorStr(&op->msk_fmt), Viv2DFormatSwizzleStr(&op->msk_fmt));
	}

	VIV2D_DBG_MSG("Viv2DPrepareComposite dst:%p-%p(%dx%d)[%s/%s] op:%d(%s)",
	              pDstPicture, dst, dst->width, dst->height, Viv2DFormatColorStr(&dst->format), Viv2DFormatSwizzleStr(&dst->format),
	              rop, pix_op_name(rop));

	op->blend_op = &viv2d_blend_op[rop];

	if (pMaskPicture != NULL) {
		op->has_mask = TRUE;
	}

	op->src_alpha_mode_global = FALSE;
	op->dst_alpha_mode_global = FALSE;
	op->src_alpha = 0x00;
	op->dst_alpha = 0x00;

	if (src) {
		if (Viv2DFixNonAlpha(&src->format)) {
			op->src_alpha_mode_global = TRUE;
			op->src_alpha = 0xff;
		}
	}

	if (Viv2DFixNonAlpha(&dst->format)) {
		op->dst_alpha_mode_global = TRUE;
		op->dst_alpha = 0xff;
	}

	// FIXME for an unknown reason, alpha src to fixed dest does not work propertly with 24bits depth
	/*	if (op->dst_alpha_mode_global && !op->src_alpha_mode_global) {
			return FALSE;
		}
	*/
	op->src = src;
	op->dst = dst;
	op->msk = msk;

	// src type
	op->src_type = viv2d_src_pix;

	if (pSrc != NULL && pSrcPicture->repeat && pSrc->drawable.width == 1 && pSrc->drawable.height == 1) {
		op->src_type = viv2d_src_1x1_repeat;
	}

	if (pSrc == NULL && pSrcPicture->pSourcePict->type == SourcePictTypeSolidFill) {
		op->src_type = viv2d_src_solid;
		op->fg = pSrcPicture->pSourcePict->solidFill.color;
	}

	if (pMask || pMaskPicture) {
		// msk type
		op->msk_type = viv2d_src_pix;

		if (pMask != NULL && pMaskPicture->repeat && pMask->drawable.width == 1 && pMask->drawable.height == 1) {
			op->msk_type = viv2d_src_1x1_repeat;
		}

		if (pMask == NULL && pMaskPicture->pSourcePict->type == SourcePictTypeSolidFill) {
			op->msk_type = viv2d_src_solid;
			op->fg = pMaskPicture->pSourcePict->solidFill.color;
		}

	}
	v2d->op = op;

	Viv2DAttachBo(pARMSOC, armsocSrcPix);
	Viv2DAttachBo(pARMSOC, armsocMaskPix);
	Viv2DAttachBo(pARMSOC, armsocDstPix);

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
	Viv2DOp *op = v2d->op;

	/*
		VIV2D_DBG_MSG("Viv2DComposite src:%dx%d(%dx%d) -> dst:%dx%d(%dx%d) : %dx%d %d",
		              srcX, srcY, op->src->width, op->src->height,
		              dstX, dstY, op->dst->width, op->dst->height,
		              width, height, op->src_type);
	*/
	if (op->msk) {
		// tmp 32bits argb pix
		Viv2DPixmapPrivRec tmp;
		Viv2DBlendOp *cpy_op = &viv2d_blend_op[PictOpSrc];
		Viv2DBlendOp *msk_op = &viv2d_blend_op[PictOpInReverse];
		Viv2DRect mrect, drect;

		tmp.width = width;
		tmp.height = height;
		tmp.pitch = ALIGN(width * ((32 + 7) / 8), 32);
		Viv2DSetFormat(32, 32, &tmp.format);
		tmp.bo = etna_bo_new(v2d->dev, tmp.pitch * tmp.height, ETNA_BO_UNCACHED);

		mrect.x1 = 0;
		mrect.y1 = 0;
		mrect.x2 = width;
		mrect.y2 = height;

		drect.x1 = dstX;
		drect.y1 = dstY;
		drect.x2 = dstX + width;
		drect.y2 = dstY + height;

		switch (op->src_type) {
		case viv2d_src_1x1_repeat:
			_Viv2DStreamSrc(v2d, op->src, 0, 0, 1, 1);
			etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_LOW,
			               VIVS_DE_STRETCH_FACTOR_LOW_X(((op->src->width - 1) << 16) / (op->dst->width - 1)));
			etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_HIGH,
			               VIVS_DE_STRETCH_FACTOR_HIGH_Y(((op->src->height - 1) << 16) / (op->dst->height - 1)));
			_Viv2DStreamDst(v2d, &tmp, VIVS_DE_DEST_CONFIG_COMMAND_STRETCH_BLT, NULL);
			break;
		case viv2d_src_solid:
			_Viv2DStreamDst(v2d, &tmp, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, NULL);
			_Viv2DStreamColor(v2d, op->fg);
			break;
		default:
			_Viv2DStreamSrc(v2d, op->src, srcX, srcY, width, height);
			_Viv2DStreamDst(v2d, &tmp, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, NULL);
			break;
		}
		_Viv2DStreamBlendOp(v2d, cpy_op, op->src_alpha, 0, op->src_alpha_mode_global, FALSE);
		_Viv2DStreamRects(v2d, &mrect, 1);

		// apply msk with mask op to tmp
		switch (op->msk_type) {
		case viv2d_src_1x1_repeat:
			_Viv2DStreamSrcWithFormat(v2d, op->msk, 0, 0, 1, 1, &op->msk_fmt);
			etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_LOW,
			               VIVS_DE_STRETCH_FACTOR_LOW_X(((op->msk->width - 1) << 16) / (op->dst->width - 1)));
			etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_HIGH,
			               VIVS_DE_STRETCH_FACTOR_HIGH_Y(((op->msk->height - 1) << 16) / (op->dst->height - 1)));
			_Viv2DStreamDst(v2d, &tmp, VIVS_DE_DEST_CONFIG_COMMAND_STRETCH_BLT, NULL);
			break;
		case viv2d_src_solid:
			_Viv2DStreamDst(v2d, &tmp, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, NULL);
			_Viv2DStreamColor(v2d, op->fg);
			break;
		default:
			_Viv2DStreamSrcWithFormat(v2d, op->msk, maskX, maskY, width, height, &op->msk_fmt);
			_Viv2DStreamDst(v2d, &tmp, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, NULL);
			break;
		}
		_Viv2DStreamBlendOp(v2d, msk_op, 0, 0, FALSE, FALSE);
		_Viv2DStreamRects(v2d, &mrect, 1);

		// finally apply to dest
		_Viv2DStreamSrc(v2d, &tmp, 0, 0, width, height);
		_Viv2DStreamDst(v2d, op->dst, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, NULL);
		_Viv2DStreamBlendOp(v2d, op->blend_op, 0, op->dst_alpha, FALSE, op->dst_alpha_mode_global);
		VIV2D_DBG_MSG("Viv2DComposite mask %x %d %d -> %d", op->fg, op->blend_op->op, op->blend_op->srcBlendMode, op->blend_op->dstBlendMode);
		_Viv2DStreamRects(v2d, &drect, 1);

		_Viv2DStreamCommit(v2d); // commit now because of tmp bo
		etna_bo_cpu_prep(tmp.bo, DRM_ETNA_PREP_READ | DRM_ETNA_PREP_WRITE);
		VIV2D_DBG_MSG("Viv2DComposite del bo %p", tmp.bo);
		etna_bo_del(tmp.bo);
	} else {
		// new srcX,srcY group
		if (op->prev_src_x != srcX || op->prev_src_y != srcY || op->cur_rect >= VIV2D_MAX_RECTS) {
			// stream previous rects
			if (op->prev_src_x > -1) {
				_Viv2DStreamRects(v2d, op->rects, op->cur_rect);
				op->cur_rect = 0;
			}

			// create states for srcX,srcY group
			switch (op->src_type) {
			case viv2d_src_1x1_repeat:
				_Viv2DStreamSrc(v2d, op->src, 0, 0, 1, 1);
				etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_LOW,
				               VIVS_DE_STRETCH_FACTOR_LOW_X(((op->src->width - 1) << 16) / (op->dst->width - 1)));
				etna_set_state(v2d->stream, VIVS_DE_STRETCH_FACTOR_HIGH,
				               VIVS_DE_STRETCH_FACTOR_HIGH_Y(((op->src->height - 1) << 16) / (op->dst->height - 1)));
				_Viv2DStreamDst(v2d, op->dst, VIVS_DE_DEST_CONFIG_COMMAND_STRETCH_BLT, NULL);
				break;
			case viv2d_src_solid:
				_Viv2DStreamDst(v2d, op->dst, VIVS_DE_DEST_CONFIG_COMMAND_CLEAR, NULL);
				_Viv2DStreamColor(v2d, op->fg);
				break;
			default:
				_Viv2DStreamSrc(v2d, op->src, srcX, srcY, width, height);
				_Viv2DStreamDst(v2d, op->dst, VIVS_DE_DEST_CONFIG_COMMAND_BIT_BLT, NULL);
				break;
			}
			_Viv2DStreamBlendOp(v2d, op->blend_op, op->src_alpha, op->dst_alpha, op->src_alpha_mode_global, op->dst_alpha_mode_global);
		}

		_Viv2DOpAddRect(op, dstX, dstY, width, height);

		op->prev_src_x = srcX;
		op->prev_src_y = srcY;
	}
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
	Viv2DOp *op = v2d->op;

	if (op->msk) {
		// already done masked operations
	} else {
		_Viv2DStreamRects(v2d, op->rects, op->cur_rect);

		VIV2D_DBG_MSG("Viv2DDoneComposite %d", v2d->stream->offset);
		_Viv2DStreamCommit(v2d);
	}

	if (op->src)
		Viv2DDetachBo(pARMSOC, op->src->armsocPix);
	if (op->msk)
		Viv2DDetachBo(pARMSOC, op->msk->armsocPix);

	Viv2DDetachBo(pARMSOC, op->dst->armsocPix);

	_Viv2DOpDestroy(op);
	v2d->op = NULL;
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
#if 0 // TODO need to change CloseScreen/FreeScreen ..
	exaDriverFini(pScreen);
	free(pNv->EXADriverPtr);
#endif
	return TRUE;
}

static void
FreeScreen(FREE_SCREEN_ARGS_DECL)
{
}

#if 0

void *AttachViv2DDMABuf(OMAPEXAPtr omap_exa, int fd) {
	Viv2DEXAPtr v2d_exa = (Viv2DEXAPtr)omap_exa;
	Viv2DPtr v2d = v2d_exa->v2d;
	v2d->bo = etna_bo_from_dmabuf(v2d->dev, fd);
	return NULL;
}
#endif

struct ARMSOCEXARec *
InitViv2DEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd)
{
	Viv2DEXAPtr v2d_exa = calloc(sizeof (*v2d_exa), 1);
	struct ARMSOCEXARec *omap_exa = (struct ARMSOCEXARec *)v2d_exa;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	ExaDriverPtr exa;
	Viv2DPtr v2d = calloc(sizeof (*v2d), 1);
	int etnavivFD, scanoutFD;
	uint64_t model, revision;

	etnavivFD = VIV2DDetectDevice("etnaviv");

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

	//v2d->bo = NULL;

	scanoutFD = armsoc_bo_get_dmabuf(pARMSOC->scanout);
	v2d->bo = etna_bo_from_dmabuf(v2d->dev, scanoutFD);

	v2d_exa->v2d = v2d;

	exa = exaDriverAlloc();
	if (!exa) {
		goto fail;
	}

	v2d_exa->exa = exa;

	exa->exa_major = EXA_VERSION_MAJOR;
	exa->exa_minor = EXA_VERSION_MINOR;

	exa->pixmapOffsetAlign = 0;
	exa->pixmapPitchAlign = 32 * 4; // see OMAPCalculateStride()
	exa->flags = EXA_OFFSCREEN_PIXMAPS |
	             EXA_HANDLES_PIXMAPS | EXA_SUPPORTS_PREPARE_AUX;
	exa->maxX = 4096;
	exa->maxY = 4096;

	/* Required EXA functions: */
	exa->WaitMarker = ARMSOCWaitMarker;
	exa->CreatePixmap2 = Viv2DCreatePixmap;
	exa->DestroyPixmap = Viv2DDestroyPixmap;
	exa->ModifyPixmapHeader = Viv2DModifyPixmapHeader;

	exa->PrepareAccess = ARMSOCPrepareAccess;
	exa->FinishAccess = ARMSOCFinishAccess;
	exa->PixmapIsOffscreen = ARMSOCPixmapIsOffscreen;

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

	if (! exaDriverInit(pScreen, exa)) {
		ERROR_MSG("exaDriverInit failed");
		goto fail;
	}

	omap_exa->CloseScreen = CloseScreen;
	omap_exa->FreeScreen = FreeScreen;

	INFO_MSG("Viv2DEXA: initialized.");

	return omap_exa;

fail:
	if (v2d_exa) {
		free(v2d_exa);
	}
	return NULL;
}


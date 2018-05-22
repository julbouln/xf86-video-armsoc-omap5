/* -*- mode: C; c-file-style: "k&r"; tab-width 4; indent-tabs-mode: t; -*- */

/*
 * Copyright © 2012 Texas Instruments, Inc
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
 *    Rob Clark <rob@ti.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#include "fourcc.h"
#include "drm_fourcc.h"

#include "armsoc_driver.h"
#include "armsoc_exa.h"

#define NUM_TEXTURE_PORTS 32		/* this is basically arbitrary */

#define IMAGE_MAX_W 2048
#define IMAGE_MAX_H 2048

#define ALIGN(val, align)	(((val) + (align) - 1) & ~((align) - 1))

typedef struct {
	unsigned int format;
	int nplanes;
	PixmapPtr pSrcPix[3];
} ARMSOCPortPrivRec, *ARMSOCPortPrivPtr;


static XF86VideoEncodingRec ARMSOCVideoEncoding[] =
{
	{ 0, (char *)"XV_IMAGE", IMAGE_MAX_W, IMAGE_MAX_H, {1, 1} },
};

static XF86VideoFormatRec ARMSOCVideoFormats[] =
{
	{15, TrueColor}, {16, TrueColor}, {24, TrueColor},
	{15, DirectColor}, {16, DirectColor}, {24, DirectColor}
};

static XF86AttributeRec ARMSOCVideoTexturedAttributes[] =
{
};

static XF86ImageRec ARMSOCVideoTexturedImages[MAX_FORMATS];

/* do we support video? */
static inline Bool has_video(struct ARMSOCRec * pARMSOC)
{
	return pARMSOC->pARMSOCEXA &&
	       pARMSOC->pARMSOCEXA->GetFormats &&
	       pARMSOC->pARMSOCEXA->PutTextureImage;
}


typedef int (*ARMSOCPutTextureImageProc)(
    PixmapPtr pSrcPix, BoxPtr pSrcBox,
    PixmapPtr pOsdPix, BoxPtr pOsdBox,
    PixmapPtr pDstPix, BoxPtr pDstBox,
    BoxPtr fullDstBox,
    void *closure);
/**
 * Helper function to implement video blit, handling clipping, damage, etc..
 *
 * TODO: move to EXA?
 */
static int
ARMSOCVidCopyArea(DrawablePtr pSrcDraw, BoxPtr pSrcBox,
                  DrawablePtr pOsdDraw, BoxPtr pOsdBox,
                  DrawablePtr pDstDraw, BoxPtr pDstBox,
                  ARMSOCPutTextureImageProc PutTextureImage, void *closure,
                  RegionPtr clipBoxes)
{
	ScreenPtr pScreen = pDstDraw->pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	PixmapPtr pSrcPix = draw2pix(pSrcDraw);
	PixmapPtr pOsdPix = draw2pix(pOsdDraw);
	PixmapPtr pDstPix = draw2pix(pDstDraw);
	pixman_fixed_t sx, sy, tx, ty;
	pixman_transform_t srcxfrm;
	BoxPtr pbox;
	int nbox, dx, dy, ret = Success;

#ifdef COMPOSITE
	DEBUG_MSG("--> %dx%d, %dx%d", pDstPix->screen_x, pDstPix->screen_y,
	          pDstDraw->x, pDstDraw->y);
	/* Convert screen coords to pixmap coords */
	if (pDstPix->screen_x || pDstPix->screen_y) {
		RegionTranslate(clipBoxes, -pDstPix->screen_x, -pDstPix->screen_y);
	}
	dx = pDstPix->screen_x;
	dy = pDstPix->screen_y;
#else
	dx = 0;
	dy = 0;
#endif

	/* the clip-region gives coordinates in dst's coord space..  generate
	 * a transform that can be used to work backwards from dst->src coord
	 * space:
	 */
	sx = ((pixman_fixed_48_16_t) (pSrcBox->x2 - pSrcBox->x1) << 16) /
	     (pDstBox->x2 - pDstBox->x1);
	sy = ((pixman_fixed_48_16_t) (pSrcBox->y2 - pSrcBox->y1) << 16) /
	     (pDstBox->y2 - pDstBox->y1);
	tx = ((pixman_fixed_48_16_t)(pDstBox->x1 - dx) << 16);
	ty = ((pixman_fixed_48_16_t)(pDstBox->y1 - dy) << 16);

	pixman_transform_init_scale(&srcxfrm, sx, sy);
	pixman_transform_translate(NULL, &srcxfrm, tx, ty);

	// TODO generate transform for osd as well

	pbox = RegionRects(clipBoxes);
	nbox = RegionNumRects(clipBoxes);

	while (nbox--) {
		RegionRec damage;
		BoxRec dstb = *pbox;
		BoxRec srcb = *pbox;
		BoxRec osdb = *pbox;

		pixman_transform_bounds(&srcxfrm, &srcb);
		//pixman_transform_bounds(&osdxfrm, &osdb);

		/* cropping is done in src coord space, post transform: */
		srcb.x1 += pSrcBox->x1;
		srcb.y1 += pSrcBox->y1;
		srcb.x2 += pSrcBox->x1;
		srcb.y2 += pSrcBox->y1;

		DEBUG_MSG("%d,%d %d,%d -> %d,%d %d,%d",
		          srcb.x1, srcb.y1, srcb.x2, srcb.y2,
		          dstb.x1, dstb.y1, dstb.x2, dstb.y2);

		ret = PutTextureImage(pSrcPix, &srcb, pOsdPix, &osdb,
		                      pDstPix, &dstb, pDstBox, closure);
		if (ret != Success) {
			break;
		}

		RegionInit(&damage, &dstb, 1);

#ifdef COMPOSITE
		/* Convert screen coords to pixmap coords */
		if (pDstPix->screen_x || pDstPix->screen_y) {
			RegionTranslate(&damage, pDstPix->screen_x, pDstPix->screen_y);
		}
#endif

		DamageRegionAppend(pDstDraw, &damage);
		RegionUninit(&damage);

		pbox++;
	}

	DamageRegionProcessPending(pDstDraw);

	return ret;
}

static PixmapPtr
setupplane(ScreenPtr pScreen, PixmapPtr pSrcPix, int width, int height,
           int depth, int srcpitch, int bufpitch, unsigned char **bufp)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(pScrn);
	struct armsoc_bo *bo;
	unsigned char *src, *buf = *bufp;
	int i;

	if (pSrcPix && ((pSrcPix->drawable.height != height) ||
	                (pSrcPix->drawable.width != width))) {
		pScreen->DestroyPixmap(pSrcPix);
		pSrcPix = NULL;
	}

	if (!pSrcPix) {
		pSrcPix = pScreen->CreatePixmap(pScreen, width, height, depth, CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
	}

	bo = ARMSOCPixmapBo(pSrcPix);
	armsoc_bo_cpu_prep(bo, ARMSOC_GEM_WRITE);
	src = armsoc_bo_map(bo);

	/* copy from buf to src pixmap: */
	for (i = 0; i < height; i++) {
		memcpy(src, buf, srcpitch);
		src += srcpitch;
		buf += bufpitch;
	}

	armsoc_bo_cpu_fini(bo, ARMSOC_GEM_WRITE);

	*bufp = buf;

	if (pARMSOC->pARMSOCEXA->Reattach)
		pARMSOC->pARMSOCEXA->Reattach(pSrcPix, width, height, srcpitch);

	return pSrcPix;
}

static void
freebufs(ScreenPtr pScreen, ARMSOCPortPrivPtr pPriv)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(pPriv->pSrcPix); i++) {
		if (pPriv->pSrcPix[i])
			pScreen->DestroyPixmap(pPriv->pSrcPix[i]);
		pPriv->pSrcPix[i] = NULL;
	}
}


static void
ARMSOCVideoStopVideo(ScrnInfoPtr pScrn, pointer data, Bool exit)
{
	/* maybe we can deallocate pSrcPix here?? */
}

static int
ARMSOCVideoSetPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
                            INT32 value, pointer data)
{
	return BadMatch;
}

static int
ARMSOCVideoGetPortAttribute(ScrnInfoPtr pScrn, Atom attribute,
                            INT32 *value, pointer data)
{
	return BadMatch;
}

static void
ARMSOCVideoQueryBestSize(ScrnInfoPtr pScrn, Bool motion,
                         short vid_w, short vid_h,
                         short drw_w, short drw_h,
                         unsigned int *p_w, unsigned int *p_h,
                         pointer data)
{
	/* currently no constraints.. */
	*p_w = drw_w;
	*p_h = drw_h;
}

static int ARMSOCVideoPutTextureImage(
    PixmapPtr pSrcPix, BoxPtr pSrcBox,
    PixmapPtr pOsdPix, BoxPtr pOsdBox,
    PixmapPtr pDstPix, BoxPtr pDstBox,
    BoxPtr fullDstBox,
    void *closure)
{
	ScreenPtr pScreen = pDstPix->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(pScrn);
	ARMSOCPortPrivPtr pPriv = closure;
	Bool ret;

	if (pARMSOC->pARMSOCEXA->PutTextureImage) {
		DEBUG_MSG("src: %dx%d; %d,%d %d,%d",
		          pSrcPix->drawable.width, pSrcPix->drawable.height,
		          pSrcBox->x1, pSrcBox->y1, pSrcBox->x2, pSrcBox->y2);
		DEBUG_MSG("dst: %dx%d; %d,%d %d,%d",
		          pDstPix->drawable.width, pDstPix->drawable.height,
		          pDstBox->x1, pDstBox->y1, pDstBox->x2, pDstBox->y2);

		ret = pARMSOC->pARMSOCEXA->PutTextureImage(pSrcPix, pSrcBox,
		        pOsdPix, pOsdBox, pDstPix, pDstBox, fullDstBox,
		        pPriv->nplanes - 1, &pPriv->pSrcPix[1],
		        pPriv->format);
		if (ret) {
			return Success;
		}
	}
	DEBUG_MSG("PutTextureImage failed");


	return BadImplementation;
}

/**
 * The main function for XV, called to blit/scale/colorcvt an image
 * to it's destination drawable
 *
 * The source rectangle of the video is defined by (src_x, src_y, src_w, src_h).
 * The dest rectangle of the video is defined by (drw_x, drw_y, drw_w, drw_h).
 * id is a fourcc code for the format of the video.
 * buf is the pointer to the source data in system memory.
 * width and height are the w/h of the source data.
 * If "sync" is TRUE, then we must be finished with *buf at the point of return
 * (which we always are).
 * clipBoxes is the clipping region in screen space.
 * data is a pointer to our port private.
 * drawable is some Drawable, which might not be the screen in the case of
 * compositing.  It's a new argument to the function in the 1.1 server.
 */
static int
ARMSOCVideoPutImage(ScrnInfoPtr pScrn, short src_x, short src_y, short drw_x,
                    short drw_y, short src_w, short src_h, short drw_w, short drw_h,
                    int id, unsigned char *buf, short width, short height,
                    Bool Sync, RegionPtr clipBoxes, pointer data, DrawablePtr pDstDraw)
{
	int ret;
	ScreenPtr pScreen = pDstDraw->pScreen;
	ARMSOCPortPrivPtr pPriv = (ARMSOCPortPrivPtr)data;
//	struct ARMSOCRec * pARMSOC = ARMSOCPTR(pScrn);

	BoxRec srcb = {
		.x1 = src_x,
		.y1 = src_y,
		.x2 = src_x + src_w,
		.y2 = src_y + src_h,
	};
	BoxRec dstb = {
		.x1 = drw_x,
		.y1 = drw_y,
		.x2 = drw_x + drw_w,
		.y2 = drw_y + drw_h,
	};
	int i, depth, nplanes;
	int srcpitch1, srcpitch2, bufpitch1, bufpitch2, src_h2, src_w2;

	switch (id) {
//	case fourcc_code('N','V','1','2'):
//		break;
	case fourcc_code('Y', 'V', '1', '2'):
	case fourcc_code('I', '4', '2', '0'):
		nplanes = 3;
		srcpitch1 = ALIGN(src_w, 16);
		srcpitch2 = ALIGN(src_w / 2, 8);
		bufpitch1 = ALIGN(width, 4);
		bufpitch2 = ALIGN(width / 2, 4);
		depth = 8;
		src_h2 = src_h / 2;
		src_w2 = src_w / 2;
		break;
	case fourcc_code('U', 'Y', 'V', 'Y'):
	case fourcc_code('Y', 'U', 'Y', 'V'):
	case fourcc_code('Y', 'U', 'Y', '2'):
		nplanes = 1;
		srcpitch1 = src_w * 2;
		bufpitch1 = width * 2;
		depth = 16;
		srcpitch2 = bufpitch2 = src_h2 = src_w2 = 0;
		break;
	default:
		ERROR_MSG("unexpected format: %08x (%4.4s)", id, (char *)&id);
		return BadMatch;
	}

	if (pPriv->format != id) {
		freebufs(pScreen, pPriv);
	}

	pPriv->format = id;
	pPriv->nplanes = nplanes;

	pPriv->pSrcPix[0] = setupplane(pScreen, pPriv->pSrcPix[0],
	                               src_w, src_h, depth, srcpitch1, bufpitch1, &buf);

	for (i = 1; i < pPriv->nplanes; i++) {
		pPriv->pSrcPix[i] = setupplane(pScreen, pPriv->pSrcPix[i],
		                               src_w2, src_h2, depth, srcpitch2, bufpitch2, &buf);
	}

	/* note: ARMSOCVidCopyArea() handles the composite-clip, so we can
	 * ignore clipBoxes
	 */
	/*	drmVBlank vbl = { .request = {
				.type = DRM_VBLANK_RELATIVE,
				.sequence = 0,
			}
		};

		ret = drmWaitVBlank(pARMSOC->drmFD, &vbl);
		if (ret) {
			ERROR_MSG("get vblank counter failed: %s", strerror(errno));
		}
	*/
	ret = ARMSOCVidCopyArea(&pPriv->pSrcPix[0]->drawable, &srcb,
	                        NULL, NULL, pDstDraw, &dstb,
	                        ARMSOCVideoPutTextureImage, pPriv, clipBoxes);
	/*
		vbl.request.sequence = vbl.reply.sequence + 1;
		vbl.request.type = DRM_VBLANK_ABSOLUTE;

		ret = drmWaitVBlank(pARMSOC->drmFD, &vbl);
		if (ret) {
			ERROR_MSG("get vblank counter failed: %s", strerror(errno));
		}
	*/
	return ret;

}

/**
 * QueryImageAttributes
 *
 * calculates
 * - size (memory required to store image),
 * - pitches,
 * - offsets
 * of image
 * depending on colorspace (id) and dimensions (w,h) of image
 * values of
 * - w,
 * - h
 * may be adjusted as needed
 *
 * @param pScrn unused
 * @param id colorspace of image
 * @param w pointer to width of image
 * @param h pointer to height of image
 * @param pitches pitches[i] = length of a scanline in plane[i]
 * @param offsets offsets[i] = offset of plane i from the beginning of the image
 * @return size of the memory required for the XvImage queried
 */
static int
ARMSOCVideoQueryImageAttributes(ScrnInfoPtr pScrn, int id,
                                unsigned short *w, unsigned short *h,
                                int *pitches, int *offsets)
{
	int size, tmp;

	if (*w > IMAGE_MAX_W)
		*w = IMAGE_MAX_W;
	if (*h > IMAGE_MAX_H)
		*h = IMAGE_MAX_H;

	*w = (*w + 1) & ~1; // width rounded up to an even number
	if (offsets)
		offsets[0] = 0;

	switch (id) {
	case fourcc_code('Y', 'V', '1', '2'):
	case fourcc_code('I', '4', '2', '0'):
		*h = (*h + 1) & ~1; // height rounded up to an even number
		size = (*w + 3) & ~3; // width rounded up to a multiple of 4
		if (pitches)
			pitches[0] = size; // width rounded up to a multiple of 4
		size *= *h;
		if (offsets)
			offsets[1] = size; // number of pixels in "rounded up" image
		tmp = ((*w >> 1) + 3) & ~3; // width/2 rounded up to a multiple of 4
		if (pitches)
			pitches[1] = pitches[2] = tmp; // width/2 rounded up to a multiple of 4
		tmp *= (*h >> 1); // 1/4*number of pixels in "rounded up" image
		size += tmp; // 5/4*number of pixels in "rounded up" image
		if (offsets)
			offsets[2] = size; // 5/4*number of pixels in "rounded up" image
		size += tmp; // = 3/2*number of pixels in "rounded up" image
		break;
	case fourcc_code('U', 'Y', 'V', 'Y'):
	case fourcc_code('Y', 'U', 'Y', '2'):
		size = *w << 1; // 2*width
		if (pitches)
			pitches[0] = size; // 2*width
		size *= *h; // 2*width*height
		break;
	default:
		xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "Unknown colorspace: %x\n", id);
		*w = *h = size = 0;
		break;
	}

	return size;
}

static XF86VideoAdaptorPtr
ARMSOCVideoSetupTexturedVideo(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(pScrn);
	XF86VideoAdaptorPtr adapt;
	ARMSOCPortPrivPtr pPriv;
	int i, nformats, nsupported;
	static unsigned int formats[MAX_FORMATS];

	if (!has_video(pARMSOC)) {
		return NULL;
	}

	if (!(adapt = calloc(1, sizeof(XF86VideoAdaptorRec) +
	                     sizeof(ARMSOCPortPrivRec) +
	                     (sizeof(DevUnion) * NUM_TEXTURE_PORTS)))) {
		return NULL;
	}


	adapt->type			= XvWindowMask | XvInputMask | XvImageMask;
	adapt->flags		= 0;
	adapt->name			= (char *)"ARMSOC Textured Video";
	adapt->nEncodings	= ARRAY_SIZE(ARMSOCVideoEncoding);
	adapt->pEncodings	= ARMSOCVideoEncoding;
	adapt->nFormats		= ARRAY_SIZE(ARMSOCVideoFormats);
	adapt->pFormats		= ARMSOCVideoFormats;
	adapt->nPorts		= NUM_TEXTURE_PORTS;
	adapt->pPortPrivates	= (DevUnion*)(&adapt[1]);

	pPriv = (ARMSOCPortPrivPtr)(&adapt->pPortPrivates[NUM_TEXTURE_PORTS]);
	for (i = 0; i < NUM_TEXTURE_PORTS; i++)
		adapt->pPortPrivates[i].ptr = (pointer)(pPriv);

	adapt->nAttributes		= ARRAY_SIZE(ARMSOCVideoTexturedAttributes);
	adapt->pAttributes		= ARMSOCVideoTexturedAttributes;

	nformats = pARMSOC->pARMSOCEXA->GetFormats(formats);
	nsupported = 0;
	for (i = 0; i < nformats; i++) {
		switch (formats[i]) {
//		case fourcc_code('N','V','1','2'):
//			break;
		case fourcc_code('Y', 'V', '1', '2'):
			ARMSOCVideoTexturedImages[nsupported++] =
			    (XF86ImageRec)XVIMAGE_YV12;
			break;
		case fourcc_code('I', '4', '2', '0'):
			ARMSOCVideoTexturedImages[nsupported++] =
			    (XF86ImageRec)XVIMAGE_I420;
			break;
		case fourcc_code('U', 'Y', 'V', 'Y'):
			ARMSOCVideoTexturedImages[nsupported++] =
			    (XF86ImageRec)XVIMAGE_UYVY;
			break;
		case fourcc_code('Y', 'U', 'Y', 'V'):
		case fourcc_code('Y', 'U', 'Y', '2'):
			ARMSOCVideoTexturedImages[nsupported++] =
			    (XF86ImageRec)XVIMAGE_YUY2;
			break;
		default:
			/* ignore unsupported formats */
			break;
		}
	}

	adapt->nImages			= nsupported;
	adapt->pImages			= ARMSOCVideoTexturedImages;

	adapt->PutVideo			= NULL;
	adapt->PutStill			= NULL;
	adapt->GetVideo			= NULL;
	adapt->GetStill			= NULL;
	adapt->StopVideo		= ARMSOCVideoStopVideo;
	adapt->SetPortAttribute	= ARMSOCVideoSetPortAttribute;
	adapt->GetPortAttribute	= ARMSOCVideoGetPortAttribute;
	adapt->QueryBestSize	= ARMSOCVideoQueryBestSize;
	adapt->PutImage			= ARMSOCVideoPutImage;
	adapt->QueryImageAttributes	= ARMSOCVideoQueryImageAttributes;

	return adapt;
}

/**
 * If EXA implementation supports GetFormats() and PutTextureImage() we can
 * use that to implement XV.  There is a copy involve because we need to
 * copy the buffer to a texture (TODO possibly we can support wrapping
 * external buffers, but current EXA submodule API doesn't give us a way to
 * do that).  So for optimal path from hw decoders to display, dri2video
 * should be used.  But this at least helps out legacy apps.
 */
Bool
ARMSOCVideoScreenInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(pScrn);
	XF86VideoAdaptorPtr textureAdaptor =
	    ARMSOCVideoSetupTexturedVideo(pScreen);

	if (textureAdaptor) {
		XF86VideoAdaptorPtr *adaptors, *newAdaptors;
		int n = xf86XVListGenericAdaptors(pScrn, &adaptors);
		newAdaptors = calloc(n + 1, sizeof(XF86VideoAdaptorPtr *));
		memcpy(newAdaptors, adaptors, n * sizeof(XF86VideoAdaptorPtr *));
		pARMSOC->textureAdaptor = textureAdaptor;
		newAdaptors[n] = textureAdaptor;
		xf86XVScreenInit(pScreen, newAdaptors, n + 1);
		free(newAdaptors);
		return TRUE;
	}

	return FALSE;
}

void
ARMSOCVideoCloseScreen(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(pScrn);
	if (pARMSOC->textureAdaptor) {
		ARMSOCPortPrivPtr pPriv = (ARMSOCPortPrivPtr)
		                          pARMSOC->textureAdaptor->pPortPrivates[0].ptr;
		freebufs(pScreen, pPriv);
	}
}

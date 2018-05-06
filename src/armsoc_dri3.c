#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "fb.h"
#include "gcstruct.h"
#include "xf86.h"
#include "dri3.h"
#include "misyncshm.h"
#include "compat-api.h"

#include "armsoc_driver.h"
#include "armsoc_exa.h"

#include "drmmode_driver.h"

static Bool ARMSOCDRI3Authorise(struct ARMSOCRec *pARMSOC, int fd)
{
	int ret;
	struct stat st;
	drm_magic_t magic;

	if (fstat(fd, &st) || !S_ISCHR(st.st_mode))
		return FALSE;

	/*
	 * If the device is a render node, we don't need to auth it.
	 * Render devices start at minor number 128 and up, though it
	 * would be nice to have some other test for this.
	 */
	if (st.st_rdev & 0x80)
		return TRUE;

	ret = drmGetMagic(fd, &magic);
	if (ret) {
		EARLY_ERROR_MSG("ARMSOCDRI3Open cannot get magic : %d", ret);
		return FALSE;
	}

	ret = drmAuthMagic(pARMSOC->drmFD, magic);
	if (ret) {
		EARLY_ERROR_MSG("ARMSOCDRI3Open cannot auth magic : %d", ret);
		return FALSE;
	}

	return TRUE;
}

static int ARMSOCDRI3Open(ScreenPtr pScreen, RRProviderPtr provider, int *o)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);

//	int fd = drmOpen(pARMSOC->deviceName, NULL);
	int fd = open(pARMSOC->deviceName, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		ERROR_MSG("ARMSOCDRI3Open cannot open %s", pARMSOC->deviceName);
		return BadAlloc;
	} else {
		INFO_MSG("ARMSOCDRI3Open %s opened in %d",  pARMSOC->deviceName, fd);
	}

	if (!ARMSOCDRI3Authorise(pARMSOC, fd)) {
		ERROR_MSG("ARMSOCDRI3Open cannot authorize %s : %d", pARMSOC->deviceName, fd);
		close(fd);
		return BadMatch;
	}

	*o = fd;

	return Success;
}

static PixmapPtr ARMSOCDRI3PixmapFromFD(ScreenPtr pScreen, int fd,
                                        CARD16 width, CARD16 height, CARD16 stride, CARD8 depth, CARD8 bpp)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	PixmapPtr pixmap;
	struct ARMSOCPixmapPrivRec *priv;

	pixmap = pScreen->CreatePixmap(pScreen, width, height, depth, CREATE_PIXMAP_USAGE_BACKING_PIXMAP);
	if (pixmap == NullPixmap) {
		ERROR_MSG("ARMSOCDRI3Open cannot create pixmap");
		return pixmap;
	}
	INFO_MSG("ARMSOCDRI3PixmapFromFD %p %dx%d %d/%d %d", pixmap, width, height, depth, bpp, stride);

	pScreen->ModifyPixmapHeader(pixmap, width, height, depth, bpp, stride, NULL);

	priv = exaGetPixmapDriverPrivate(pixmap);

	if (!priv || !priv->bo) {
		ERROR_MSG("ARMSOCDRI3Open no privPix");
		pScreen->DestroyPixmap(pixmap);
		return NullPixmap;
	}

	armsoc_bo_put_dmabuf(priv->bo, fd);

	if(pARMSOC->pARMSOCEXA->Reattach)
		pARMSOC->pARMSOCEXA->Reattach(pixmap, width, height, stride);

	return pixmap;
}

static int ARMSOCDRI3FDFromPixmap(ScreenPtr pScreen, PixmapPtr pixmap,
                                  CARD16 *stride, CARD32 *size)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCPixmapPrivRec *priv = exaGetPixmapDriverPrivate(pixmap);

	INFO_MSG("ARMSOCDRI3FDFromPixmap");

	/* Only support pixmaps backed by an etnadrm bo */
	if (!priv || !priv->bo)
		return BadMatch;

	*stride = pixmap->devKind;
	*size = armsoc_bo_size(priv->bo);

	return armsoc_bo_get_dmabuf(priv->bo);
}

static dri3_screen_info_rec armsoc_dri3_info = {
	.version = 0,
	.open = ARMSOCDRI3Open,
	.pixmap_from_fd = ARMSOCDRI3PixmapFromFD,
	.fd_from_pixmap = ARMSOCDRI3FDFromPixmap,
};

Bool ARMSOCDRI3ScreenInit(ScreenPtr pScreen)
{
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	struct stat st;

	if (!pARMSOC)
		return FALSE;

	if (fstat(pARMSOC->drmFD, &st) || !S_ISCHR(st.st_mode))
		return FALSE;

	if (!miSyncShmScreenInit(pScreen))
		return FALSE;

	INFO_MSG("ARMSOC DRI3 init");

	return dri3_screen_init(pScreen, &armsoc_dri3_info);
}

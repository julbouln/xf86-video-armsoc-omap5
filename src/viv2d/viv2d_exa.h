#ifndef VIV2D_EXA_H
#define VIV2D_EXA_H

#include "armsoc_driver.h"
#include "armsoc_exa.h"

typedef struct {
	struct ARMSOCEXARec base;
	ExaDriverPtr exa;
	/* add any other driver private data here.. */
	Viv2DPtr v2d;
} Viv2DEXARec, *Viv2DEXAPtr;

static inline Viv2DRec*
Viv2DPrivFromPixmap(PixmapPtr pPixmap)
{
	ScrnInfoPtr pScrn = pix2scrn(pPixmap);
	struct ARMSOCRec *pARMSOC = ARMSOCPTR(pScrn);
	Viv2DEXAPtr exa = (Viv2DEXAPtr)(pARMSOC->pARMSOCEXA);
	Viv2DRec *v2d = exa->v2d;
	return v2d;
}

static inline Viv2DRec*
Viv2DPrivFromARMSOC(struct ARMSOCRec *pARMSOC) {
	Viv2DEXAPtr exa = (Viv2DEXAPtr)(pARMSOC->pARMSOCEXA);
	Viv2DRec *v2d = exa->v2d;
	return v2d;
}

static inline Viv2DRec*
Viv2DPrivFromScreen(ScreenPtr pScreen)
{
	Viv2DRec *v2d;
	Viv2DEXAPtr exa;
	struct ARMSOCRec *pARMSOC = ARMSOCPTR_FROM_SCREEN(pScreen);
	exa = (Viv2DEXAPtr)(pARMSOC->pARMSOCEXA);
	v2d = exa->v2d;
	return v2d;
}

static inline Viv2DPixmapPrivPtr Viv2DPixmapPrivFromPixmap(PixmapPtr pPixmap){
	struct ARMSOCPixmapPrivRec *pixPriv = exaGetPixmapDriverPrivate(pPixmap);
	return pixPriv->priv;
}

struct ARMSOCEXARec *InitViv2DEXA(ScreenPtr pScreen, ScrnInfoPtr pScrn, int fd);

#endif
/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_DIX_CONFIG_H
#include "dix-config.h"
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>

#include <xorg-server.h>
#include <xf86.h>
#include <xf86Crtc.h>
#include <xf86drm.h>
#include <xf86str.h>
#include <present.h>

#include "armsoc_driver.h"
#include "drmmode_display.h"

//#define ARMSOC_PRESENT_FLIP 1
//#define ARMSOC_PRESENT_WAIT_VBLANK 1

#define ARMSOC_PRESENT_DBG_MSG(fmt, ...)
/*#define ARMSOC_PRESENT_DBG_MSG(fmt, ...) \
		do { xf86Msg(X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
*/

extern drmEventContext event_context;

struct armsoc_present_vblank_event {
	uint64_t        event_id;
	Bool            unflip;
};

typedef void (*armsoc_drm_handler_proc)(uint64_t frame,
                                        uint64_t usec,
                                        void *data);

typedef void (*armsoc_drm_abort_proc)(void *data);


struct armsoc_drm_queue {
	struct xorg_list list;
	xf86CrtcPtr crtc;
	uint32_t seq;
	void *data;
	ScrnInfoPtr scrn;
	armsoc_drm_handler_proc handler;
	armsoc_drm_abort_proc abort;
};

static struct xorg_list armsoc_drm_queue;

static uint32_t armsoc_drm_seq;

static void armsoc_box_intersect(BoxPtr dest, BoxPtr a, BoxPtr b)
{
	dest->x1 = a->x1 > b->x1 ? a->x1 : b->x1;
	dest->x2 = a->x2 < b->x2 ? a->x2 : b->x2;
	if (dest->x1 >= dest->x2) {
		dest->x1 = dest->x2 = dest->y1 = dest->y2 = 0;
		return;
	}

	dest->y1 = a->y1 > b->y1 ? a->y1 : b->y1;
	dest->y2 = a->y2 < b->y2 ? a->y2 : b->y2;
	if (dest->y1 >= dest->y2)
		dest->x1 = dest->x2 = dest->y1 = dest->y2 = 0;
}

static void armsoc_crtc_box(xf86CrtcPtr crtc, BoxPtr crtc_box)
{
	if (crtc->enabled) {
		crtc_box->x1 = crtc->x;
		crtc_box->x2 =
		    crtc->x + xf86ModeWidth(&crtc->mode, crtc->rotation);
		crtc_box->y1 = crtc->y;
		crtc_box->y2 =
		    crtc->y + xf86ModeHeight(&crtc->mode, crtc->rotation);
	} else
		crtc_box->x1 = crtc_box->x2 = crtc_box->y1 = crtc_box->y2 = 0;
}

static int armsoc_box_area(BoxPtr box)
{
	return (int)(box->x2 - box->x1) * (int)(box->y2 - box->y1);
}

Bool
armsoc_crtc_on(xf86CrtcPtr crtc)
{
	struct drmmode_crtc_private_rec * drmmode_crtc = crtc->driver_private;

//	return TRUE;
	return crtc->enabled && drmmode_crtc->dpms_mode == DPMSModeOn;
}

/*
 * Return the crtc covering 'box'. If two crtcs cover a portion of
 * 'box', then prefer 'desired'. If 'desired' is NULL, then prefer the crtc
 * with greater coverage
 */

xf86CrtcPtr
armsoc_covering_crtc(ScrnInfoPtr scrn,
                     BoxPtr box, xf86CrtcPtr desired, BoxPtr crtc_box_ret)
{
	xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(scrn);
	xf86CrtcPtr crtc, best_crtc;
	int coverage, best_coverage;
	int c;
	BoxRec crtc_box, cover_box;

	best_crtc = NULL;
	best_coverage = 0;
	crtc_box_ret->x1 = 0;
	crtc_box_ret->x2 = 0;
	crtc_box_ret->y1 = 0;
	crtc_box_ret->y2 = 0;
	for (c = 0; c < xf86_config->num_crtc; c++) {
		crtc = xf86_config->crtc[c];

		/* If the CRTC is off, treat it as not covering */
		if (!armsoc_crtc_on(crtc))
			continue;

		armsoc_crtc_box(crtc, &crtc_box);
		armsoc_box_intersect(&cover_box, &crtc_box, box);
		coverage = armsoc_box_area(&cover_box);
		if (coverage && crtc == desired) {
			*crtc_box_ret = crtc_box;
			return crtc;
		}
		if (coverage > best_coverage) {
			*crtc_box_ret = crtc_box;
			best_crtc = crtc;
			best_coverage = coverage;
		}
	}
	return best_crtc;
}

xf86CrtcPtr
armsoc_crtc_covering_drawable(DrawablePtr pDraw)
{
	ScreenPtr pScreen = pDraw->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	BoxRec box, crtcbox;

	box.x1 = pDraw->x;
	box.y1 = pDraw->y;
	box.x2 = box.x1 + pDraw->width;
	box.y2 = box.y1 + pDraw->height;

	return armsoc_covering_crtc(pScrn, &box, NULL, &crtcbox);
}


static Bool
armsoc_get_kernel_ust_msc(xf86CrtcPtr crtc,
                          uint32_t *msc, uint64_t *ust)
{
	ScreenPtr screen = crtc->randr_crtc->pScreen;
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(scrn);
	struct drmmode_crtc_private_rec * drmmode_crtc = crtc->driver_private;
	drmVBlank vbl;
	int ret;

	/* Get current count */
	vbl.request.type = DRM_VBLANK_RELATIVE | drmmode_crtc->vblank_pipe;
	vbl.request.sequence = 0;
	vbl.request.signal = 0;
#ifdef ARMSOC_PRESENT_WAIT_VBLANK

	ret = drmWaitVBlank(pARMSOC->drmFD, &vbl);
	if (ret) {
		*msc = 0;
		*ust = 0;
		return FALSE;
	} else {
		*msc = vbl.reply.sequence;
		*ust = (CARD64) vbl.reply.tval_sec * 1000000 + vbl.reply.tval_usec;
		ARMSOC_PRESENT_DBG_MSG("armsoc_get_kernel_ust_msc msc:%d ust:%d",*msc,*ust);
		return TRUE;
	}
#else
		*msc = vbl.reply.sequence;
		*ust = (CARD64) vbl.reply.tval_sec * 1000000 + vbl.reply.tval_usec;
		ARMSOC_PRESENT_DBG_MSG("armsoc_get_kernel_ust_msc msc:%d ust:%d",*msc,*ust);
		return TRUE;
#endif

}

/**
 * Convert a 32-bit kernel MSC sequence number to a 64-bit local sequence
 * number, adding in the vblank_offset and high 32 bits, and dealing
 * with 64-bit wrapping
 */
uint64_t
armsoc_kernel_msc_to_crtc_msc(xf86CrtcPtr crtc, uint32_t sequence)
{
	struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
	sequence += drmmode_crtc->vblank_offset;

	if ((int32_t) (sequence - drmmode_crtc->msc_prev) < -0x40000000)
		drmmode_crtc->msc_high += 0x100000000L;
	drmmode_crtc->msc_prev = sequence;
	return drmmode_crtc->msc_high + sequence;
}

int
armsoc_get_crtc_ust_msc(xf86CrtcPtr crtc, CARD64 *ust, CARD64 *msc)
{
	uint32_t kernel_msc;

	if (!armsoc_get_kernel_ust_msc(crtc, &kernel_msc, ust))
		return BadMatch;
	*msc = armsoc_kernel_msc_to_crtc_msc(crtc, kernel_msc);

	return Success;
}


/////

/*
 * Enqueue a potential drm response; when the associated response
 * appears, we've got data to pass to the handler from here
 */
uint32_t
armsoc_drm_queue_alloc(xf86CrtcPtr crtc,
                       void *data,
                       armsoc_drm_handler_proc handler,
                       armsoc_drm_abort_proc abort)
{
	ScreenPtr screen = crtc->randr_crtc->pScreen;
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	struct armsoc_drm_queue *q;

	q = calloc(1, sizeof(struct armsoc_drm_queue));

	if (!q)
		return 0;
	if (!armsoc_drm_seq)
		++armsoc_drm_seq;
	q->seq = armsoc_drm_seq++;
	q->scrn = scrn;
	q->crtc = crtc;
	q->data = data;
	q->handler = handler;
	q->abort = abort;

	xorg_list_add(&q->list, &armsoc_drm_queue);

	return q->seq;
}

/**
 * Abort one queued DRM entry, removing it
 * from the list, calling the abort function and
 * freeing the memory
 */
static void
armsoc_drm_abort_one(struct armsoc_drm_queue *q)
{
	xorg_list_del(&q->list);
	q->abort(q->data);
	free(q);
}

/**
 * Abort all queued entries on a specific scrn, used
 * when resetting the X server
 */
static void
armsoc_drm_abort_scrn(ScrnInfoPtr scrn)
{
	struct armsoc_drm_queue *q, *tmp;

	xorg_list_for_each_entry_safe(q, tmp, &armsoc_drm_queue, list) {
		if (q->scrn == scrn)
			armsoc_drm_abort_one(q);
	}
}

/**
 * Abort by drm queue sequence number.
 */
void
armsoc_drm_abort_seq(ScrnInfoPtr scrn, uint32_t seq)
{
	struct armsoc_drm_queue *q, *tmp;

	xorg_list_for_each_entry_safe(q, tmp, &armsoc_drm_queue, list) {
		if (q->seq == seq) {
			armsoc_drm_abort_one(q);
			break;
		}
	}
}

/*
 * Externally usable abort function that uses a callback to match a single
 * queued entry to abort
 */
void
armsoc_drm_abort(ScrnInfoPtr scrn, Bool (*match)(void *data, void *match_data),
                 void *match_data)
{
	struct armsoc_drm_queue *q;

	xorg_list_for_each_entry(q, &armsoc_drm_queue, list) {
		if (match(q->data, match_data)) {
			armsoc_drm_abort_one(q);
			break;
		}
	}
}


#if 1
#include <sys/poll.h>
/*
 * Flush the DRM event queue when full; makes space for new events.
 *
 * Returns a negative value on error, 0 if there was nothing to process,
 * or 1 if we handled any events.
 */
int
armsoc_flush_drm_events(ScreenPtr screen)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
//    modesettingPtr ms = modesettingPTR(scrn);
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(scrn);

	struct pollfd p = { .fd = pARMSOC->drmFD, .events = POLLIN };
	int r;

	ARMSOC_PRESENT_DBG_MSG("armsoc_flush_drm_events");

	do {
		r = poll(&p, 1, 0);
	} while (r == -1 && (errno == EINTR || errno == EAGAIN));

	/* If there was an error, r will be < 0.  Return that.  If there was
	 * nothing to process, r == 0.  Return that.
	 */
	if (r <= 0)
		return r;

	/* Try to handle the event.  If there was an error, return it. */
	r = drmHandleEvent(pARMSOC->drmFD, &event_context);
	if (r < 0)
		return r;

	/* Otherwise return 1 to indicate that we handled an event. */
	return 1;
}
#endif
/////

static RRCrtcPtr
armsoc_present_get_crtc(WindowPtr window)
{
	xf86CrtcPtr xf86_crtc = armsoc_crtc_covering_drawable(&window->drawable);
	return xf86_crtc ? xf86_crtc->randr_crtc : NULL;
}

static int
armsoc_present_get_ust_msc(RRCrtcPtr crtc, CARD64 *ust, CARD64 *msc)
{
	xf86CrtcPtr xf86_crtc = crtc->devPrivate;

	return armsoc_get_crtc_ust_msc(xf86_crtc, ust, msc);
}

/*
 * Called when the queued vblank event has occurred
 */
static void
armsoc_present_vblank_handler(uint64_t msc, uint64_t usec, void *data)
{
	struct armsoc_present_vblank_event *event = data;

	ARMSOC_PRESENT_DBG_MSG("\t\tmh %lld msc %llu\n",
	              (long long) event->event_id, (long long) msc);

	present_event_notify(event->event_id, usec, msc);
	free(event);
}


#define MAX_VBLANK_OFFSET 1000
/**
 * Convert a 64-bit adjusted MSC value into a 32-bit kernel sequence number,
 * removing the high 32 bits and subtracting out the vblank_offset term.
 *
 * This also updates the vblank_offset when it notices that the value should
 * change.
 */
uint32_t
armsoc_crtc_msc_to_kernel_msc(xf86CrtcPtr crtc, uint64_t expect)
{
	struct drmmode_crtc_private_rec *drmmode_crtc = crtc->driver_private;
	uint64_t msc;
	uint64_t ust;
	int64_t diff;

	if (armsoc_get_crtc_ust_msc(crtc, &ust, &msc) == Success) {
		diff = expect - msc;

		/* We're way off here, assume that the kernel has lost its mind
		 * and smack the vblank back to something sensible
		 */
		if (diff < -MAX_VBLANK_OFFSET || MAX_VBLANK_OFFSET < diff) {
			drmmode_crtc->vblank_offset += (int32_t) diff;
			if (drmmode_crtc->vblank_offset > -MAX_VBLANK_OFFSET &&
			        drmmode_crtc->vblank_offset < MAX_VBLANK_OFFSET)
				drmmode_crtc->vblank_offset = 0;
		}
	}
	return (uint32_t) (expect - drmmode_crtc->vblank_offset);
}

/*
 * Called when the queued vblank is aborted
 */
static void
armsoc_present_vblank_abort(void *data)
{
	ARMSOC_PRESENT_DBG_MSG("armsoc_present_vblank_abort");
	struct armsoc_present_vblank_event *event = data;

	ARMSOC_PRESENT_DBG_MSG("\t\tma %lld\n", (long long) event->event_id);

	free(event);
}

/*
 * Queue an event to report back to the Present extension when the specified
 * MSC has past
 */
static int
armsoc_present_queue_vblank(RRCrtcPtr crtc,
                            uint64_t event_id,
                            uint64_t msc)
{
	ARMSOC_PRESENT_DBG_MSG("armsoc_present_queue_vblank");
	xf86CrtcPtr xf86_crtc = crtc->devPrivate;
	ScreenPtr screen = crtc->pScreen;
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
//    modesettingPtr ms = modesettingPTR(scrn);
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(scrn);
	struct drmmode_crtc_private_rec * drmmode_crtc = xf86_crtc->driver_private;
	struct armsoc_present_vblank_event *event;
	drmVBlank vbl;
	int ret;
	uint32_t seq;

	event = calloc(sizeof(struct armsoc_present_vblank_event), 1);
	if (!event)
		return BadAlloc;
	event->event_id = event_id;
	seq = armsoc_drm_queue_alloc(xf86_crtc, event,
	                             armsoc_present_vblank_handler,
	                             armsoc_present_vblank_abort);
	if (!seq) {
		free(event);
		return BadAlloc;
	}

	vbl.request.type =
	    DRM_VBLANK_ABSOLUTE | DRM_VBLANK_EVENT | drmmode_crtc->vblank_pipe;
	vbl.request.sequence = armsoc_crtc_msc_to_kernel_msc(xf86_crtc, msc);
	vbl.request.signal = seq;
//#ifdef ARMSOC_PRESENT_WAIT_VBLANK
	for (;;) {
		ret = drmWaitVBlank(pARMSOC->drmFD, &vbl);
		ARMSOC_PRESENT_DBG_MSG("drmWaitVBlank %d", ret);
		if (!ret)
			break;
		/* If we hit EBUSY, then try to flush events.  If we can't, then
		 * this is an error.
		 */
		if (errno != EBUSY || armsoc_flush_drm_events(screen) < 0) {
			ARMSOC_PRESENT_DBG_MSG("abort %d", errno);
			armsoc_drm_abort_seq(scrn, seq);
			return BadAlloc;
		}
	}
//	#endif
	ARMSOC_PRESENT_DBG_MSG("\t\tmq %lld seq %u msc %llu (hw msc %u)\n",
	              (long long) event_id, seq, (long long) msc,
	              vbl.request.sequence);
	return Success;
}

static Bool
armsoc_present_event_match(void *data, void *match_data)
{
	ARMSOC_PRESENT_DBG_MSG("armsoc_present_event_match");
	struct armsoc_present_vblank_event *event = data;
	uint64_t *match = match_data;

	return *match == event->event_id;
}

/*
 * Remove a pending vblank event from the DRM queue so that it is not reported
 * to the extension
 */
static void
armsoc_present_abort_vblank(RRCrtcPtr crtc, uint64_t event_id, uint64_t msc)
{
	ARMSOC_PRESENT_DBG_MSG("armsoc_present_abort_vblank");
	ScreenPtr screen = crtc->pScreen;
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);

	armsoc_drm_abort(scrn, armsoc_present_event_match, &event_id);
}

/*
 * Flush our batch buffer when requested by the Present extension.
 */
static void
armsoc_present_flush(WindowPtr window)
{
#ifdef ARMSOC_PRESENT_FLIP
	ScreenPtr screen = window->drawable.pScreen;
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
//    modesettingPtr ms = modesettingPTR(scrn);
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(scrn);

//    if (ms->drmmode.glamor)
//      glamor_block_handler(screen);
#endif
}

#ifdef ARMSOC_PRESENT_FLIP

/**
 * Callback for the DRM event queue when a flip has completed on all pipes
 *
 * Notify the extension code
 */
static void
armsoc_present_flip_handler(struct ARMSOCRec * pARMSOC, uint64_t msc,
                            uint64_t ust, void *data)
{
	struct armsoc_present_vblank_event *event = data;

	ARMSOC_PRESENT_DBG_MSG("\t\tms:fc %lld msc %llu ust %llu\n",
	              (long long) event->event_id,
	              (long long) msc, (long long) ust);

//    if (event->unflip)
	//      ms->drmmode.present_flipping = FALSE;

	armsoc_present_vblank_handler(msc, ust, event);
}

/*
 * Callback for the DRM queue abort code.  A flip has been aborted.
 */
static void
armsoc_present_flip_abort(struct ARMSOCRec * pARMSOC, void *data)
{
	struct armsoc_present_vblank_event *event = data;

	ARMSOC_PRESENT_DBG_MSG("\t\tms:fa %lld\n", (long long) event->event_id);

	free(event);
}

/*
 * Test to see if page flipping is possible on the target crtc
 */
static Bool
armsoc_present_check_flip(RRCrtcPtr crtc,
                          WindowPtr window,
                          PixmapPtr pixmap,
                          Bool sync_flip)
{
	ScreenPtr screen = window->drawable.pScreen;
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
//    modesettingPtr ms = modesettingPTR(scrn);
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(scrn);
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(scrn);
	int num_crtcs_on = 0;
	int i;
	ARMSOC_PRESENT_DBG_MSG("armsoc_present_check_flip");

/*
	if (!ms->drmmode.pageflip)
		return FALSE;

	if (ms->drmmode.dri2_flipping)
		return FALSE;
*/
	if (!scrn->vtSema)
		return FALSE;

	for (i = 0; i < config->num_crtc; i++) {
		struct drmmode_crtc_private_rec * drmmode_crtc = config->crtc[i]->driver_private;

		if (armsoc_crtc_on(config->crtc[i]))
			num_crtcs_on++;
	}

	/* We can't do pageflipping if all the CRTCs are off. */
	if (num_crtcs_on == 0)
		return FALSE;

	/* Check stride, can't change that on flip */
/*	if (pixmap->devKind != drmmode_bo_get_pitch(&ms->drmmode.front_bo))
		return FALSE;
*/
	/* Make sure there's a bo we can get to */
	/* XXX: actually do this.  also...is it sufficient?
	 * if (!glamor_get_pixmap_private(pixmap))
	 *     return FALSE;
	 */

	return TRUE;
}

/*
 * Queue a flip on 'crtc' to 'pixmap' at 'target_msc'. If 'sync_flip' is true,
 * then wait for vblank. Otherwise, flip immediately
 */
static Bool
armsoc_present_flip(RRCrtcPtr crtc,
                    uint64_t event_id,
                    uint64_t target_msc,
                    PixmapPtr pixmap,
                    Bool sync_flip)
{
	ScreenPtr screen = crtc->pScreen;
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
//    modesettingPtr ms = modesettingPTR(scrn);
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(scrn);
	xf86CrtcPtr xf86_crtc = crtc->devPrivate;
	struct drmmode_crtc_private_rec * drmmode_crtc = xf86_crtc->driver_private;
	Bool ret;
	struct armsoc_present_vblank_event *event;
	ARMSOC_PRESENT_DBG_MSG("armsoc_present_flip");

	if (!armsoc_present_check_flip(crtc, screen->root, pixmap, sync_flip))
		return FALSE;

	event = calloc(1, sizeof(struct armsoc_present_vblank_event));
	if (!event)
		return FALSE;

	ARMSOC_PRESENT_DBG_MSG("\t\tms:pf %lld msc %llu\n",
	              (long long) event_id, (long long) target_msc);

	event->event_id = event_id;
	event->unflip = FALSE;

ret = drmmode_page_flip(&pixmap->drawable, drmmode_crtc->drmmode->fb_id, NULL);

//	ret = drmmode_page_flip(screen, pixmap, event);
//    ret = ms_do_pageflip(screen, pixmap, event, drmmode_crtc->vblank_pipe, !sync_flip,
//                         armsoc_present_flip_handler, armsoc_present_flip_abort);
	if (!ret)
		xf86DrvMsg(scrn->scrnIndex, X_ERROR, "present flip failed\n");
//	else
//		ms->drmmode.present_flipping = TRUE;

	return ret;
}

/*
 * Queue a flip back to the normal frame buffer
 */
static void
armsoc_present_unflip(ScreenPtr screen, uint64_t event_id)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
//    modesettingPtr ms = modesettingPTR(scrn);
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(scrn);
	PixmapPtr pixmap = screen->GetScreenPixmap(screen);
	xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(scrn);
	int i;
	struct armsoc_present_vblank_event *event;

	event = calloc(1, sizeof(struct armsoc_present_vblank_event));
	if (!event)
		return;

	event->event_id = event_id;
	event->unflip = TRUE;

	if (armsoc_present_check_flip(NULL, screen->root, pixmap, TRUE) &&
	        drmmode_page_flip(screen, pixmap, event)
//        ms_do_pageflip(screen, pixmap, event, -1, FALSE,
//                       armsoc_present_flip_handler, armsoc_present_flip_abort)
	   ) {
		return;
	}

	for (i = 0; i < config->num_crtc; i++) {
		xf86CrtcPtr crtc = config->crtc[i];
		struct drmmode_crtc_private_rec * drmmode_crtc = crtc->driver_private;

		if (!crtc->enabled)
			continue;

		/* info->drmmode.fb_id still points to the FB for the last flipped BO.
		 * Clear it, drmmode_set_mode_major will re-create it
		 */
		if (drmmode_crtc->drmmode->fb_id) {
			drmModeRmFB(drmmode_crtc->drmmode->fd,
			            drmmode_crtc->drmmode->fb_id);
			drmmode_crtc->drmmode->fb_id = 0;
		}

		if (drmmode_crtc->dpms_mode == DPMSModeOn)
			crtc->funcs->set_mode_major(crtc, &crtc->mode, crtc->rotation,
			                            crtc->x, crtc->y);
//		else
//			drmmode_crtc->need_modeset = TRUE;
	}

	present_event_notify(event_id, 0, 0);
//	ms->drmmode.present_flipping = FALSE;
}
#endif

static present_screen_info_rec armsoc_present_screen_info = {
	.version = PRESENT_SCREEN_INFO_VERSION,

	.get_crtc = armsoc_present_get_crtc,
	.get_ust_msc = armsoc_present_get_ust_msc,
	.queue_vblank = armsoc_present_queue_vblank,
	.abort_vblank = armsoc_present_abort_vblank,
	.flush = armsoc_present_flush,

	.capabilities = PresentCapabilityNone,
#ifdef ARMSOC_PRESENT_FLIP
	.check_flip = armsoc_present_check_flip,
	.flip = armsoc_present_flip,
	.unflip = armsoc_present_unflip,
#endif
};


/*
 * General DRM kernel handler. Looks for the matching sequence number in the
 * drm event queue and calls the handler for it.
 */
static void
armsoc_drm_handler(int fd, uint32_t frame, uint32_t sec, uint32_t usec,
                   void *user_ptr)
{
	struct armsoc_drm_queue *q, *tmp;
	uint32_t user_data = (uint32_t) (intptr_t) user_ptr;

	xorg_list_for_each_entry_safe(q, tmp, &armsoc_drm_queue, list) {
		if (q->seq == user_data) {
			uint64_t msc;

			msc = armsoc_kernel_msc_to_crtc_msc(q->crtc, frame);
			xorg_list_del(&q->list);
			q->handler(msc, (uint64_t) sec * 1000000 + usec, q->data);
			free(q);
			break;
		}
	}
}


Bool
armsoc_present_screen_init(ScreenPtr screen)
{
	ScrnInfoPtr scrn = xf86ScreenToScrn(screen);
	struct ARMSOCRec * pARMSOC = ARMSOCPTR(scrn);
	uint64_t value;
	int ret;

	xorg_list_init(&armsoc_drm_queue);

	event_context.version = DRM_EVENT_CONTEXT_VERSION;
	event_context.vblank_handler = armsoc_drm_handler;
	event_context.page_flip_handler = armsoc_drm_handler;

	ret = drmGetCap(pARMSOC->drmFD, DRM_CAP_ASYNC_PAGE_FLIP, &value);
	if (ret == 0 && value == 1)
		armsoc_present_screen_info.capabilities |= PresentCapabilityAsync;

	return present_screen_init(screen, &armsoc_present_screen_info);
}

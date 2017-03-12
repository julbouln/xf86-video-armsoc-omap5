/*
 * Copyright © 2013 ARM Limited.
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
 */

#include "../drmmode_driver.h"
#include <stddef.h>
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <sys/ioctl.h>

/* taken from libdrm */

union omap_gem_size {
	uint32_t bytes;		/* (for non-tiled formats) */
	struct {
		uint16_t width;
		uint16_t height;
	} tiled;		/* (for tiled formats) */
};

struct drm_omap_gem_new {
	union omap_gem_size size;	/* in */
	uint32_t flags;			/* in */
	uint32_t handle;		/* out */
	uint32_t __pad;
};

#define DRM_OMAP_GEM_NEW		0x03
#define DRM_IOCTL_OMAP_GEM_NEW		DRM_IOWR(DRM_COMMAND_BASE + DRM_OMAP_GEM_NEW, struct drm_omap_gem_new)

#define OMAP_BO_SCANOUT		0x00000001	/* scanout capable (phys contiguous) */
#define OMAP_BO_CACHE_MASK	0x00000006	/* cache type mask, see cache modes */
#define OMAP_BO_TILED_MASK	0x00000f00	/* tiled mapping mask, see tiled modes */

/* cache modes */
#define OMAP_BO_CACHED		0x00000000	/* default */
#define OMAP_BO_WC		0x00000002	/* write-combine */
#define OMAP_BO_UNCACHED	0x00000004	/* strongly-ordered (uncached) */

/* tiled modes */
#define OMAP_BO_TILED_8		0x00000100
#define OMAP_BO_TILED_16	0x00000200
#define OMAP_BO_TILED_32	0x00000300
#define OMAP_BO_TILED		(OMAP_BO_TILED_8 | OMAP_BO_TILED_16 | OMAP_BO_TILED_32)

/* Cursor dimensions
 * Technically we probably don't have any size limit.. since we
 * are just using an overlay... but xserver will always create
 * cursor images in the max size, so don't use width/height values
 * that are too big
 */
/* width */
#define CURSORW   (64)
/* height */
#define CURSORH   (64)
/* Padding added down each side of cursor image */
#define CURSORPAD (0)

#define ALIGN(val, align)	(((val) + (align) - 1) & ~((align) - 1))

static int init_plane_for_cursor(int drm_fd, uint32_t plane_id) {
	int res = -1;
	drmModeObjectPropertiesPtr props;
	props = drmModeObjectGetProperties(drm_fd, plane_id,
	                                   DRM_MODE_OBJECT_PLANE);

	if (props) {
		int i;

		for (i = 0; i < props->count_props; i++) {
			drmModePropertyPtr this_prop;
			this_prop = drmModeGetProperty(drm_fd, props->props[i]);

			if (this_prop) {
				if (!strncmp(this_prop->name, "zpos",
				             DRM_PROP_NAME_LEN)) {
					res = drmModeObjectSetProperty(drm_fd,
					                               plane_id,
					                               DRM_MODE_OBJECT_PLANE,
					                               this_prop->prop_id,
					                               1);
					drmModeFreeProperty(this_prop);
					break;
				}
				drmModeFreeProperty(this_prop);
			}

		}
		drmModeFreeObjectProperties(props);
	}

	return res;
}

static int create_custom_gem(int fd, struct armsoc_create_gem *create_gem)
{
	int ret;
	unsigned int pitch;
	unsigned int flags = OMAP_BO_WC;
	uint32_t size;
	union omap_gem_size gsize;
	struct drm_omap_gem_new create_omap;

	/* 32 bytes pitch for OMAP = 16 bytes pitch for gc320 */
	pitch = ALIGN(create_gem->width * ((create_gem->bpp + 7) / 8), 32);

	if (create_gem->buf_type == ARMSOC_BO_SCANOUT)
		flags |= OMAP_BO_SCANOUT;

	size = create_gem->height * pitch;
	gsize.bytes = size;
	create_omap.size = gsize;
	create_omap.flags = flags;

	ret = drmIoctl(fd, DRM_IOCTL_OMAP_GEM_NEW, &create_omap);
	if (ret)
		return ret;

	/* Convert custom create_omap to generic create_gem */
	create_gem->handle = create_omap.handle;
	create_gem->pitch = pitch;
	create_gem->size = create_omap.size.bytes;

	return 0;
}

struct drmmode_interface omap_interface = {
	"omapdrm"	      /* name of drm driver*/,
	1                     /* use_page_flip_events */,
	1                     /* use_early_display */,
	CURSORW               /* cursor width */,
	CURSORH               /* cursor_height */,
	CURSORPAD             /* cursor padding */,
	HWCURSOR_API_PLANE /* cursor_api */,
	init_plane_for_cursor /* init_plane_for_cursor */,
	0                     /* vblank_query_supported */,
	create_custom_gem     /* create_custom_gem */,
};

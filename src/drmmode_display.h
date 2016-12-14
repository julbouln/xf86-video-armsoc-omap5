#ifndef DRMMODE_DISPLAY_H
#define DRMMODE_DISPLAY_H
#include <xf86drmMode.h>

struct drmmode_cursor_rec {
	/* hardware cursor: */
	struct armsoc_bo *bo;
	int x, y;
	 /* These are used for HWCURSOR_API_PLANE */
	drmModePlane *ovr;
	uint32_t fb_id;
	/* This is used for HWCURSOR_API_STANDARD */
	uint32_t handle;
};

struct drmmode_rec {
	int fd;
	drmModeResPtr mode_res;
	int cpp;
	struct udev_monitor *uevent_monitor;
	InputHandlerProc uevent_handler;
	struct drmmode_cursor_rec *cursor;
	uint32_t fb_id;
};

struct drmmode_crtc_private_rec {
	struct drmmode_rec *drmmode;
	uint32_t crtc_id;
	int cursor_visible;
	/* settings retained on last good modeset */
	int last_good_x;
	int last_good_y;
	Rotation last_good_rotation;
	DisplayModePtr last_good_mode;

	int dpms_mode;
	uint32_t vblank_pipe;

    /**
     * @{ MSC (vblank count) handling for the PRESENT extension.
     *
     * The kernel's vblank counters are 32 bits and apparently full of
     * lies, and we need to give a reliable 64-bit msc for GL, so we
     * have to track and convert to a userland-tracked 64-bit msc.
     */
    int32_t vblank_offset;
    uint32_t msc_prev;
    uint64_t msc_high;
/** @} */

	/* properties that we care about: */
	uint32_t prop_rotation;
};

struct drmmode_prop_rec {
	drmModePropertyPtr mode_prop;
	/* Index within the kernel-side property arrays for this connector. */
	int index;
	/* if range prop, num_atoms == 1;
	 * if enum prop, num_atoms == num_enums + 1
	 */
	int num_atoms;
	Atom *atoms;
};

struct drmmode_output_priv {
	struct drmmode_rec *drmmode;
	int output_id;
	drmModeConnectorPtr connector;
	drmModeEncoderPtr *encoders;
	drmModePropertyBlobPtr edid_blob;
	int num_props;
	struct drmmode_prop_rec *props;
	int enc_mask;   /* encoders present (mask of encoder indices) */
	int enc_clones; /* encoder clones possible (mask of encoder indices) */
};


#endif
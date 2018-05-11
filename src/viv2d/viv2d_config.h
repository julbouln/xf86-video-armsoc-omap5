// Global
#define VIV2D_STREAM_SIZE 1024*4
#define VIV2D_MAX_RECTS 256
#define VIV2D_MAX_TMP_PIX 256
#define VIV2D_PITCH_ALIGN 32

#define VIV2D_CACHE_BO 1
#define VIV2D_CACHE_SIZE 1024*2
#define VIV2D_CACHE_MAX 1024*1024*128

// EXA config
#define VIV2D_MARKER 1
#define VIV2D_PIXMAP 1
#define VIV2D_ACCESS 1

#define VIV2D_SOLID 1
#define VIV2D_COPY 1
#define VIV2D_COMPOSITE 1
#define VIV2D_PUT_TEXTURE_IMAGE 1

#define VIV2D_UPLOAD_TO_SCREEN 1
#define VIV2D_DOWNLOAD_FROM_SCREEN 1

#define VIV2D_MASK_SUPPORT 1 // support mask
#define VIV2D_SOLID_PICTURE 1 // support solid clear picture
#define VIV2D_REPEAT 1 // support repeat
#define VIV2D_REPEAT_WITH_MASK 1 // support repeat with mask
//#define VIV2D_1X1_REPEAT_AS_SOLID 1 // use solid clear instead of stretch for 1x1 repeat
#define VIV2D_SUPPORT_A8_SRC 1
#define VIV2D_SUPPORT_A8_MASK 1

//#define VIV2D_NON_RGBA_DEST_FIX 1
//#define VIV2D_CPU_BO_CLEAR 1

//#define VIV2D_SOFT_A8_DST 1

//#define VIV2D_SUPPORT_A8_DST 1
//#define VIV2D_SUPPORT_MONO 1

// WIP
//#define VIV2D_USERPTR 1

// CPU only for surface < VIV2D_MIN_SIZE
//#define VIV2D_MIN_SIZE 0
#define VIV2D_MIN_SIZE 1024 // > 16x16 32bpp
//#define VIV2D_MIN_SIZE 1024*4 // > 32x32 32bpp
//#define VIV2D_MIN_SIZE 1024*16 // > 64x64 32bpp

//#define VIV2D_SIZE_CONSTRAINTS 1
//#define VIV2D_MIN_HW_HEIGHT 64
//#define VIV2D_MIN_HW_SIZE_24BIT (256 * 256)
//#define VIV2D_MIN_HW_SIZE_24BIT (4096)

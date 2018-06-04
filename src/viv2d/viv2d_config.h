// Global
#define VIV2D_STREAM_SIZE 1024*32
#define VIV2D_MAX_RECTS 256
#define VIV2D_PITCH_ALIGN 32

// EXA config
#define VIV2D_MARKER 1
#define VIV2D_PIXMAP 1
#define VIV2D_ACCESS 1

#define VIV2D_SOLID 1
#define VIV2D_COPY 1
#define VIV2D_COMPOSITE 1
#define VIV2D_PUT_TEXTURE_IMAGE 1

#define VIV2D_MASK_SUPPORT 1 // support mask
#define VIV2D_REPEAT 1 // support repeat
#define VIV2D_REPEAT_WITH_MASK 1 // support repeat with mask
#define VIV2D_1X1_REPEAT_AS_SOLID 1 // use solid clear instead of stretch for 1x1 repeat
#define VIV2D_SOLID_PICTURE_SRC 1 // support solid clear picture
#define VIV2D_SUPPORT_A8_SRC 1
#define VIV2D_SOLID_PICTURE_MSK 1 // support solid clear picture
#define VIV2D_SUPPORT_A8_MASK 1

// EXPERIMENTAL
#define VIV2D_PREPARE_SET_FORMAT 1
//#define VIV2D_FILL_BRUSH 1
//#define VIV2D_SUPPORT_A8_DST 1
//#define VIV2D_SUPPORT_MONO 1
//#define VIV2D_UPLOAD_TO_SCREEN 1
//#define VIV2D_DOWNLOAD_FROM_SCREEN 1
//#define VIV2D_USERPTR 1
//#define VIV2D_COPY_BLEND 1
#define VIV2D_MASK_COMPONENT_SUPPORT 1
#define VIV2D_FLUSH_CALLBACK 1
#define VIV2D_CACHE_FLUSH_OPS 1
#define VIV2D_EXA_HACK 1

// CPU only for surface < VIV2D_MIN_SIZE and > VIV2D_MAX_SIZE
#define VIV2D_MAX_SIZE 2048*2048*4 // 16Mbytes
#define VIV2D_MIN_SIZE 0 // best result because less cpu-gpu exchange
//#define VIV2D_MIN_SIZE 1024 // > 16x16 32bpp
//#define VIV2D_MIN_SIZE 1024*4 // > 32x32 32bpp
//#define VIV2D_MIN_SIZE 1024*16 // > 64x64 32bpp

//#define VIV2D_UNSUPPORTED 1
//#define VIV2D_DEBUG 1
//#define VIV2D_OP_DEBUG 1
//#define VIV2D_TRACE 1

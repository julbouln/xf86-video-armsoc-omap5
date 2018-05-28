#include <stdint.h>

#include "etnaviv_drmif.h"
#include "etnaviv_drm.h"

#include "queue.h"

#define ETNA_PIPE_BOS_SIZE 1024*4 // max pipe bos

#define ETNA_BO_CACHE_SIZE 1024*1024*512 // 256 Mbytes
#define ETNA_BO_CACHE_MAX_SIZE 1024*1024*512
#define ETNA_BO_CACHE_GC_SIZE 1024*1024*64
#define ETNA_BO_CACHE_MAX_BOS_PER_BUCKET 2048
#define ETNA_BO_CACHE_MAX_SIZE_PER_BUCKET 1024*1024*256

//#define ETNA_BO_CACHE_PROFILE 1
//#define ETNA_BO_CACHE_DEBUG 1

#define ETNA_BO_CACHE_PAGE_SIZE 4096
#define ETNA_BO_CACHE_BUCKETS_COUNT 4096 // all possibles buffers betweek 4k and 16M
#define ETNA_BO_CACHE_BUCKET_FROM_SIZE(size) ((size) >> 12) - 1

struct etna_bo_cache_bucket {
	uint32_t idx;
	int dirty;
	queue_t *unused_bos;
	queue_t *free_bos;
};

struct etna_bo_cache {
	struct etna_bo_cache_bucket buckets[ETNA_BO_CACHE_BUCKETS_COUNT];
	queue_t *dirty_buckets;
	int dirty;
	size_t size;
	queue_t *usermem_bos;	
};

struct etna_device {
	int fd;
	struct etna_bo_cache *cache;
};

struct etna_bo {
	struct etna_device      *dev;
	void            *map;           /* userspace mmap'ing (if there is one) */
	uint32_t        size;
	uint32_t        handle;
	uint32_t        flags;
	uint32_t        name;           /* flink global handle (DRI2 name) */
	uint64_t        offset;         /* offset to mmap() */

	struct etna_cmd_stream *current_stream;
	uint32_t idx;
	uint32_t state;
};

struct etna_gpu {
	struct etna_device *dev;
	uint32_t core;
	uint32_t model;
	uint32_t revision;
};

struct etna_pipe {
	enum etna_pipe_id id;
	struct etna_gpu *gpu;
	struct etna_bo *bos[ETNA_PIPE_BOS_SIZE];
	uint32_t nr_bos;
};

struct etna_cmd_stream_priv {
	struct etna_cmd_stream base;
	struct etna_pipe *pipe;

	uint32_t last_timestamp;

	/* submit ioctl related tables: */
	struct {
		/* bo's table: */
		struct drm_etnaviv_gem_submit_bo *bos;
		uint32_t nr_bos, max_bos;

		/* reloc's table: */
		struct drm_etnaviv_gem_submit_reloc *relocs;
		uint32_t nr_relocs, max_relocs;
	} submit;

	/* should have matching entries in submit.bos: */
	struct etna_bo **bos;
	uint32_t nr_bos, max_bos;

};


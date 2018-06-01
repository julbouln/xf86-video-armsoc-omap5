#ifndef ETNAVIV_EXTRA_H_
#define ETNAVIV_EXTRA_H_

#include "queue.h"

//#define ETNAVIV_CUSTOM 1

#define ETNA_BO_CACHE_SIZE 1024*1024*256 // 256 Mbytes
#define ETNA_BO_CACHE_MAX_SIZE 1024*1024*256
#define ETNA_BO_CACHE_GC_SIZE 1024*1024*128
#define ETNA_BO_CACHE_MAX_BOS_PER_BUCKET 1024*4
#define ETNA_BO_CACHE_MAX_SIZE_PER_BUCKET 1024*1024*128

//#define ETNA_BO_CACHE_PROFILE 1
//#define ETNA_DEBUG 1
//#define ETNA_BO_CACHE_DEBUG 1

#define ETNA_BO_CACHE_PAGE_SIZE 4096
#define ETNA_BO_CACHE_BUCKETS_COUNT 4096 // all possibles buffers betweek 4k and 16M
#define ETNA_BO_CACHE_BUCKET_FROM_SIZE(size) ((size) >> 12) - 1

#ifdef ETNAVIV_CUSTOM

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
#endif

// cache
void etna_bo_cache_destroy(struct etna_device *dev);
void etna_bo_cache_init(struct etna_device *dev);
struct etna_bo *etna_bo_cache_new(struct etna_device *dev, size_t size);
void etna_bo_cache_del(struct etna_device *dev, struct etna_bo *bo);
void etna_bo_cache_clean(struct etna_device *dev);
void etna_bo_cache_usermem_del(struct etna_device *dev, struct etna_bo *bo);
// extra

void etna_nop(struct etna_cmd_stream *stream);
int etna_bo_ready(struct etna_bo *bo);
int etna_bo_wait(struct etna_device *dev, struct etna_pipe *pipe, struct etna_bo *bo, uint64_t ns);
struct etna_bo *etna_bo_from_usermem_prot(struct etna_device *dev, void *memory, size_t size, int flags);

#endif
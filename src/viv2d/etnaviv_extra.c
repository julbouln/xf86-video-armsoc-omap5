#include <assert.h>

#include <stdlib.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <xorg-server.h>
#include <xf86.h>

#include "etnaviv_drmif.h"
#include "etnaviv_drm.h"

#include "etnaviv_extra.h"

#ifdef ETNAVIV_CUSTOM
#include "etnaviv.h"
#else
#include "etnaviv_priv.h"
static queue_t *unused_bos;
#endif

#define ALIGN(v,a) (((v) + (a) - 1) & ~((a) - 1))

#define INFO_MSG(fmt, ...) \
		do { xf86Msg(X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)

#ifdef ETNA_BO_CACHE_DEBUG
#define CACHE_DEBUG_MSG(fmt, ...) \
		do { xf86Msg(X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
#else
#define CACHE_DEBUG_MSG(fmt, ...)
#endif

static inline void get_abs_timeout(struct drm_etnaviv_timespec *tv, uint64_t ns)
{
	struct timespec t;
	uint32_t s = ns / 1000000000;
	clock_gettime(CLOCK_MONOTONIC, &t);
	tv->tv_sec = t.tv_sec + s;
	tv->tv_nsec = t.tv_nsec + ns - (s * 1000000000);
}


// cache

static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef ETNA_BO_CACHE_PROFILE
static uint64_t prof_bucket;
static uint64_t prof_alloc;
static uint64_t prof_new;
static uint64_t prof_recycle;
static uint64_t prof_reuse;
#endif

void etna_bo_cache_usermem_del(struct etna_device *dev, struct etna_bo *bo) {
#ifdef ETNAVIV_CUSTOM
	pthread_mutex_lock(&cache_lock);
	queue_push_tail(dev->cache->usermem_bos, bo);
	pthread_mutex_unlock(&cache_lock);
#else
//	queue_push_tail(unused_bos, bo);
#endif
}

void etna_bo_cache_init(struct etna_device *dev) {
#ifdef ETNAVIV_CUSTOM
#ifdef ETNA_BO_CACHE_PROFILE
	prof_bucket = 0;
	prof_alloc = 0;
	prof_new = 0;
	prof_recycle = 0;
	prof_reuse = 0;
#endif

	dev->cache = calloc(sizeof(struct etna_bo_cache), 1);
	dev->cache->size = 0;
	dev->cache->dirty_buckets = queue_new();
	for (int i = 0; i < ETNA_BO_CACHE_BUCKETS_COUNT; ++i) {
		dev->cache->buckets[i].dirty = 0;
		dev->cache->buckets[i].unused_bos = queue_new();
		dev->cache->buckets[i].free_bos = queue_new();
	}
	dev->cache->usermem_bos = queue_new();
#else
	unused_bos = queue_new();
#endif
}

struct etna_bo *etna_bo_cache_new(struct etna_device *dev, size_t size, int flags) {
#ifdef ETNAVIV_CUSTOM
	struct etna_bo_cache *cache = dev->cache;
	size_t aligned_size = ALIGN(size, ETNA_BO_CACHE_PAGE_SIZE);

	uint16_t bucket_size = ETNA_BO_CACHE_BUCKET_FROM_SIZE(aligned_size);
	struct etna_bo_cache_bucket *bucket = &cache->buckets[bucket_size];

#ifdef ETNA_BO_CACHE_PROFILE
	prof_alloc++;
#endif

	if (cache->size > ETNA_BO_CACHE_MAX_SIZE) {
		etna_bo_cache_clean(dev);
	}

	pthread_mutex_lock(&cache_lock);

	if (!queue_is_empty(bucket->free_bos)) {
		struct etna_bo *bo = queue_pop_head(bucket->free_bos);

		CACHE_DEBUG_MSG("etna_cache_bo_new: reuse bo:%p bo_size:%d cache_size:%d", bo, size, cache->size);

#ifdef ETNA_BO_CACHE_PROFILE
		prof_reuse++;
#endif
		pthread_mutex_unlock(&cache_lock);
		return bo;
	} else {
		struct etna_bo *bo = etna_bo_new(dev, aligned_size, ETNA_BO_WC);
		CACHE_DEBUG_MSG("etna_cache_bo_new: new bo:%p bo_size:%d cache_size:%d", bo, size, cache->size);

#ifdef ETNA_BO_CACHE_PROFILE
		prof_new++;
#endif
		cache->size += aligned_size;
		pthread_mutex_unlock(&cache_lock);
		return bo;
	}

	pthread_mutex_unlock(&cache_lock);

	return NULL;
#else
	struct etna_bo *bo;
	bo = etna_bo_new(dev, size, flags);
	CACHE_DEBUG_MSG("etna_bo_new: new bo:%p", bo);
	return bo;
#endif
};

void etna_bo_cache_del(struct etna_device *dev, struct etna_bo *bo) {
#ifdef ETNAVIV_CUSTOM
	struct etna_bo_cache *cache = dev->cache;
	size_t aligned_bo_size = ALIGN(bo->size, ETNA_BO_CACHE_PAGE_SIZE);
	uint16_t bucket_size = ETNA_BO_CACHE_BUCKET_FROM_SIZE(aligned_bo_size);
	struct etna_bo_cache_bucket *bucket = &cache->buckets[bucket_size];
	pthread_mutex_lock(&cache_lock);
	queue_push_tail(bucket->unused_bos, bo);
	if (!bucket->dirty) {
		queue_push_tail(cache->dirty_buckets, bucket);
		bucket->dirty = 1;
	}
	pthread_mutex_unlock(&cache_lock);
#else
	queue_push_tail(unused_bos, bo);
#endif
}

#ifdef ETNAVIV_CUSTOM
int etna_bo_cache_clean_bucket(struct etna_device *dev, struct etna_bo_cache_bucket *bucket) {
	struct etna_bo_cache *cache = dev->cache;
	bucket->dirty = 0;
	uint32_t qsize = queue_size(bucket->unused_bos);
	for (int i = 0; i < qsize; ++i) {
		struct etna_bo *unused_bo = queue_peek_head(bucket->unused_bos);
		if (unused_bo->state == ETNA_BO_READY) { // bos are really free when they are ready
			CACHE_DEBUG_MSG("etna_bo_cache_clean_bucket: remove bo:%p bo_size:%d", unused_bo, unused_bo->size);
			unused_bo = queue_pop_head(bucket->unused_bos);
			queue_push_tail(bucket->free_bos, unused_bo);
		} else {
			// bucket is still dirty
			bucket->dirty = 1;
		}
	}
	return bucket->dirty;
}

void etna_bo_cache_recycle_bucket(struct etna_device *dev, struct etna_bo_cache_bucket *bucket) {
	struct etna_bo_cache *cache = dev->cache;
	while (!queue_is_empty(bucket->free_bos)) {
		struct etna_bo *free_bo = queue_pop_head(bucket->free_bos);
#ifdef ETNA_BO_CACHE_PROFILE
		prof_recycle++;
#endif
		cache->size -= ALIGN(free_bo->size, ETNA_BO_CACHE_PAGE_SIZE);
		CACHE_DEBUG_MSG("etna_cache_bo_del: del bo:%p bo_size:%d cache_size:%d", free_bo, free_bo->size, cache->size);
		etna_bo_del(free_bo);
	}
}
#endif

void etna_bo_cache_clean(struct etna_device *dev) {
#ifdef ETNAVIV_CUSTOM
	struct etna_bo_cache *cache = dev->cache;
	pthread_mutex_lock(&cache_lock);

	uint32_t dsize = queue_size(cache->dirty_buckets);

	for (int i = 0; i < dsize; i++) {
		struct etna_bo_cache_bucket *bucket = queue_pop_head(cache->dirty_buckets);
		CACHE_DEBUG_MSG("etna_bo_cache_clean: clean dirty_bucket_size:%d free_size:%d unused_size:%d", dsize, queue_size(bucket->free_bos), queue_size(bucket->unused_bos));

		if (etna_bo_cache_clean_bucket(dev, bucket)) {
			queue_push_tail(cache->dirty_buckets, bucket);
		}
		if (queue_size(bucket->free_bos) * ETNA_BO_CACHE_PAGE_SIZE * (i + 1) > ETNA_BO_CACHE_MAX_SIZE_PER_BUCKET) {
//				if (queue_size(bucket->free_bos) > ETNA_BO_CACHE_MAX_BOS_PER_BUCKET) {
			CACHE_DEBUG_MSG("etna_bo_cache_clean: recycle free_size:%d bucket:%d", queue_size(bucket->free_bos), i);
			etna_bo_cache_recycle_bucket(dev, bucket);
		}
	}

	if (cache->size > ETNA_BO_CACHE_SIZE) {
		CACHE_DEBUG_MSG("etna_bo_cache_clean: recycle size:%d", cache->size);
		// clean largest buckets first
		for (int i = ETNA_BO_CACHE_BUCKETS_COUNT - 1; i >= 0; --i) {
			if (cache->size > ETNA_BO_CACHE_GC_SIZE) {
				etna_bo_cache_recycle_bucket(dev, &cache->buckets[i]);
			} else {
				break;
			}
		}
	}

	while (!queue_is_empty(cache->usermem_bos)) {
		struct etna_bo *bo = queue_peek_head(cache->usermem_bos);
		if (bo->state == ETNA_BO_READY) { // bos are really free when they are ready
			bo = queue_pop_head(cache->usermem_bos);
			CACHE_DEBUG_MSG("etna_bo_cache_clean: delete usermem bo:%p", bo);
			etna_bo_del(bo);
		}
	}

	pthread_mutex_unlock(&cache_lock);
#else
	uint32_t qsize = queue_size(unused_bos);

	for (int i = 0; i < qsize; ++i) {
		struct etna_bo *unused_bo = queue_peek_head(unused_bos);

		if (!unused_bo->current_stream) {
			unused_bo = queue_pop_head(unused_bos);
			CACHE_DEBUG_MSG("etna_bo_del: del bo:%p", unused_bo);
			etna_bo_del(unused_bo);
		}
	}

#endif
}

void etna_bo_cache_destroy(struct etna_device *dev) {
#ifdef ETNAVIV_CUSTOM
	for (int i = 0; i < ETNA_BO_CACHE_BUCKETS_COUNT; ++i) {
		struct etna_bo_cache_bucket *bucket = &dev->cache->buckets[i];
		etna_bo_cache_clean_bucket(dev, bucket);
		etna_bo_cache_recycle_bucket(dev, bucket);
		queue_free(dev->cache->buckets[i].unused_bos);
		queue_free(dev->cache->buckets[i].free_bos);
	}
	queue_free(dev->cache->usermem_bos);
	queue_free(dev->cache->dirty_buckets);
#else
	etna_bo_cache_clean(dev);
	queue_free(unused_bos);
#endif
}


/* extra */

void etna_nop(struct etna_cmd_stream *stream) {
	etna_cmd_stream_emit(stream, 0x18000000);
}

void etna_bo_unmap(struct etna_bo *bo) {
	if (bo->map) {
		munmap(bo->map, bo->size);
		bo->map = NULL;
	}

}

int etna_bo_ready(struct etna_bo *bo) {
#ifdef ETNAVIV_CUSTOM
	return (bo->state == ETNA_BO_READY);
#else
	return (bo->current_stream == NULL);
#endif
}

int etna_bo_wait(struct etna_device *dev, struct etna_pipe *pipe, struct etna_bo *bo, uint64_t ns) {
#ifdef ETNAVIV_CUSTOM
	int err;
	struct drm_etnaviv_gem_wait req = {
		.pipe = pipe->gpu->core,
		.handle = bo->handle,
		.flags = 0,
		.pad = 0,
		.timeout = 0
	};

	get_abs_timeout(&req.timeout, ns);

	err = drmCommandWrite(dev->fd, DRM_ETNAVIV_GEM_WAIT,
	                      &req, sizeof(req));
	return err;
#else
	return 1;
#endif
}

struct etna_bo *etna_bo_from_usermem_prot(struct etna_device *dev, void *memory, size_t size, int flags) {
#ifdef ETNAVIV_CUSTOM
	struct drm_etnaviv_gem_userptr req = {
		.user_ptr = (uintptr_t)memory,
		.user_size = size,
		.flags = flags,
		.handle = 0
	};

	int err = drmCommandWriteRead(dev->fd, DRM_ETNAVIV_GEM_USERPTR, &req,
	                              sizeof(req));
	if (err) {
		INFO_MSG("etna_bo_from_usermem_prot fail: %d", err);
		return NULL;
	}
	else {
		struct etna_bo *usr_bo = NULL;
		usr_bo = bo_from_handle(dev, size, req.handle, flags);
//		usr_bo->map = memory;

		INFO_MSG("etna_bo_from_usermem_prot success : mem:%p bo:%p handle:%d size:%d", memory, usr_bo, req.handle, size);
		return usr_bo;
	}
#else
	struct drm_etnaviv_gem_userptr req = {
		.user_ptr = (uintptr_t)memory,
		.user_size = size,
		.flags = flags,
		.handle = 0
	};

	int err = drmCommandWriteRead(dev->fd, DRM_ETNAVIV_GEM_USERPTR, &req,
	                              sizeof(req));
	if (err) {
		INFO_MSG("etna_bo_from_usermem_prot fail: %d", err);
		return NULL;
	}
	else {
		struct etna_bo *usr_bo = NULL;
		usr_bo = etna_bo_from_handle(dev, req.handle, size);
//		usr_bo->map = memory;

		INFO_MSG("etna_bo_from_usermem_prot success : mem:%p bo:%p handle:%d size:%d", memory, usr_bo, req.handle, size);
		return usr_bo;
	}
#endif
}


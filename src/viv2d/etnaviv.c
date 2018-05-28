/*
simplified etnaviv drm based on libdrm
*/

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

#include "etnaviv.h"
#include "etnaviv_drmif.h"

#define ALIGN(v,a) (((v) + (a) - 1) & ~((a) - 1))

#define INFO_MSG(fmt, ...) \
		do { xf86Msg(X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define DEBUG_MSG(fmt, ...) \
		do { xf86Msg(X_INFO, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define WARN_MSG(fmt, ...) \
		do { xf86Msg(X_WARNING, fmt "\n",\
				##__VA_ARGS__); } while (0)
#define ERROR_MSG(fmt, ...) \
		do { xf86Msg(X_ERROR, fmt "\n",\
				##__VA_ARGS__); } while (0)

#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

// cache

static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef ETNA_BO_CACHE_PROFILE
static uint64_t prof_bucket;
static uint64_t prof_alloc;
static uint64_t prof_new;
static uint64_t prof_recycle;
static uint64_t prof_reuse;
#endif

void etna_bo_cache_clean(struct etna_device *dev);

void etna_bo_cache_init(struct etna_device *dev) {
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
}

void etna_bo_cache_usermem_del(struct etna_device *dev, struct etna_bo *bo) {
	queue_push_tail(dev->cache->usermem_bos, bo);
}

struct etna_bo *etna_bo_cache_new(struct etna_device *dev, size_t size) {
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

#ifdef ETNA_BO_CACHE_DEBUG
		INFO_MSG("etna_cache_bo_new: reuse bo:%p bo_size:%d cache_size:%d", bo, size, cache->size);
#endif
#ifdef ETNA_BO_CACHE_PROFILE
		prof_reuse++;
#endif
		pthread_mutex_unlock(&cache_lock);
		return bo;
	} else {
		struct etna_bo *bo = etna_bo_new(dev, aligned_size, ETNA_BO_UNCACHED);
#ifdef ETNA_BO_CACHE_DEBUG
		INFO_MSG("etna_cache_bo_new: new bo:%p bo_size:%d cache_size:%d", bo, size, cache->size);
#endif
#ifdef ETNA_BO_CACHE_PROFILE
		prof_new++;
#endif
		cache->size += aligned_size;
		pthread_mutex_unlock(&cache_lock);
		return bo;
	}

	pthread_mutex_unlock(&cache_lock);

	return NULL;
};

void etna_bo_cache_del(struct etna_device *dev, struct etna_bo *bo) {
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
}

int etna_bo_cache_clean_bucket(struct etna_device *dev, struct etna_bo_cache_bucket *bucket) {
	struct etna_bo_cache *cache = dev->cache;
	bucket->dirty = 0;
	uint32_t qsize = queue_size(bucket->unused_bos);
	for (int i = 0; i < qsize; ++i) {
		struct etna_bo *unused_bo = queue_peek_head(bucket->unused_bos);
		if (unused_bo->state == ETNA_BO_READY) { // bos are really free when they are ready
#ifdef ETNA_BO_CACHE_DEBUG
			INFO_MSG("etna_bo_cache_clean_bucket: remove bo:%p bo_size:%d", unused_bo, unused_bo->size);
#endif
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
#ifdef ETNA_BO_CACHE_DEBUG
		INFO_MSG("etna_cache_bo_del: del bo:%p bo_size:%d cache_size:%d", free_bo, free_bo->size, cache->size);
#endif
		etna_bo_del(free_bo);
	}
}

void etna_bo_cache_clean(struct etna_device *dev) {
	struct etna_bo_cache *cache = dev->cache;
	pthread_mutex_lock(&cache_lock);

	uint32_t dsize = queue_size(cache->dirty_buckets);

	for (int i = 0; i < dsize; i++) {
		struct etna_bo_cache_bucket *bucket = queue_pop_head(cache->dirty_buckets);
#ifdef ETNA_BO_CACHE_DEBUG
		INFO_MSG("etna_bo_cache_clean: clean free_size:%d unused_size:%d", queue_size(bucket->free_bos), queue_size(bucket->unused_bos));
#endif
		if (etna_bo_cache_clean_bucket(dev, bucket)) {
			queue_push_tail(cache->dirty_buckets, bucket);
		}
		if (queue_size(bucket->free_bos) * ETNA_BO_CACHE_PAGE_SIZE * (i + 1) > ETNA_BO_CACHE_MAX_SIZE_PER_BUCKET) {
//				if (queue_size(bucket->free_bos) > ETNA_BO_CACHE_MAX_BOS_PER_BUCKET) {
			INFO_MSG("etna_bo_cache_clean: recycle free_size:%d bucket:%d", queue_size(bucket->free_bos), i);
			etna_bo_cache_recycle_bucket(dev, bucket);
		}
	}

	if (cache->size > ETNA_BO_CACHE_SIZE) {
		INFO_MSG("etna_bo_cache_clean: recycle size:%d", cache->size);
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
			INFO_MSG("etna_bo_cache_clean: delete usermem bo:%p", bo);
			etna_bo_del(bo);
		}
	}

	pthread_mutex_unlock(&cache_lock);
}

void etna_bo_cache_destroy(struct etna_device *dev) {
	for (int i = 0; i < ETNA_BO_CACHE_BUCKETS_COUNT; ++i) {
		struct etna_bo_cache_bucket *bucket = &dev->cache->buckets[i];
		etna_bo_cache_clean_bucket(dev, bucket);
		etna_bo_cache_recycle_bucket(dev, bucket);
		queue_free(dev->cache->buckets[i].unused_bos);
		queue_free(dev->cache->buckets[i].free_bos);
	}
	queue_free(dev->cache->usermem_bos);
	queue_free(dev->cache->dirty_buckets);
}

// device

struct etna_device *etna_device_new(int fd)
{
	struct etna_device *dev = calloc(sizeof(*dev), 1);

	if (!dev)
		return NULL;

	dev->fd = fd;

	etna_bo_cache_init(dev);

	return dev;
}

void etna_device_del(struct etna_device *dev)
{
	etna_bo_cache_destroy(dev);
	free(dev);
}

static uint64_t get_param(struct etna_device *dev, uint32_t core, uint32_t param)
{
	struct drm_etnaviv_param req = {
		.pipe = core,
		.param = param,
	};
	int ret;

	ret = drmCommandWriteRead(dev->fd, DRM_ETNAVIV_GET_PARAM, &req, sizeof(req));
	if (ret) {
		ERROR_MSG("get-param (%x) failed! %d (%s)", param, ret, strerror(errno));
		return 0;
	}

	return req.value;
}

// gpu

struct etna_gpu *etna_gpu_new(struct etna_device *dev, unsigned int core)
{
	struct etna_gpu *gpu;

	gpu = calloc(1, sizeof(*gpu));
	if (!gpu) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	gpu->dev = dev;
	gpu->core = core;

	gpu->model    	= get_param(dev, core, ETNAVIV_PARAM_GPU_MODEL);
	gpu->revision 	= get_param(dev, core, ETNAVIV_PARAM_GPU_REVISION);

	if (!gpu->model)
		goto fail;

	INFO_MSG(" GPU model:          0x%x (rev %x)", gpu->model, gpu->revision);

	return gpu;
fail:
	if (gpu)
		etna_gpu_del(gpu);

	return NULL;
}

void etna_gpu_del(struct etna_gpu *gpu)
{
	free(gpu);
}

int etna_gpu_get_param(struct etna_gpu *gpu, enum etna_param_id param,
                       uint64_t *value)
{
	struct etna_device *dev = gpu->dev;
	unsigned int core = gpu->core;

	switch (param) {
	case ETNA_GPU_MODEL:
		*value = gpu->model;
		return 0;
	case ETNA_GPU_REVISION:
		*value = gpu->revision;
		return 0;
	case ETNA_GPU_FEATURES_0:
		*value = get_param(dev, core, ETNAVIV_PARAM_GPU_FEATURES_0);
		return 0;
	case ETNA_GPU_FEATURES_1:
		*value = get_param(dev, core, ETNAVIV_PARAM_GPU_FEATURES_1);
		return 0;
	case ETNA_GPU_FEATURES_2:
		*value = get_param(dev, core, ETNAVIV_PARAM_GPU_FEATURES_2);
		return 0;
	case ETNA_GPU_FEATURES_3:
		*value = get_param(dev, core, ETNAVIV_PARAM_GPU_FEATURES_3);
		return 0;
	case ETNA_GPU_FEATURES_4:
		*value = get_param(dev, core, ETNAVIV_PARAM_GPU_FEATURES_4);
		return 0;
	case ETNA_GPU_FEATURES_5:
		*value = get_param(dev, core, ETNAVIV_PARAM_GPU_FEATURES_5);
		return 0;
	case ETNA_GPU_FEATURES_6:
		*value = get_param(dev, core, ETNAVIV_PARAM_GPU_FEATURES_6);
		return 0;
	case ETNA_GPU_STREAM_COUNT:
		*value = get_param(dev, core, ETNA_GPU_STREAM_COUNT);
		return 0;
	case ETNA_GPU_REGISTER_MAX:
		*value = get_param(dev, core, ETNA_GPU_REGISTER_MAX);
		return 0;
	case ETNA_GPU_THREAD_COUNT:
		*value = get_param(dev, core, ETNA_GPU_THREAD_COUNT);
		return 0;
	case ETNA_GPU_VERTEX_CACHE_SIZE:
		*value = get_param(dev, core, ETNA_GPU_VERTEX_CACHE_SIZE);
		return 0;
	case ETNA_GPU_SHADER_CORE_COUNT:
		*value = get_param(dev, core, ETNA_GPU_SHADER_CORE_COUNT);
		return 0;
	case ETNA_GPU_PIXEL_PIPES:
		*value = get_param(dev, core, ETNA_GPU_PIXEL_PIPES);
		return 0;
	case ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE:
		*value = get_param(dev, core, ETNA_GPU_VERTEX_OUTPUT_BUFFER_SIZE);
		return 0;
	case ETNA_GPU_BUFFER_SIZE:
		*value = get_param(dev, core, ETNA_GPU_BUFFER_SIZE);
		return 0;
	case ETNA_GPU_INSTRUCTION_COUNT:
		*value = get_param(dev, core, ETNA_GPU_INSTRUCTION_COUNT);
		return 0;
	case ETNA_GPU_NUM_CONSTANTS:
		*value = get_param(dev, core, ETNA_GPU_NUM_CONSTANTS);
		return 0;
	case ETNA_GPU_NUM_VARYINGS:
		*value = get_param(dev, core, ETNA_GPU_NUM_VARYINGS);
		return 0;

	default:
		ERROR_MSG("invalid param id: %d", param);
		return -1;
	}

	return 0;
}

// pipe

static inline void get_abs_timeout(struct drm_etnaviv_timespec *tv, uint64_t ns)
{
	struct timespec t;
	uint32_t s = ns / 1000000000;
	clock_gettime(CLOCK_MONOTONIC, &t);
	tv->tv_sec = t.tv_sec + s;
	tv->tv_nsec = t.tv_nsec + ns - (s * 1000000000);
}

int etna_pipe_wait_ns(struct etna_pipe *pipe, uint32_t timestamp, uint64_t ns)
{
	if (pipe->nr_bos > 0) {
		struct etna_device *dev = pipe->gpu->dev;
		int ret;

		struct drm_etnaviv_wait_fence req = {
			.pipe = pipe->gpu->core,
			.fence = timestamp,
		};

		if (ns == 0)
			req.flags |= ETNA_WAIT_NONBLOCK;

		get_abs_timeout(&req.timeout, ns);

		ret = drmCommandWrite(dev->fd, DRM_ETNAVIV_WAIT_FENCE, &req, sizeof(req));
		if (ret) {
			ERROR_MSG("wait-fence failed! %d (%s)", ret, strerror(errno));
			return ret;
		}

		etna_bo_cache_clean(dev);

		for (int i = 0; i < pipe->nr_bos; i++) {
			pipe->bos[i]->state = ETNA_BO_READY;
		}
		pipe->nr_bos = 0;
	}
	return 0;
}

int etna_pipe_wait(struct etna_pipe *pipe, uint32_t timestamp, uint32_t ms)
{
	return etna_pipe_wait_ns(pipe, timestamp, ms * 1000000);
}

void etna_pipe_del(struct etna_pipe *pipe)
{
	free(pipe);
}

struct etna_pipe *etna_pipe_new(struct etna_gpu *gpu, enum etna_pipe_id id)
{
	struct etna_pipe *pipe;

	pipe = calloc(1, sizeof(*pipe));
	if (!pipe) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	pipe->id = id;
	pipe->gpu = gpu;
	pipe->nr_bos = 0;

	return pipe;
fail:
	return NULL;
}

// stream

static pthread_mutex_t idx_lock = PTHREAD_MUTEX_INITIALIZER;

static void *grow(void *ptr, uint32_t nr, uint32_t *max, uint32_t sz)
{
	if ((nr + 1) > *max) {
		if ((*max * 2) < (nr + 1))
			*max = nr + 5;
		else
			*max = *max * 2;
		ptr = realloc(ptr, *max * sz);
	}

	return ptr;
}

#define APPEND(x, name) ({ \
	(x)->name = grow((x)->name, (x)->nr_ ## name, &(x)->max_ ## name, sizeof((x)->name[0])); \
	(x)->nr_ ## name ++; \
})

static inline struct etna_cmd_stream_priv *
etna_cmd_stream_priv(struct etna_cmd_stream *stream)
{
	return (struct etna_cmd_stream_priv *)stream;
}

struct etna_cmd_stream *etna_cmd_stream_new(struct etna_pipe *pipe, uint32_t size,
        void (*reset_notify)(struct etna_cmd_stream *stream, void *priv),
        void *priv)
{
	struct etna_cmd_stream_priv *stream = NULL;

	if (size == 0) {
		ERROR_MSG("invalid size of 0");
		goto fail;
	}

	stream = calloc(1, sizeof(*stream));
	if (!stream) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	/* allocate even number of 32-bit words */
	size = ALIGN(size, 2);

	stream->base.buffer = malloc(size * sizeof(uint32_t));
	if (!stream->base.buffer) {
		ERROR_MSG("allocation failed");
		goto fail;
	}

	stream->base.size = size;
	stream->pipe = pipe;

	return &stream->base;

fail:
	if (stream)
		etna_cmd_stream_del(&stream->base);

	return NULL;
}

void etna_cmd_stream_del(struct etna_cmd_stream *stream)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);

	free(stream->buffer);
	free(priv->submit.relocs);
	free(priv);
}

static void reset_buffer(struct etna_cmd_stream *stream)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);

	stream->offset = 0;
	priv->submit.nr_bos = 0;
	priv->submit.nr_relocs = 0;
	priv->nr_bos = 0;

}

uint32_t etna_cmd_stream_timestamp(struct etna_cmd_stream *stream)
{
	return etna_cmd_stream_priv(stream)->last_timestamp;
}

static uint32_t append_bo(struct etna_cmd_stream *stream, struct etna_bo *bo)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);
	uint32_t idx;

	idx = APPEND(&priv->submit, bos);
	idx = APPEND(priv, bos);

	priv->submit.bos[idx].flags = 0;
	priv->submit.bos[idx].handle = bo->handle;

	priv->bos[idx] = (bo);

	return idx;
}

/* add (if needed) bo, return idx: */
static uint32_t bo2idx(struct etna_cmd_stream *stream, struct etna_bo *bo,
                       uint32_t flags)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);
	uint32_t idx;

	pthread_mutex_lock(&idx_lock);

	bo->state = ETNA_BO_STREAMED;

	if (!bo->current_stream) {
		idx = append_bo(stream, bo);
		bo->current_stream = stream;
		bo->idx = idx;
	} else if (bo->current_stream == stream) {
		idx = bo->idx;
	} else {
		/* slow-path: */
		for (idx = 0; idx < priv->nr_bos; idx++)
			if (priv->bos[idx] == bo)
				break;
		if (idx == priv->nr_bos) {
			/* not found */
			idx = append_bo(stream, bo);
		}
	}
	pthread_mutex_unlock(&idx_lock);

	if (flags & ETNA_RELOC_READ)
		priv->submit.bos[idx].flags |= ETNA_SUBMIT_BO_READ;
	if (flags & ETNA_RELOC_WRITE)
		priv->submit.bos[idx].flags |= ETNA_SUBMIT_BO_WRITE;

	return idx;
}

static void flush(struct etna_cmd_stream *stream, int in_fence_fd,
                  int *out_fence_fd)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);
	int ret, id = priv->pipe->id;
	struct etna_gpu *gpu = priv->pipe->gpu;

	struct drm_etnaviv_gem_submit req = {
		.pipe = gpu->core,
		.exec_state = id,
		.bos = VOID2U64(priv->submit.bos),
		.nr_bos = priv->submit.nr_bos,
		.relocs = VOID2U64(priv->submit.relocs),
		.nr_relocs = priv->submit.nr_relocs,
		.stream = VOID2U64(stream->buffer),
		.stream_size = stream->offset * 4, /* in bytes */
	};

	if (in_fence_fd != -1) {
		req.flags |= ETNA_SUBMIT_FENCE_FD_IN | ETNA_SUBMIT_NO_IMPLICIT;
		req.fence_fd = in_fence_fd;
	}

	if (out_fence_fd)
		req.flags |= ETNA_SUBMIT_FENCE_FD_OUT;

	ret = drmCommandWriteRead(gpu->dev->fd, DRM_ETNAVIV_GEM_SUBMIT,
	                          &req, sizeof(req));

	if (ret)
		ERROR_MSG("etna flush submit failed: %d (%s)", ret, strerror(errno));
	else
		priv->last_timestamp = req.fence;

	for (uint32_t i = 0; i < priv->nr_bos; i++) {
		struct etna_bo *bo = priv->bos[i];

		bo->current_stream = NULL;
		bo->state = ETNA_BO_FLUSHED;

		if (priv->pipe->nr_bos < ETNA_PIPE_BOS_SIZE) {
			priv->pipe->bos[priv->pipe->nr_bos] = bo;
			priv->pipe->nr_bos++;
		} else {
			ERROR_MSG("etna flush pipe bos array full");
		}
	}

	if (out_fence_fd)
		*out_fence_fd = req.fence_fd;
}

void etna_cmd_stream_flush(struct etna_cmd_stream *stream)
{
	flush(stream, -1, NULL);
	reset_buffer(stream);
}

void etna_cmd_stream_flush2(struct etna_cmd_stream *stream, int in_fence_fd,
                            int *out_fence_fd)
{
	flush(stream, in_fence_fd, out_fence_fd);
	reset_buffer(stream);
}

void etna_cmd_stream_finish(struct etna_cmd_stream *stream)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);

	flush(stream, -1, NULL);
	etna_pipe_wait(priv->pipe, priv->last_timestamp, 5000);
	reset_buffer(stream);
}

void etna_cmd_stream_reloc(struct etna_cmd_stream *stream, const struct etna_reloc *r)
{
	struct etna_cmd_stream_priv *priv = etna_cmd_stream_priv(stream);
	struct drm_etnaviv_gem_submit_reloc *reloc;
	uint32_t idx = APPEND(&priv->submit, relocs);
	uint32_t addr = 0;

	reloc = &priv->submit.relocs[idx];

	reloc->reloc_idx = bo2idx(stream, r->bo, r->flags);
	reloc->reloc_offset = r->offset;
	reloc->submit_offset = stream->offset * 4; /* in bytes */
	reloc->flags = 0;

//	INFO_MSG("etna_cmd_stream_reloc bo:%p idx:%d/%d", r->bo, idx, reloc->reloc_idx);

	etna_cmd_stream_emit(stream, addr);
}

// bo

/* get buffer info */
static int get_buffer_info(struct etna_bo *bo)
{
	int ret;
	struct drm_etnaviv_gem_info req = {
		.handle = bo->handle,
	};

	ret = drmCommandWriteRead(bo->dev->fd, DRM_ETNAVIV_GEM_INFO,
	                          &req, sizeof(req));
	if (ret) {
		return ret;
	}

	/* really all we need for now is mmap offset */
	bo->offset = req.offset;

	return 0;
}


/* allocate a new buffer object, call w/ table_lock held */
static struct etna_bo *bo_from_handle(struct etna_device *dev,
                                      uint32_t size, uint32_t handle, uint32_t flags)
{
	struct etna_bo *bo = calloc(sizeof(*bo), 1);

	bo->dev = dev;
	bo->size = size;
	bo->handle = handle;
	bo->flags = flags;
	bo->state = ETNA_BO_READY;

	return bo;
}

/* allocate a new (un-tiled) buffer object */
struct etna_bo *etna_bo_new(struct etna_device *dev, uint32_t size,
                            uint32_t flags)
{
	struct etna_bo *bo;
	int ret;
	struct drm_etnaviv_gem_new req = {
		.flags = flags,
	};

	req.size = size;
	ret = drmCommandWriteRead(dev->fd, DRM_ETNAVIV_GEM_NEW,
	                          &req, sizeof(req));
	if (ret)
		return NULL;

	bo = bo_from_handle(dev, size, req.handle, flags);
	return bo;
}


uint32_t etna_bo_handle(struct etna_bo *bo)
{
	return bo->handle;
}

/* caller owns the dmabuf fd that is returned and is responsible
 * to close() it when done
 */
int etna_bo_dmabuf(struct etna_bo *bo)
{
	int ret, prime_fd;

	ret = drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC,
	                         &prime_fd);
	if (ret) {
		ERROR_MSG("failed to get dmabuf fd: %d", ret);
		return ret;
	}

	return prime_fd;
}

uint32_t etna_bo_size(struct etna_bo *bo)
{
	return bo->size;
}

void *etna_bo_map(struct etna_bo *bo)
{
	if (!bo->map) {
		if (!bo->offset) {
			get_buffer_info(bo);
		}

		bo->map = mmap(0, bo->size, PROT_READ | PROT_WRITE,
		               MAP_SHARED, bo->dev->fd, bo->offset);
		if (bo->map == MAP_FAILED) {
			ERROR_MSG("mmap failed: %s", strerror(errno));
			bo->map = NULL;
		}
	}

	return bo->map;
}

/* import a buffer from dmabuf fd, does not take ownership of the
 * fd so caller should close() the fd when it is otherwise done
 * with it (even if it is still using the 'struct etna_bo *')
 */
struct etna_bo *etna_bo_from_dmabuf(struct etna_device *dev, int fd)
{
	struct etna_bo *bo;
	int ret, size;
	uint32_t handle;

	ret = drmPrimeFDToHandle(dev->fd, fd, &handle);
	if (ret) {
		return NULL;
	}

	/* lseek() to get bo size */
	size = lseek(fd, 0, SEEK_END);
	lseek(fd, 0, SEEK_CUR);

	bo = bo_from_handle(dev, size, handle, 0);

	return bo;
}

int etna_bo_cpu_prep(struct etna_bo *bo, uint32_t op, uint64_t ns)
{
	struct drm_etnaviv_gem_cpu_prep req = {
		.handle = bo->handle,
		.op = op,
	};

	get_abs_timeout(&req.timeout, ns);

	return drmCommandWrite(bo->dev->fd, DRM_ETNAVIV_GEM_CPU_PREP,
	                       &req, sizeof(req));
}

void etna_bo_cpu_fini(struct etna_bo *bo)
{
	struct drm_etnaviv_gem_cpu_fini req = {
		.handle = bo->handle,
	};

	drmCommandWrite(bo->dev->fd, DRM_ETNAVIV_GEM_CPU_FINI,
	                &req, sizeof(req));
}


/* destroy a buffer object */
void etna_bo_del(struct etna_bo *bo)
{

//	xf86DrvMsg(-1, X_INFO, "etna_bo_del destroy bo:%p map:%p size:%d\n",bo, bo->map, bo->size);
	if (bo->map) {
		munmap(bo->map, bo->size);
	}

	if (bo->handle) {
		struct drm_gem_close req = {
			.handle = bo->handle,
		};

		drmIoctl(bo->dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
	}


	free(bo);
}

/* extra */

void etna_nop(struct etna_cmd_stream *stream) {
	etna_cmd_stream_emit(stream, 0x18000000);
}

int etna_bo_ready(struct etna_bo *bo) {
	return (bo->state == ETNA_BO_READY);
}

int etna_bo_wait(struct etna_device *dev, struct etna_pipe *pipe, struct etna_bo *bo, uint64_t ns) {
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
}

struct etna_bo *etna_bo_from_usermem_prot(struct etna_device *dev, void *memory, size_t size, int flags) {
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
}


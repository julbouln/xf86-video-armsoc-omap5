#ifndef ETNAVIV_PRIV_H_
#define ETNAVIV_PRIV_H_

struct list_head
{
    struct list_head *prev;
    struct list_head *next;
};


struct etna_bo_bucket {
	uint32_t size;
	struct list_head list;
};

struct etna_bo_cache {
	struct etna_bo_bucket cache_bucket[14 * 4];
	unsigned num_buckets;
	time_t time;
};

struct etna_device {
	int fd;
	uint32_t refcnt;

	/* tables to keep track of bo's, to avoid "evil-twin" etna_bo objects:
	 *
	 *   handle_table: maps handle to etna_bo
	 *   name_table: maps flink name to etna_bo
	 *
	 * We end up needing two tables, because DRM_IOCTL_GEM_OPEN always
	 * returns a new handle.  So we need to figure out if the bo is already
	 * open in the process first, before calling gem-open.
	 */
	void *handle_table, *name_table;

	struct etna_bo_cache bo_cache;

	int closefd;        /* call close(fd) upon destruction */
};

/* a GEM buffer object allocated from the DRM device */
struct etna_bo {
	struct etna_device      *dev;
	void            *map;           /* userspace mmap'ing (if there is one) */
	uint32_t        size;
	uint32_t        handle;
	uint32_t        flags;
	uint32_t        name;           /* flink global handle (DRI2 name) */
	uint64_t        offset;         /* offset to mmap() */
	uint32_t        refcnt;

	/* in the common case, a bo won't be referenced by more than a single
	 * command stream.  So to avoid looping over all the bo's in the
	 * reloc table to find the idx of a bo that might already be in the
	 * table, we cache the idx in the bo.  But in order to detect the
	 * slow-path where bo is ref'd in multiple streams, we also must track
	 * the current_stream for which the idx is valid.  See bo2idx().
	 */
	struct etna_cmd_stream *current_stream;
	uint32_t idx;

	int reuse;
	struct list_head list;   /* bucket-list entry */
	time_t free_time;        /* time when added to bucket-list */
};

#endif /* ETNAVIV_PRIV_H_ */

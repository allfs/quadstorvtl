/*
 * Target I/O.
 * (C) 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 */

#include "iscsi.h"
#include "iscsi_dbg.h"
#include "iotype.h"

static int tio_add_pages(struct tio *tio, int count)
{
	int i;
	pagestruct_t *page;

	dprintk(D_GENERIC, "%p %d (%d)\n", tio, count, tio->pg_cnt);

	tio->pg_cnt = count;

	count *= sizeof(pagestruct_t *);

	do {
		tio->pvec = zalloc(count, M_IETTIO, M_NOWAIT);
		if (!tio->pvec)
			yield();
	} while (!tio->pvec);

	for (i = 0; i < tio->pg_cnt; i++) {
		do {
			if (!(page = page_alloc(0)))
                                yield();
		} while (!page);
		tio->pvec[i] = page;
	}
	return 0;
}

static slab_cache_t *tio_cache;

struct tio *tio_alloc(int count)
{
	struct tio *tio;

	tio = slab_cache_alloc(tio_cache, M_WAITOK, sizeof(*tio));

	tio->pg_cnt = 0;
	tio->idx = 0;
	tio->offset = 0;
	tio->size = 0;
	tio->pvec = NULL;

	atomic_set(&tio->count, 1);

	if (count)
		tio_add_pages(tio, count);

	return tio;
}

static void tio_free(struct tio *tio)
{
	int i;
	for (i = 0; i < tio->pg_cnt; i++) {
		assert(tio->pvec[i]);
		page_free(tio->pvec[i]);
	}
	free(tio->pvec, M_IETTIO);
	slab_cache_free(tio_cache, tio);
}

void tio_put(struct tio *tio)
{
	assert(atomic_read(&tio->count));
	if (atomic_dec_and_test(&tio->count))
		tio_free(tio);
}

void tio_get(struct tio *tio)
{
	atomic_inc(&tio->count);
}

void tio_set(struct tio *tio, u32 size, loff_t offset)
{
	tio->idx = offset >> PAGE_CACHE_SHIFT;
	tio->offset = offset & ~PAGE_CACHE_MASK;
	tio->size = size;
}

int tio_read(struct iet_volume *lu, struct tio *tio)
{
	struct iotype *iot = lu->iotype;
	assert(iot);
	if (!tio->size)
		return 0;
	return iot->make_request ? iot->make_request(lu, tio, READ) : 0;
}

int tio_write(struct iet_volume *lu, struct tio *tio)
{
	struct iotype *iot = lu->iotype;
	assert(iot);
	if (!tio->size)
		return 0;
	return iot->make_request ? iot->make_request(lu, tio, WRITE) : 0;
}

int tio_sync(struct iet_volume *lu, struct tio *tio)
{
	struct iotype *iot = lu->iotype;
	assert(iot);
	return iot->sync ? iot->sync(lu, tio) : 0;
}

int tio_init(void)
{
#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	tio_cache = slab_cache_create("istio", sizeof(struct tio),
				      0, 0, NULL, NULL);
#else
	tio_cache = slab_cache_create("istio", sizeof(struct tio),
				      0, 0, NULL);
#endif
#else
	tio_cache = slab_cache_create("istio", sizeof(struct tio),
				      NULL, NULL, NULL, NULL, 0, 0);
#endif
	return  tio_cache ? 0 : -ENOMEM;
}

void tio_exit(void)
{
	if (tio_cache)
		slab_cache_destroy(tio_cache);
}

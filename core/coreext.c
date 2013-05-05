/*
 * Core defs for other operating systems.
 */
#include "coreext.h"
#include "bdev.h"
#include "tcache.h"

void
sys_memset(void *dest, int c, int size)
{
	unsigned char *s = dest;
	while (size--)
		*s++ = c;
}

void
free(void *ptr, int mtype)
{
	(*kcbs.free)(ptr);
}

void * 
zalloc(size_t size, int type, int flags)
{
	return (*kcbs.zalloc)(size, type, flags);
}

static struct tpriv tpriv;

void
thread_start(void)
{
	(*kcbs.thread_start)(&tpriv);
	return;
}

void
thread_end(void)
{
	(*kcbs.thread_end)(&tpriv);
	return;
}

bio_t *
bio_get_new(struct bdevint *bint, void *end_bio_func, void *consumer, uint64_t b_start, int bio_vec_count, int rw)
{
	uint64_t bi_sector = BIO_SECTOR(b_start, bint->sector_shift);
	iodev_t *iodev = bint->b_dev;
	bio_t *bio;

	bio = (*kcbs.g_new_bio)(iodev, end_bio_func, consumer, bi_sector, bio_vec_count, rw);
	return bio; 
}

int
tcache_need_new_bio(struct tcache *tcache, bio_t *bio, uint64_t b_start, struct bdevint *bint, int stat)
{
	int nr_sectors;
	uint64_t bi_block;

	if (bint->b_dev != (*kcbs.bio_get_iodev)(bio)) {
		return 1;
	}

	nr_sectors = (*kcbs.bio_get_nr_sectors)(bio);
	bi_block = BIO_SECTOR(b_start, bint->sector_shift);
	if ((((*kcbs.bio_get_start_sector)(bio)) + nr_sectors) != bi_block) {
		return 1;
	}
	else
		return 0;
}

/* 
 * Copyright (C) Shivaram Upadhyayula <shivaram.u@quadstor.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 as published by the Free Software Foundation
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301, USA.
 */

#include "tcache.h"

struct tcache *
tcache_alloc(int bio_count)
{
	struct tcache *tcache;

	tcache = __uma_zalloc(tcache_cache, Q_WAITOK | Q_ZERO, sizeof(*tcache)); 
	atomic_set(&tcache->refs, 1);
	tcache->completion = wait_completion_alloc("tcache compl");
#ifdef FREEBSD
	tcache->bio_list = zalloc((bio_count * sizeof(struct biot *)), M_TCACHE, Q_WAITOK);
#else
	tcache->bio_list = zalloc((bio_count * sizeof(struct bio *)), M_TCACHE, Q_WAITOK);
#endif
	tcache->bio_count = bio_count;
	return tcache;
}

#ifdef FREEBSD
void
tcache_free_pages(struct tcache *tcache)
{
	int i;
	pagestruct_t *page;

	for (i = 0; i < tcache->bio_count; i++)
	{
		struct biot *biot;
		int j;

		biot = tcache->bio_list[i];
		if (!biot)
			break;

		for (j = 0; j < biot->page_count; j++) {
			page = biot->pages[j];
			vm_pg_free(page);
		}
	}
}
#else
void
tcache_free_pages(struct tcache *tcache)
{
	int i;

	for (i = 0; i < tcache->bio_count; i++)
	{
		bio_t *bio;

		bio = tcache->bio_list[i];
		if (!bio)
			break;
		bio_free_pages(bio);
	}
}
#endif

void
tcache_free(struct tcache *tcache)
{
	int i;

	for (i = 0; i < tcache->bio_count; i++)
	{
		if (!tcache->bio_list[i])
			break;
#ifdef FREEBSD
		g_destroy_biot(tcache->bio_list[i]);
#else
		g_destroy_bio(tcache->bio_list[i]);
#endif
	}

	free(tcache->bio_list, M_TCACHE);
	wait_completion_free(tcache->completion);
	uma_zfree(tcache_cache, tcache);
}

#ifdef FREEBSD 
void tcache_end_bio(struct bio *bio)
#else
void tcache_end_bio(struct bio *bio, int err)
#endif
{
	struct tcache *tcache;
#ifdef FREEBSD
	int err = bio->bio_error;
	struct biot *biot = (struct biot *)bio_get_caller(bio);
#endif

#ifdef FREEBSD
	tcache = biot->cache;
#else
	tcache = (struct tcache *)bio_get_caller(bio);
#endif

	if (unlikely(err))
		atomic_set_bit(TCACHE_IO_ERROR, &tcache->flags);

	if (!(atomic_dec_and_test(&tcache->bio_remain)))
		return;

	wait_complete(tcache->completion);
	tcache_put(tcache);
}

int
__tcache_add_page(struct tcache *tcache, pagestruct_t *page, uint64_t b_start, struct bdevint *bint, int size, int rw, void *end_bio)
{
#ifdef FREEBSD
	struct biot *bio = NULL;
#else
	struct bio *bio = NULL;
	uint32_t max_pages;
#endif
	int retval;

	bio = tcache->bio_list[tcache->last_idx];
	if (bio && tcache_need_new_bio(tcache, bio, b_start, bint, 1)) {
		tcache->last_idx++;
		bio = NULL;
	}

again:
	if (!bio) {
#ifdef FREEBSD
		bio = biot_alloc(bint, b_start, tcache);
#else
		max_pages = bio_get_max_pages(bint->b_dev);
		if (unlikely(!max_pages))
			max_pages = 32;
		bio = bio_get_new(bint, end_bio, tcache, b_start, max_pages, rw);
#endif
		if (unlikely(!bio)) {
			return -1;
		}

		tcache->bio_list[tcache->last_idx] = bio;
		atomic_inc(&tcache->bio_remain);
	}

#ifdef FREEBSD
	retval = biot_add_page(bio, page, size);
#else
	retval = bio_add_page(bio, page, size, 0); 
#endif
	if (unlikely(retval != size)) {
#ifndef FREEBSD 
		if (unlikely(!bio_get_length(bio)))
			return -1;
#endif
		tcache->last_idx++;

		bio = NULL;
		goto again;
	}
	tcache->size += size;
	return 0;
}

int
tcache_add_page(struct tcache *tcache, pagestruct_t *page, uint64_t b_start, struct bdevint *bint, int size, int rw)
{
	return __tcache_add_page(tcache, page, b_start, bint, size, rw, tcache_end_bio);
}

#ifdef FREEBSD
void
__tcache_entry_rw(struct tcache *tcache, int rw, void *end_bio)
{
	int i;
	struct bdevint *b_dev;

	atomic_set_bit(TCACHE_IO_SUBMITTED, &tcache->flags);
	tcache_get(tcache);
	for (i = 0; i < tcache->bio_count; i++)
	{
		struct biot *bio = tcache->bio_list[i];

		if (!bio)
			break;

		b_dev = bio->bint;
		send_biot(bio, rw, end_bio);
	}
}

void
tcache_entry_rw(struct tcache *tcache, int rw)
{
	__tcache_entry_rw(tcache, rw, tcache_end_bio);
}
#else
void
__tcache_entry_rw(struct tcache *tcache, int rw, void *end_bio)
{
	tcache_entry_rw(tcache, rw);
}

void
tcache_entry_rw(struct tcache *tcache, int rw)
{
	int i;
	iodev_t *prev_b_dev = NULL, *b_dev;
	int count;
	struct tpriv priv = { 0 };
	int log = atomic_test_bit(TCACHE_LOG_WRITE, &tcache->flags);

	atomic_set_bit(TCACHE_IO_SUBMITTED, &tcache->flags);

	bzero(&priv, sizeof(priv));
	tcache_get(tcache);
	count = atomic_read(&tcache->bio_remain);
	for (i = 0; i < count; i++) {
		struct bio *bio = tcache->bio_list[i];

		b_dev = send_bio(bio);
		if (prev_b_dev && ((b_dev != prev_b_dev) || log)) {
			if (!priv.data || log) {
				bdev_start(prev_b_dev, &priv);
				bdev_marker(b_dev, &priv);
			}
		}
		else if (!prev_b_dev) {
			bdev_marker(b_dev, &priv);
		}
		prev_b_dev = b_dev;
	}

	if (prev_b_dev) {
		bdev_start(prev_b_dev, &priv);
	}
}
#endif

int
tcache_zero_range(struct bdevint *bint, uint64_t b_start, int pages)
{
	pagestruct_t *page;
	struct tcache *tcache;
	struct tcache_list tcache_list;
	int todo, retval;
	int i;

	SLIST_INIT(&tcache_list);
	page = vm_pg_alloc(VM_ALLOC_ZERO);
	if (!page) {
		debug_warn("Page allocation failure\n");
		return -1;
	}

	while (pages) {
		todo = min_t(int, 1024, pages);
		tcache = tcache_alloc(todo);
		pages -= todo;
		for (i = 0; i < todo; i++) {
			retval = tcache_add_page(tcache, page, b_start, bint, LBA_SIZE, QS_IO_WRITE);
			if (unlikely(retval != 0)) {
				debug_warn("tcache add page failed\n");
				tcache_put(tcache);
				goto err;
			}
			b_start += (LBA_SIZE >> bint->sector_shift);
		}
		tcache_entry_rw(tcache, QS_IO_WRITE);
		SLIST_INSERT_HEAD(&tcache_list, tcache, t_list);
	}

	retval = tcache_list_wait(&tcache_list);
	vm_pg_free(page);
	return retval;
err:
	tcache_list_wait(&tcache_list);
	vm_pg_free(page);
	return -1;
}


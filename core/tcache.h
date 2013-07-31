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

#ifndef QS_TCACHE_H_
#define QS_TCACHE_H_

#include "coredefs.h"
#include "bdev.h"

#define LOG_GROUP_MAX_PAGES	16 /* 64KB writes at time */
struct tcache {
#ifdef FREEBSD
	struct biot **bio_list;
#else
	struct bio **bio_list;
#endif
	wait_compl_t *completion;
	int flags;
	uint16_t bio_count;
	uint16_t last_idx;
	uint32_t size;
	atomic_t bio_remain;
	atomic_t refs;
	SLIST_ENTRY(tcache) t_list;
};

#define TCACHE_MAX_SIZE	(512 * 1024)

SLIST_HEAD(tcache_list, tcache);

#define tcache_get(tch)		atomic_inc(&(tch)->refs)

struct tcache * tcache_alloc(int bio_count);
void tcache_free(struct tcache *tcache);
#define tcache_put(tch)					\
do {							\
	if (atomic_dec_and_test(&(tch)->refs))		\
		tcache_free(tch);			\
} while (0)

void tcache_entry_rw(struct tcache *tcache, int rw);
void __tcache_entry_rw(struct tcache *tcache, int rw, void *end_bio);

#ifdef FREEBSD 
void tcache_end_bio(struct bio *bio);
#else
void tcache_end_bio(struct bio *bio, int err);
#endif

int tcache_add_page(struct tcache *tcache, pagestruct_t *page, uint64_t b_start, struct bdevint *bint, int size, int rw);
void tcache_free_pages(struct tcache *tcache);
void tcache_free_data(struct tcache *tcache);
int __tcache_add_page(struct tcache *tcache, pagestruct_t *page, uint64_t b_start, struct bdevint *bint, int size, int rw, void *end_bio);

static inline int
tcache_list_wait(struct tcache_list *tcache_list)
{
	int error = 0;
	struct tcache *tcache;

	while ((tcache = SLIST_FIRST(tcache_list)) != NULL) {
		SLIST_REMOVE_HEAD(tcache_list, t_list);
		debug_check(!atomic_test_bit(TCACHE_IO_SUBMITTED, &tcache->flags));
		wait_for_done(tcache->completion);
		if (atomic_test_bit(TCACHE_IO_ERROR, &tcache->flags))
			error = -1;
		tcache_put(tcache);
	}
	return error;
}

static inline void
tcache_list_free(struct tcache_list *tcache_list)
{
	struct tcache *tcache;

	while ((tcache = SLIST_FIRST(tcache_list)) != NULL) {
		SLIST_REMOVE_HEAD(tcache_list, t_list);
		tcache_put(tcache);
	}
}

int tcache_zero_range(struct bdevint *bint, uint64_t b_start, int pages);

#endif

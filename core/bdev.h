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

#ifndef QUADSTOR_BDEV_H_
#define QUADSTOR_BDEV_H_
#include "coredefs.h"
#include "../common/commondefs.h"

struct raw_bintindex {
	uint16_t csum;
	uint16_t pad[3];
};

struct bintunmap {
	struct bintindex *index;
	int entry;
	int pos;
	TAILQ_ENTRY(bintunmap) u_list;
};

struct bintindex {
	pagestruct_t *metadata;
	struct bdevint *bint;
	uint64_t b_start;
	STAILQ_ENTRY(bintindex) i_list;
	TAILQ_HEAD(, bintunmap) unmap_list;
	wait_chan_t *index_wait;
	atomic_t refs;
	int index_id;
};

static inline void
index_get(struct bintindex *index)
{
	atomic_inc(&index->refs);
}

static inline void
index_put(struct bintindex *index)
{
	debug_check(!atomic_read(&index->refs));
	atomic_dec(&index->refs);
}

struct bdevgroup;
struct bdevint {
	uint64_t b_start;
	uint64_t index_b_start;
	uint64_t b_end;
	uint64_t size;
	uint64_t usize;
	atomic64_t free;
	uint32_t bid;
	uint32_t sector_size;
	uint32_t sector_shift;
	int flags;
	int group_flags;
	uint8_t  vendor[8];
	uint8_t  product[16];
	uint8_t  serialnumber[32];
	char mrid[TL_RID_MAX];
	iodev_t *b_dev;
	g_consumer_t *cp;
	struct bdevgroup *group;
	SLIST_ENTRY(bdevint) a_list;
	STAILQ_HEAD(, bintindex) index_list;
	STAILQ_HEAD(, bintindex) check_list;
	int index_count;
	sx_t *bint_lock;
};

static inline uint32_t
bint_blocks(struct bdevint *bint, uint32_t size)
{
	uint32_t sector_mask;
	uint32_t blocks;

	sector_mask = (1 << bint->sector_shift) - 1;
	blocks = size >> bint->sector_shift;
	if (size & sector_mask)
		blocks++;
	return blocks;
}
#define BMAP_ENTRIES		(BINT_INDEX_META_SIZE - sizeof(struct raw_bintindex))
#define UNITS_PER_INDEX		(BMAP_ENTRIES  * 8)

static inline int
bint_nindexes(uint64_t usize)
{
	uint64_t units;
	int nindexes;

	units = usize >> BINT_UNIT_SHIFT;
	nindexes = units / UNITS_PER_INDEX;
	if (units % UNITS_PER_INDEX)
		nindexes++;
	return nindexes;
}

static inline uint64_t
bdev_tape_bstart(struct bdevint *bint, int tape_id)
{
	uint64_t offset;

	offset = (VTAPES_OFFSET + (LBA_SIZE * tape_id));
	return (offset >> bint->sector_shift);
}

static inline uint64_t
calc_alloc_block(struct bdevint *bint, uint64_t index_id, uint64_t i, uint64_t j)
{
	uint64_t units;
	
	units = (index_id * UNITS_PER_INDEX) + (i * 8) + j;
	return ((units << BINT_UNIT_SHIFT) >> bint->sector_shift); 
}

static inline int
calc_bmap_entries(struct bdevint *bint, int index_id)
{
	uint64_t units, min_units;
	uint64_t entries;

	units = bint->usize >> BINT_UNIT_SHIFT;
	units -= (index_id * UNITS_PER_INDEX);
	min_units = min_t(uint64_t, UNITS_PER_INDEX, units);
	entries = (min_units >> 3);
	if (min_units & 0x7)
		entries++;
	return (int)(entries);
}

static inline int 
calc_index_id(struct bdevint *bint, uint64_t block, int *ret_i, int *ret_j)
{
	uint64_t span = block << bint->sector_shift;
	uint64_t units = span >> BINT_UNIT_SHIFT;
	uint64_t index_id;
	uint64_t bits;

	index_id = units / UNITS_PER_INDEX;
	bits = units % UNITS_PER_INDEX;

	*ret_i = bits / 8;
	*ret_j = bits & 0x7;
	return index_id;
}

static inline uint64_t
get_iter_start(int index_id, int i)
{
	if (index_id || i)
		return 0;
	else
		return BINT_RESERVED_SEGMENTS; 
}

#define bint_lock(b)	sx_xlock((b)->bint_lock)
#define bint_unlock(b)	sx_xunlock((b)->bint_lock)

#define BINT_MAX_INDEX_COUNT		8

static inline uint64_t
bdev_get_bend(struct bdevint *bint, uint64_t b_start)
{
	uint64_t b_end;
	b_end = b_start + (BINT_UNIT_SIZE >> bint->sector_shift);
	return b_end;
}

extern struct bdevint *bint_list[];

static inline struct bdevint *
bdev_find(uint32_t bid)
{
	if (bid < TL_MAX_DISKS)
		return bint_list[bid];
	else
		return NULL;
}

int bdev_add_new(struct bdev_info *binfo);
int bdev_remove(struct bdev_info *binfo);
int bdev_get_info(struct bdev_info *binfo);
int bdev_unmap_config(struct bdev_info *binfo);
int bdev_release_block(struct bdevint *bint, uint64_t block);
uint64_t bdev_get_block(struct bdevint *bint, struct bdevint **ret, uint64_t *b_end);
void bdev_finalize(void);
void bint_incr_free(struct bdevint *bint, uint64_t freed);
struct bintindex * bint_get_index(struct bdevint *bint, int index_id);
int bint_index_io(struct bdevint *bint, struct bintindex *index, int rw);

uint64_t bint_index_bstart(struct bdevint *bint, int index);
int bint_toggle_index_full(struct bdevint *bint, int index, int full, int async);
void bint_index_free(struct bintindex *index);
int bint_sync(struct bdevint *bint);
int bdev_check_disks(void);

static inline int
bint_unmap_supported(struct bdevint *bint)
{
	return atomic_test_bit(GROUP_FLAGS_UNMAP, &bint->group_flags);
}

#endif

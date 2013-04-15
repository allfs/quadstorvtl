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

#include "coredefs.h"
#include "tape_partition.h"
#include "map_lookup.h"
#include "blk_map.h"
#include "qs_lib.h"

extern uma_t *map_lookup_cache;
static struct map_lookup * __map_lookup_load(struct tape_partition *partition, uint64_t b_start, uint32_t bid);

static uint64_t
map_lookup_bstart(struct map_lookup *map_lookup)
{
	return map_lookup->b_start;
}

static struct bdevint *
map_lookup_bint(struct map_lookup *map_lookup)
{
	return map_lookup->bint;
}

int
map_lookup_map_has_next(struct blk_map *map)
{
	if (map->mlookup->next_block)
		return 1;

	if (map->mlookup_entry_id != (map->mlookup->map_nrs - 1))
		return 1;

	return 0;
}

struct map_lookup *
mlookup_get_prev(struct map_lookup *mlookup)
{
	return TAILQ_PREV(mlookup, maplookup_list, l_list);
}

struct map_lookup *
mlookup_get_next(struct map_lookup *mlookup)
{
	return TAILQ_NEXT(mlookup, l_list);
}

int 
mlookup_has_next(struct map_lookup *mlookup)
{
	return (mlookup_get_next(mlookup) != NULL);
}

int 
mlookup_has_prev(struct map_lookup *mlookup)
{
	return (mlookup_get_prev(mlookup) != NULL);
}

static inline void
map_lookup_insert_before(struct map_lookup *mlookup, struct map_lookup *new)
{
	TAILQ_INSERT_BEFORE(mlookup, new, l_list);
	mlookup->partition->mlookup_count++;
}

static inline void
map_lookup_insert_after(struct map_lookup *mlookup, struct map_lookup *new)
{
	TAILQ_INSERT_AFTER(&mlookup->partition->mlookup_list, mlookup, new, l_list);
	mlookup->partition->mlookup_count++;
}

void
map_lookup_remove(struct tape_partition *partition, struct map_lookup *mlookup)
{
	if (atomic_read(&mlookup->refs) > 1 || !mlookup->l_ids_start)
		return;

	TAILQ_REMOVE(&partition->mlookup_list, mlookup, l_list);
	map_lookup_put(mlookup);
}

void
map_lookup_free_till_cur(struct tape_partition *partition)
{
	struct map_lookup *mlookup, *next, *cur_mlookup;

	if (!partition->cur_map)
		return;

	cur_mlookup = partition->cur_map->mlookup;
	TAILQ_FOREACH_SAFE(mlookup, &partition->mlookup_list, l_list, next) {
		if (mlookup == cur_mlookup)
			break;
		map_lookup_remove(partition, mlookup);
	}
}

#ifdef FREEBSD 
void static map_lookup_end_bio(bio_t *bio)
#else
void static map_lookup_end_bio(bio_t *bio, int err)
#endif
{
	struct map_lookup *map_lookup = (struct map_lookup *)bio_get_caller(bio);
#ifdef FREEBSD
	int err = bio->bio_error;
#endif

	if (unlikely(err))
		atomic_set_bit(META_DATA_ERROR, &map_lookup->flags);

	if (bio_get_command(bio) == QS_IO_WRITE)
		atomic_clear_bit(META_DATA_DIRTY, &map_lookup->flags);
	else
		atomic_clear_bit(META_DATA_READ_DIRTY, &map_lookup->flags);

	chan_wakeup(map_lookup->map_lookup_wait);
	map_lookup_put(map_lookup);
	g_destroy_bio(bio);
}

static void
map_lookup_write_csum(struct map_lookup *map_lookup)
{

}

static int
map_lookup_io(struct map_lookup *map_lookup, int rw)
{
	int retval;

	if (rw == QS_IO_WRITE) {
		debug_check(atomic_test_bit(META_DATA_DIRTY, &map_lookup->flags));
		map_lookup_write_csum(map_lookup);
		atomic_set_bit(META_DATA_DIRTY, &map_lookup->flags);
		atomic_clear_bit(META_IO_PENDING, &map_lookup->flags);
		atomic_clear_bit(META_DATA_CLONED, &map_lookup->flags);
	}
	else {
		atomic_set_bit(META_DATA_READ_DIRTY, &map_lookup->flags);
	}

	map_lookup_get(map_lookup);
	retval = qs_lib_bio_page(map_lookup->bint, map_lookup->b_start, LBA_SIZE, map_lookup->metadata, map_lookup_end_bio, map_lookup, rw, 0);
	if (unlikely(retval != 0)) {
		debug_warn("Failed to load amap table at %llu bid %u\n", (unsigned long long)map_lookup_bstart(map_lookup), map_lookup_bint(map_lookup)->bid);
		atomic_clear_bit(META_DATA_DIRTY, &map_lookup->flags);
		atomic_clear_bit(META_DATA_READ_DIRTY, &map_lookup->flags);
		atomic_set_bit(META_DATA_ERROR, &map_lookup->flags);
		chan_wakeup(map_lookup->map_lookup_wait);
		map_lookup_put(map_lookup);
	}
	return retval;
}

int
map_lookup_flush_meta(struct map_lookup *mlookup)
{
	struct raw_map_lookup *raw_mlookup;
	int retval;
	uint16_t csum;

	if (!atomic_test_bit(META_IO_PENDING, &mlookup->flags))
		return 0;

	wait_on_chan(mlookup->map_lookup_wait, !atomic_test_bit(META_DATA_DIRTY, &mlookup->flags));
	if (atomic_test_bit(META_DATA_ERROR, &mlookup->flags))
		return -1;
 	raw_mlookup = (struct raw_map_lookup *)(vm_pg_address(mlookup->metadata) + (LBA_SIZE - sizeof(*raw_mlookup)));
	csum = net_calc_csum16(vm_pg_address(mlookup->metadata), LBA_SIZE - sizeof(*raw_mlookup));

	raw_mlookup->csum = csum;
	raw_mlookup->map_nrs = mlookup->map_nrs;
	raw_mlookup->next_block = mlookup->next_block;
	raw_mlookup->prev_block = mlookup->prev_block;
	raw_mlookup->l_ids_start = mlookup->l_ids_start;
	raw_mlookup->f_ids_start = mlookup->f_ids_start;
	raw_mlookup->s_ids_start = mlookup->s_ids_start;
	retval = map_lookup_io(mlookup, QS_IO_WRITE);
	return retval;
}

void
__map_lookup_free_all(struct maplookup_list *mlookup_list)
{
	struct map_lookup *mlookup;

	while ((mlookup = TAILQ_FIRST(mlookup_list)) != NULL) {
		TAILQ_REMOVE(mlookup_list, mlookup, l_list);
		if (atomic_read(&mlookup->refs) > 1)
			debug_warn("map lookup refs %d\n", atomic_read(&mlookup->refs));
		map_lookup_put(mlookup);
	}
}

void
map_lookup_free_all(struct tape_partition *partition)
{
	__map_lookup_free_all(&partition->mlookup_list);
	partition->mlookup_count = 0;
}

void
map_lookup_free_from_mlookup(struct tape_partition *partition, struct map_lookup *mlookup)
{
	struct map_lookup *next;

	while (mlookup) {
		next = TAILQ_NEXT(mlookup, l_list);
		TAILQ_REMOVE(&partition->mlookup_list, mlookup, l_list);
		partition->mlookup_count--;
		wait_on_chan(mlookup->map_lookup_wait, !atomic_test_bit(META_DATA_DIRTY, &mlookup->flags) && !atomic_test_bit(META_DATA_READ_DIRTY, &mlookup->flags));
		map_lookup_put(mlookup);
		mlookup = next;
	}
	return;
}

void
map_lookup_free(struct map_lookup *mlookup)
{
	if (mlookup->metadata)
		vm_pg_free(mlookup->metadata);

	wait_chan_free(mlookup->map_lookup_wait);
	uma_zfree(map_lookup_cache, mlookup);
}

static int
map_lookup_read_header(struct map_lookup *mlookup)
{
	struct raw_map_lookup *raw_mlookup;
	uint16_t csum;

	if (atomic_test_bit(META_DATA_LOADED, &mlookup->flags))
		return 0;

 	raw_mlookup = (struct raw_map_lookup *)(vm_pg_address(mlookup->metadata) + (LBA_SIZE - sizeof(*raw_mlookup)));
	csum = net_calc_csum16(vm_pg_address(mlookup->metadata), LBA_SIZE - sizeof(*raw_mlookup));
	if (csum != raw_mlookup->csum) {
		debug_warn("Mismatch in csum got %x stored %x\n", csum, raw_mlookup->csum);
		return -1;
	}
	mlookup->map_nrs = raw_mlookup->map_nrs;
	mlookup->next_block = raw_mlookup->next_block;
	mlookup->prev_block = raw_mlookup->prev_block;
	mlookup->l_ids_start = raw_mlookup->l_ids_start;
	mlookup->f_ids_start = raw_mlookup->f_ids_start;
	mlookup->s_ids_start = raw_mlookup->s_ids_start;
	map_lookup_resync_ids(mlookup);
	atomic_set_bit(META_DATA_LOADED, &mlookup->flags);
	return 0;
}

static inline int
map_lookup_check_read(struct map_lookup *mlookup)
{
	if (atomic_test_bit(META_DATA_LOADED, &mlookup->flags))
		return 0;

	wait_on_chan(mlookup->map_lookup_wait, !atomic_test_bit(META_DATA_READ_DIRTY, &mlookup->flags));

	if (atomic_test_bit(META_DATA_ERROR, &mlookup->flags))
	{
		return -1;
	}
	return map_lookup_read_header(mlookup);
}

static int
map_lookup_read_meta(struct map_lookup *mlookup)
{
	int retval;

	if (atomic_test_bit(META_DATA_LOADED, &mlookup->flags))
		return 0;

	wait_on_chan(mlookup->map_lookup_wait, !atomic_test_bit(META_DATA_READ_DIRTY, &mlookup->flags));
	if (atomic_test_bit(META_DATA_ERROR, &mlookup->flags))
		return -1;
	retval = map_lookup_read_header(mlookup);
	return retval;
}

struct map_lookup *
map_lookup_load_prev(struct map_lookup *mlookup)
{
	struct map_lookup *ret_lookup;
	struct tape_partition *partition = mlookup->partition;
	struct map_lookup *prev_mlookup;
	int retval;

	if (!mlookup->prev_block)
	{
		return NULL;
	}

	prev_mlookup = mlookup_get_prev(mlookup);
	if (prev_mlookup && prev_mlookup->b_start == BLOCK_BLOCKNR(mlookup->prev_block) && prev_mlookup->bint->bid == BLOCK_BID(mlookup->prev_block))
	{
		retval = map_lookup_check_read(prev_mlookup);
		if (retval != 0) {
			TAILQ_REMOVE(&partition->mlookup_list, prev_mlookup, l_list);
			map_lookup_free(prev_mlookup);
			return NULL;
		}

		ret_lookup = prev_mlookup;
		return ret_lookup;
	}

	ret_lookup = map_lookup_load(partition, BLOCK_BLOCKNR(mlookup->prev_block), BLOCK_BID(mlookup->prev_block));
	if (!ret_lookup)
	{
		return NULL;
	}
	map_lookup_insert_before(mlookup, ret_lookup);

	return ret_lookup;
}

void
map_lookup_load_next_async(struct map_lookup *mlookup)
{
	struct tape_partition *partition = mlookup->partition;
	struct map_lookup *ret_lookup;
	struct map_lookup *next_lookup;

	if (!mlookup->next_block)
	{
		return;
	}

	next_lookup = mlookup_get_next(mlookup);
	if (next_lookup && next_lookup->b_start == BLOCK_BLOCKNR(mlookup->next_block) && next_lookup->bint->bid == BLOCK_BID(mlookup->next_block))
	{
		return;
	}

	ret_lookup = __map_lookup_load(partition, BLOCK_BLOCKNR(mlookup->next_block), BLOCK_BID(mlookup->next_block));
	if (!ret_lookup)
		return;

	map_lookup_insert_after(mlookup, ret_lookup);
	return;
}

int
map_lookup_next_avail(struct map_lookup *mlookup)
{
	int avail = 0;
	struct map_lookup *next_lookup;

	if (!mlookup->next_block)
	{
		return 0;
	}

	next_lookup = mlookup_get_next(mlookup);
	if (next_lookup && next_lookup->b_start ==  BLOCK_BLOCKNR(mlookup->next_block) && next_lookup->bint->bid ==  BLOCK_BID(mlookup->next_block))
	{
		avail = 1;
	}
	return avail;
}

struct map_lookup *
map_lookup_load_next(struct map_lookup *mlookup)
{
	struct map_lookup *ret_lookup;
	struct tape_partition *partition = mlookup->partition;
	struct map_lookup *next_lookup;
	int retval;

	if (!mlookup->next_block)
		return NULL;

	next_lookup = mlookup_get_next(mlookup);
	if (next_lookup && next_lookup->b_start ==  BLOCK_BLOCKNR(mlookup->next_block) && next_lookup->bint->bid ==  BLOCK_BID(mlookup->next_block))
	{
		retval = map_lookup_check_read(next_lookup);
		if (retval != 0) {
			TAILQ_REMOVE(&partition->mlookup_list, next_lookup, l_list);
			map_lookup_free(next_lookup);
			return NULL;
		}

		ret_lookup = next_lookup;
		return ret_lookup;
	}

	ret_lookup = map_lookup_load(partition, BLOCK_BLOCKNR(mlookup->next_block), BLOCK_BID(mlookup->next_block));
	if (!ret_lookup)
		return NULL;

	map_lookup_insert_after(mlookup, ret_lookup);
	return ret_lookup;
}


struct map_lookup *
map_lookup_find_last(struct tape_partition *partition)
{
	uint64_t lookup_start;
	uint32_t lookup_bid;
	int retval;
	struct map_lookup *mlookup;

	mlookup = tape_partition_last_mlookup(partition);
	retval = map_lookup_check_read(mlookup);
	if (retval != 0)
	{
		TAILQ_REMOVE(&partition->mlookup_list, mlookup, l_list);
		map_lookup_free(mlookup);
		return NULL;
	}

	lookup_start =  BLOCK_BLOCKNR(mlookup->next_block);
	lookup_bid =  BLOCK_BID(mlookup->next_block);
	if (!lookup_start)
		return mlookup;

	while (lookup_start)
	{
		struct map_lookup *new;

		/* So what do we do in case of errors of load */	
		new = map_lookup_load(partition, lookup_bid, lookup_start);
		if (!new)
			return NULL;

		if (new->next_block)
		{
			lookup_start =  BLOCK_BLOCKNR(new->next_block);
			lookup_bid =  BLOCK_BID(new->next_block);
			map_lookup_free(new);
			continue;
		}
		map_lookup_insert_after(mlookup, new);
		return new;
	}

	return NULL;
}

static struct map_lookup *
__map_lookup_load(struct tape_partition *partition, uint64_t b_start, uint32_t bid)
{
	struct map_lookup *mlookup;
	int retval;
	struct bdevint *bint;

	bint = bdev_find(bid);
	if (!bint) {
		debug_warn("Cannot find bdev at at bid %u !!!\n", bid);
		return NULL;
	}

	mlookup = __uma_zalloc(map_lookup_cache, Q_WAITOK | Q_ZERO, sizeof(*mlookup));
	mlookup->metadata = vm_pg_alloc(0);
	if (unlikely(!mlookup->metadata)) {
		uma_zfree(map_lookup_cache, mlookup);
		return NULL;
	}

	mlookup->map_lookup_wait = wait_chan_alloc("map lookup wait");
	mlookup->partition = partition;
	mlookup->b_start = b_start;
	mlookup->bint = bint;
	atomic_set(&mlookup->refs, 1);

	atomic_set_bit(META_IO_READ_PENDING, &mlookup->flags);
	retval = map_lookup_io(mlookup, QS_IO_READ);
	if (unlikely(retval != 0)) {
		map_lookup_put(mlookup);
		return NULL;
	}

	return mlookup;
}

struct map_lookup *
map_lookup_load(struct tape_partition *partition, uint64_t b_start, uint32_t bid)
{
	struct map_lookup *mlookup;
	int retval;

	mlookup = __map_lookup_load(partition, b_start, bid);
	if (unlikely(!mlookup))
		return NULL;

	retval = map_lookup_read_meta(mlookup);
	if (retval != 0) {
		map_lookup_free(mlookup);
		return NULL;
	}
	return mlookup;
}

struct map_lookup *
map_lookup_new(struct tape_partition *partition, uint64_t l_ids_start, uint64_t f_ids_start, uint64_t s_ids_start, uint64_t b_start, struct bdevint *bint)
{
	struct map_lookup *mlookup;

	mlookup = __uma_zalloc(map_lookup_cache, Q_WAITOK | Q_ZERO, sizeof(*mlookup));
	mlookup->metadata = vm_pg_alloc(VM_ALLOC_ZERO);
	if (unlikely(!mlookup->metadata)) {
		uma_zfree(map_lookup_cache, mlookup);
		return NULL;
	}

	mlookup->map_lookup_wait = wait_chan_alloc("map lookup wait");
	mlookup->partition = partition;
	mlookup->b_start = b_start;
	mlookup->bint = bint;
	mlookup->l_ids_start = l_ids_start;
	mlookup->f_ids_start = f_ids_start;
	mlookup->s_ids_start = s_ids_start;
	debug_check(!mlookup->bint);
	atomic_set_bit(META_DATA_NEW, &mlookup->flags);
	atomic_set_bit(META_DATA_LOADED, &mlookup->flags);
	atomic_set(&mlookup->refs, 1);
	return mlookup;	
}

int
map_lookup_write_eod(struct blk_map *map)
{
	struct map_lookup *mlookup;
	int retval, map_nrs;
	uint64_t next_block;

	debug_check(!map);
	mlookup = map->mlookup;
	next_block = mlookup->next_block;
	map_nrs = mlookup->map_nrs;

	mlookup->next_block = 0;
	mlookup->map_nrs = map->mlookup_entry_id + 1;
	mlookup->pending_new_maps = 0;
	atomic_set_bit(META_IO_PENDING, &mlookup->flags);
	retval = map_lookup_flush_meta(mlookup);
	map_lookup_free_from_mlookup(mlookup->partition, mlookup_get_next(mlookup));
	if (unlikely(retval != 0)) {
		mlookup->next_block = next_block;
		mlookup->map_nrs = map_nrs;
	}
	return retval;
} 

void
map_lookup_entry_update(struct blk_map *map)
{
	struct map_lookup *mlookup = map->mlookup;
	struct map_lookup_entry *entry;

	entry = map_lookup_get_entry(mlookup, map->mlookup_entry_id);
	MENTRY_SET_LID_INFO(entry, map->l_ids_start, map->f_ids, map->s_ids);
	SET_BLOCK(entry->block, map->b_start, map->bint->bid);
	atomic_set_bit(META_IO_PENDING, &mlookup->flags);
}

void
map_lookup_link(struct map_lookup *last, struct map_lookup *mlookup)
{
	struct tape_partition *partition = mlookup->partition;

	debug_check(!partition);
	SET_BLOCK(mlookup->prev_block, last->b_start, last->bint->bid);
	SET_BLOCK(last->next_block, mlookup->b_start, mlookup->bint->bid);
	atomic_set_bit(META_IO_PENDING, &last->flags);
}

void
map_lookup_add_map(struct map_lookup *mlookup, struct blk_map *map)
{
	uint16_t entry_id;

	entry_id = mlookup->map_nrs;
	debug_check(!mlookup->pending_new_maps);
	mlookup->pending_new_maps--;
	mlookup->map_nrs++;
	map->mlookup_entry_id = entry_id;
	map_lookup_entry_update(map);
	atomic_set_bit(META_IO_PENDING, &mlookup->flags);
}

struct map_lookup_entry *
map_lookup_first_blkmap(struct map_lookup *mlookup)
{
	struct map_lookup_entry *entry;

	entry = map_lookup_get_entry(mlookup, 0);
	if (!entry->block)
		return NULL;
	return entry;
}

struct map_lookup_entry *
map_lookup_last_blkmap(struct map_lookup *mlookup)
{
	struct map_lookup_entry *entry;

	if (!mlookup->map_nrs)
		return NULL;

	entry = map_lookup_get_entry(mlookup, (mlookup->map_nrs - 1));
	debug_check(!entry->block);
	if (unlikely(!entry->block))
		return NULL;

	return entry;
}

struct map_lookup_entry*
map_lookup_prev_entry(struct map_lookup *mlookup, uint16_t entry_id, struct map_lookup **ret_lookup, uint16_t *prev_entry_id)
{
	struct map_lookup_entry *prev;
	struct map_lookup *prev_mlookup;

	if (entry_id) {
		prev = map_lookup_get_entry(mlookup, entry_id - 1);
		*ret_lookup = mlookup;
		*prev_entry_id = entry_id - 1;
		return prev;
	}

	if (!mlookup->prev_block)
		return NULL;

	prev_mlookup = map_lookup_load_prev(mlookup);
	if (!prev_mlookup)
		return NULL;

	*ret_lookup = prev_mlookup;
	*prev_entry_id = prev_mlookup->map_nrs - 1;
	return map_lookup_get_entry(prev_mlookup, *prev_entry_id);
}

struct map_lookup_entry *
map_lookup_next_entry(struct map_lookup *mlookup, uint16_t mlookup_entry_id, struct map_lookup **ret, uint16_t *next_entry_id)
{
	struct map_lookup *tmp;

	if (mlookup_entry_id < (mlookup->map_nrs - 1)) {
		*ret = mlookup;
		*next_entry_id = mlookup_entry_id + 1;
		return map_lookup_get_entry(mlookup, (mlookup_entry_id + 1));
	} else {
		tmp = map_lookup_load_next(mlookup);
		if (!tmp || !tmp->map_nrs) {
			if (tmp && !tmp->map_nrs)
				debug_warn("tmp map nrs is zero\n");
			return NULL;
		}
		*ret = tmp;
		*next_entry_id = 0;
		return map_lookup_get_entry(tmp, 0);
	}
}

static struct map_lookup_entry *
map_lookup_locate_entry(struct map_lookup *mlookup, uint64_t block_address, uint16_t *ret_entry_id)
{
	struct map_lookup_entry *entry, *prev = NULL;
	int i;

	if (!mlookup->map_nrs)
		return NULL;

	entry  = map_lookup_get_entry(mlookup, 0);
	for (i = 0; i < mlookup->map_nrs; i++, entry++) {
		if (MENTRY_LID_START(entry) > block_address) {
			debug_check(!prev);
			*ret_entry_id = (i - 1);
			return prev;
		}
		prev = entry;
	}
	*ret_entry_id = (i - 1);
	return prev;
}

static struct map_lookup_entry *
map_lookup_locate_file_entry(struct map_lookup *mlookup, uint64_t block_address, uint16_t *ret_entry_id)
{
	struct map_lookup_entry *entry, *prev = NULL;
	int i;
	uint8_t f_ids;
	uint64_t f_ids_start = mlookup->f_ids_start;

	if (!mlookup->map_nrs)
		return NULL;

	entry  = map_lookup_get_entry(mlookup, 0);
	for (i = 0; i < mlookup->map_nrs; i++, entry++) {
		f_ids = MENTRY_FILEMARKS(entry);
		if ((f_ids_start + f_ids) > block_address) {
			*ret_entry_id = i;
			return entry;
		}
		f_ids_start += f_ids;
		prev = entry;
	}
	*ret_entry_id = (i - 1);
	return prev;
}

struct map_lookup_entry *
map_lookup_locate(struct tape_partition *partition, uint64_t block_address, struct map_lookup **ret_mlookup, uint16_t *ret_entry_id)
{
	struct map_lookup_entry *entry = NULL;
	struct map_lookup *mlookup;
	struct map_lookup *next;

	/* We need to squeeze in maps. But for later */
	mlookup = tape_partition_first_mlookup(partition);

	while (mlookup) {
		if (!mlookup->next_block)
			break;

		next = map_lookup_load_next(mlookup);
		if (!next)
			return NULL;

		if (next->l_ids_start > block_address)
			break;

		mlookup = next;
	}

	entry = map_lookup_locate_entry(mlookup, block_address, ret_entry_id);
	if (!entry && mlookup->map_nrs) {
		entry = map_lookup_last_blkmap(mlookup);
		*ret_entry_id = (mlookup->map_nrs - 1);
	}
	debug_check(!entry);
	if (unlikely(!entry))
		return NULL;
	*ret_mlookup = mlookup;
	return entry;
}

struct map_lookup_entry *
map_lookup_locate_file(struct tape_partition *partition, uint64_t block_address, struct map_lookup **ret_mlookup, uint16_t *ret_entry_id)
{
	struct map_lookup_entry *entry = NULL;
	struct map_lookup *mlookup;
	struct map_lookup *next;

	/* We need to squeeze in maps. But for later */
	mlookup = tape_partition_first_mlookup(partition);

	while (mlookup) {
		if (!mlookup->next_block)
			break;

		next = map_lookup_load_next(mlookup);
		if (!next)
			return NULL;

		if (next->f_ids_start > block_address)
			break;

		mlookup = next;
	}

	entry = map_lookup_locate_file_entry(mlookup, block_address, ret_entry_id);
	if (!entry && mlookup->map_nrs) {
		entry = map_lookup_last_blkmap(mlookup);
		*ret_entry_id = (mlookup->map_nrs - 1);
	}
	debug_check(!entry);
	if (unlikely(!entry))
		return NULL;
	*ret_mlookup = mlookup;
	return entry;
}

struct map_lookup_entry *
map_lookup_space_forward(struct tape_partition *partition, uint8_t code, int *count, int *error, struct map_lookup **ret_lookup, uint16_t *ret_entry_id)
{
	struct blk_map *map = partition->cur_map;
	struct map_lookup *mlookup, *next_mlookup;
	struct map_lookup_entry *entry, *next;
	uint16_t entry_id, next_entry_id;
	uint64_t lids, fids, sids;
	int todo = *count;

	mlookup = map->mlookup;
	entry_id = map->mlookup_entry_id;
	entry = map_lookup_next_entry(mlookup, entry_id, &next_mlookup, &next_entry_id);
	if (!entry) {
		if (!mlookup->next_block)
			*error = EOD_REACHED;
		else
			*error = MEDIA_ERROR;
		return NULL;
	}

	mlookup = next_mlookup;
	entry_id = next_entry_id;
	while (entry) {
		entry = map_lookup_get_entry(mlookup, entry_id);
		if (!todo)
			goto out;
		next = map_lookup_next_entry(mlookup, entry_id, &next_mlookup, &next_entry_id);
		if (!next) {
			if (mlookup->next_block) {
				*error = MEDIA_ERROR;
			}
			goto out;
		}

		lids = MENTRY_LID_START(next) - MENTRY_LID_START(entry);
		fids = MENTRY_FILEMARKS(entry);
		sids = MENTRY_SETMARKS(entry);
		switch (code) {
		case SPACE_CODE_BLOCKS:
			if (fids || sids || lids > todo)
				goto out;
			todo -= lids;
			break;
		case SPACE_CODE_FILEMARKS:
			if (sids || fids > todo)
				goto out;
			todo -= fids;
			break;
		case SPACE_CODE_SETMARKS:
			if (sids > todo)
				goto out;
			todo -= sids;
			break;
		}
		entry = next;
		entry_id = next_entry_id;
		mlookup = next_mlookup;
	}
out:
	*ret_lookup = mlookup;
	*ret_entry_id = entry_id;
	*count = todo;
	return entry;
}

struct map_lookup_entry *
map_lookup_space_backward(struct tape_partition *partition, uint8_t code, int *count, int *error, struct map_lookup **ret_lookup, uint16_t *ret_entry_id)
{
	struct blk_map *map = partition->cur_map;
	struct map_lookup *mlookup, *prev_mlookup;
	struct map_lookup_entry *entry, *prev;
	uint16_t entry_id, prev_entry_id;
	uint64_t lids, fids, sids;
	int todo = *count;

	mlookup = map->mlookup;
	entry_id = map->mlookup_entry_id;
	entry = map_lookup_prev_entry(mlookup, entry_id, &prev_mlookup, &prev_entry_id);
	if (!entry) {
		if (!mlookup->l_ids_start)
			*error = BOM_REACHED;
		else
			*error = MEDIA_ERROR;
		return NULL;
	}

	mlookup = prev_mlookup;
	entry_id = prev_entry_id;
	while (entry) {
		entry = map_lookup_get_entry(mlookup, entry_id);
		if (!todo)
			goto out;
		prev = map_lookup_prev_entry(mlookup, entry_id, &prev_mlookup, &prev_entry_id);
		if (!prev) {
			if (mlookup->l_ids_start) {
				*error = MEDIA_ERROR;
			}
			goto out;
		}

		lids = MENTRY_LID_START(entry) - MENTRY_LID_START(prev);
		fids = MENTRY_FILEMARKS(entry);
		sids = MENTRY_SETMARKS(entry);
		switch (code) {
		case SPACE_CODE_BLOCKS:
			if (fids || sids || lids > todo)
				goto out;
			todo -= lids;
			break;
		case SPACE_CODE_FILEMARKS:
			if (sids || fids > todo)
				goto out;
			todo -= fids;
			break;
		case SPACE_CODE_SETMARKS:
			if (sids > todo)
				goto out;
			todo -= sids;
			break;
		}
		entry = prev;
		entry_id = prev_entry_id;
		mlookup = prev_mlookup;
	}
out:
	*ret_lookup = mlookup;
	*ret_entry_id = entry_id; 
	*count = todo;
	return entry;
}

void
map_lookup_get_ids_start(struct map_lookup *mlookup, uint16_t entry_id, uint64_t *ret_f_ids_start, uint64_t *ret_s_ids_start)
{
	struct map_lookup_entry *entry;
	uint64_t f_ids_start = mlookup->f_ids_start;
	uint64_t s_ids_start = mlookup->s_ids_start;
	int i;

	entry = map_lookup_get_entry(mlookup, 0);
	for (i = 0; i < entry_id; i++, entry++) {
		f_ids_start += MENTRY_FILEMARKS(entry);
		s_ids_start += MENTRY_SETMARKS(entry);
	}
	*ret_f_ids_start = f_ids_start;
	*ret_s_ids_start = s_ids_start;
}


void
map_lookup_resync_ids(struct map_lookup *mlookup)
{
	struct map_lookup_entry *entry;
	uint64_t f_ids = 0;
	uint64_t s_ids = 0;
	int i;

	entry = map_lookup_get_entry(mlookup, 0);
	for (i = 0; i < mlookup->map_nrs; i++, entry++) {
		f_ids += MENTRY_FILEMARKS(entry);
		s_ids += MENTRY_SETMARKS(entry);
	}

	mlookup->f_ids = f_ids;
	mlookup->s_ids = s_ids;
}

void
map_lookup_insert(struct tape_partition *partition, struct map_lookup *mlookup)
{
	TAILQ_INSERT_TAIL(&partition->mlookup_list, mlookup, l_list);
	partition->mlookup_count++;
}


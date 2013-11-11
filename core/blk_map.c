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

#include "blk_map.h"
#include "map_lookup.h"
#include "mchanger.h"
#include "qs_lib.h"
#include "gdevq.h"

extern uma_t *bentry_cache;
extern uma_t *bmap_cache;

static uint64_t
blk_map_bstart(struct blk_map *map)
{
	return map->b_start;
}

static struct bdevint *
blk_map_bint(struct blk_map *map)
{
	return map->bint;
}

static void
blk_map_get(struct blk_map *map)
{
	atomic_inc(&map->refs);
}

static void
cache_data_incr(struct blk_map *map, uint32_t size)
{
	map->cached_data += size;
	map->partition->cached_data += size;
	map->partition->cached_blocks++;
}

static void
cache_data_decr(struct blk_map *map, uint32_t size)
{
	map->cached_data -= size;
	map->partition->cached_data -= size;
	map->partition->cached_blocks--;
	debug_check(map->cached_data < 0);
	debug_check(map->partition->cached_data < 0);
	debug_check(map->partition->cached_blocks < 0);
}

static void
blk_entry_free_data(struct blk_entry *entry)
{
	uint32_t size = 0;

	if (entry->cpglist) {
		pglist_free(entry->cpglist, entry->cpglist_cnt);
		entry->cpglist = NULL;
	}

	if (entry->pglist) {
		pglist_free(entry->pglist, entry->pglist_cnt);
		entry->pglist = NULL;
	}

	if (entry->tcache) {
		size = entry->comp_size ? entry->comp_size : entry->block_size;
		cache_data_decr(entry->map, size);
		tcache_put(entry->tcache);
		entry->tcache = NULL;
	}
}

static void
blk_entry_free(struct blk_entry *entry)
{
	blk_entry_free_data(entry);
	uma_zfree(bentry_cache, entry);
}

static void
blk_entry_free_all(struct blkentry_list *entry_list)
{
	struct blk_entry *entry;

	while ((entry = TAILQ_FIRST(entry_list)) != NULL) {
		TAILQ_REMOVE(entry_list, entry, e_list);
		blk_entry_free(entry);
	}
}

static void
blk_map_free(struct blk_map *map)
{
	blk_entry_free_all(&map->entry_list);
	tcache_list_wait(&map->tcache_list);
	map_lookup_put(map->mlookup);
	if (map->metadata)
		vm_pg_free(map->metadata);
	wait_chan_free(map->blk_map_wait);
	uma_zfree(bmap_cache, map);
}

static void
blk_map_put(struct blk_map *map)
{
	if (atomic_dec_and_test(&map->refs))
		blk_map_free(map);
}

static struct blk_map * 
blk_map_alloc(struct map_lookup *mlookup)
{
	struct blk_map *map;

	map = __uma_zalloc(bmap_cache, Q_WAITOK|Q_ZERO, sizeof(*map));
	TAILQ_INIT(&map->entry_list);
	SLIST_INIT(&map->tcache_list);
	map->blk_map_wait = wait_chan_alloc("blk map wait");
	map_lookup_get(mlookup);
	map->mlookup = mlookup;
	atomic_set(&map->refs, 1);
	return map;
}

static struct blk_entry *
blk_entry_new(void)
{
	struct blk_entry *entry;

	entry = __uma_zalloc(bentry_cache, Q_WAITOK | Q_ZERO, sizeof(*entry));
	if (unlikely(!entry)) {
		debug_warn("UMA allocation failure\n");
		return NULL;
	}
	return entry;
}

static void
blk_map_entry_free_from_entry(struct blk_map *map, struct blk_entry *entry)
{
	struct blk_entry *next;

	while (entry) {
		next = TAILQ_NEXT(entry, e_list);
		TAILQ_REMOVE(&map->entry_list, entry, e_list);
		blk_entry_free(entry);
		entry = next;
	}
}

struct blk_map *
blk_map_new(struct tape_partition *partition, struct map_lookup *mlookup, uint64_t l_ids_start, uint64_t b_start, struct bdevint *bint, uint32_t segment_id)
{
	struct blk_map *map;

	map = blk_map_alloc(mlookup);
	map->partition = partition;
	map->metadata = vm_pg_alloc(VM_ALLOC_ZERO);
	if (unlikely(!map->metadata)) {
		debug_warn("Unable to allocate map's metadata\n");
		blk_map_put(map);
		return NULL;
	}

	map->b_start = b_start;
	map->bint = bint;
	map->l_ids_start = l_ids_start;
	map->segment_id = segment_id;
	atomic_set_bit(META_DATA_NEW, &map->flags);
	atomic_set_bit(META_DATA_LOADED, &map->flags);
	return map;
}

#ifdef FREEBSD 
void static blk_map_end_bio(bio_t *bio)
#else
void static blk_map_end_bio(bio_t *bio, int err)
#endif
{
	struct blk_map *blk_map = (struct blk_map *)bio_get_caller(bio);
#ifdef FREEBSD
	int err = bio->bio_error;
#endif

	if (unlikely(err))
		atomic_set_bit(META_DATA_ERROR, &blk_map->flags);

	if (bio_get_command(bio) != QS_IO_READ)
		atomic_clear_bit(META_DATA_DIRTY, &blk_map->flags);
	else
		atomic_clear_bit(META_DATA_READ_DIRTY, &blk_map->flags);

	chan_wakeup(blk_map->blk_map_wait);
	blk_map_put(blk_map);
	g_destroy_bio(bio);
}

static int
blk_map_io(struct blk_map *blk_map, int rw)
{
	int retval;

	if (rw != QS_IO_READ) {
		debug_check(atomic_test_bit(META_DATA_DIRTY, &blk_map->flags));
		atomic_set_bit(META_DATA_DIRTY, &blk_map->flags);
		atomic_clear_bit(META_IO_PENDING, &blk_map->flags);
		atomic_clear_bit(META_DATA_CLONED, &blk_map->flags);
	}
	else {
		atomic_set_bit(META_DATA_READ_DIRTY, &blk_map->flags);
	}

	blk_map_get(blk_map);
	retval = qs_lib_bio_page(blk_map->bint, blk_map->b_start, LBA_SIZE, blk_map->metadata, blk_map_end_bio, blk_map, rw, 0);
	if (unlikely(retval != 0)) {
		debug_warn("Failed to load amap table at %llu bid %u\n", (unsigned long long)blk_map_bstart(blk_map), blk_map_bint(blk_map)->bid);
		atomic_clear_bit(META_DATA_DIRTY, &blk_map->flags);
		atomic_clear_bit(META_DATA_READ_DIRTY, &blk_map->flags);
		atomic_set_bit(META_DATA_ERROR, &blk_map->flags);
		chan_wakeup(blk_map->blk_map_wait);
		blk_map_put(blk_map);
	}
	return retval;
}

static int
blk_map_meta_valid(struct blk_map *map)
{
	struct raw_blk_map *raw_map;
	uint16_t csum;

	raw_map = (struct raw_blk_map *)(vm_pg_address(map->metadata) + (LBA_SIZE - sizeof(*raw_map)));
	csum = net_calc_csum16(vm_pg_address(map->metadata), LBA_SIZE - sizeof(*raw_map));
	if (csum != raw_map->csum) {
		debug_warn("Mismatch in csum got %x stored %x\n", csum, raw_map->csum);
		return -1;
	}
	else
		return 0;
}

static void 
blk_map_read_header(struct blk_map *map)
{
	struct raw_blk_map *raw_map;
	struct map_lookup_entry *mlookup_entry = map_lookup_get_entry(map->mlookup, map->mlookup_entry_id);

	raw_map = (struct raw_blk_map *)(vm_pg_address(map->metadata) + (LBA_SIZE - sizeof(*raw_map)));
	map->segment_id = raw_map->segment_id;
	map->nr_entries = raw_map->nr_entries;

	map->f_ids = MENTRY_FILEMARKS(mlookup_entry);
	map->s_ids = MENTRY_SETMARKS(mlookup_entry);
}

static int
blk_map_load_entries(struct blk_map *map)
{
	struct raw_blk_entry *raw_entry;
	struct blk_entry *prev = NULL, *entry; 
	uint64_t lid_start = map->l_ids_start;
	int i;

	raw_entry = (struct raw_blk_entry *)(vm_pg_address(map->metadata));
	for (i = 0; i < map->nr_entries; i++, lid_start++, raw_entry++) {
		entry = blk_entry_new();
		if (unlikely(!entry)) {
			debug_warn("Cannot create a new blk entry\n");
			goto err;
		}
		TAILQ_INSERT_TAIL(&map->entry_list, entry, e_list);

		if (prev && prev->bint->bid == BLOCK_BID(raw_entry->block))
			entry->bint = prev->bint;
		else
			entry->bint = bdev_find(BLOCK_BID(raw_entry->block));

		if (unlikely(!entry->bint)) {
			debug_warn("Cannot locate bint at entry bid %u id %d nr_entries %d\n", BLOCK_BID(raw_entry->block), i, map->nr_entries);
			goto err;
		}

		entry->map = map;
		entry->entry_id = i;
		entry->bits = raw_entry->bits;
		entry->b_start = BLOCK_BLOCKNR(raw_entry->block);
		entry->block_size = entry_block_size(entry);
		entry->comp_size = entry_comp_size(entry);
		entry->lid_start = lid_start;
		prev = entry;
	}

	return 0;
err:
	blk_entry_free_all(&map->entry_list);
	return -1;
}

static int
blk_map_load_meta(struct blk_map *map)
{
	int retval;

	if (atomic_test_bit(META_DATA_LOADED, &map->flags))
		return 0;

	wait_on_chan(map->blk_map_wait, !atomic_test_bit(META_DATA_READ_DIRTY, &map->flags));

	if (atomic_test_bit(META_DATA_ERROR, &map->flags))
		return -1;

	blk_map_read_header(map);
	retval = blk_map_meta_valid(map);
	if (unlikely(retval != 0)) {
		debug_warn("blk map at %llu meta invalid\n", (unsigned long long)map->b_start);
		return -1;
	}

	retval = blk_map_load_entries(map);
	if (unlikely(retval != 0)) {
		debug_warn("blk map at %llu loading blk entries failed\n", (unsigned long long)map->b_start);
		return -1;
	}
	atomic_set_bit(META_DATA_LOADED, &map->flags);
	return 0;
}

static void
blk_map_position_eod(struct blk_map *map)
{
	map->c_entry = NULL;
}

struct blk_map *
blk_map_load(struct tape_partition *partition, uint64_t b_start, uint32_t bid, int async, struct map_lookup *mlookup, uint16_t mlookup_entry_id, int poslast)
{
	struct map_lookup_entry *mlookup_entry;
	struct blk_map *map;
	struct bdevint *bint;
	int retval;

	bint = bdev_find(bid);
	if (unlikely(!bint)) {
		debug_warn("Cannot find bdev at at bid %u !!!\n", bid);
		return NULL;
	}

	map = blk_map_alloc(mlookup);
	map->metadata = vm_pg_alloc(0);
	if (unlikely(!map->metadata)) {
		debug_warn("Unable to allocate map's metadata\n");
		blk_map_put(map);
		return NULL;
	}

	map->partition = partition;
	map->b_start = b_start;
	map->bint = bint;
	map->mlookup_entry_id = mlookup_entry_id;
	mlookup_entry = map_lookup_get_entry(map->mlookup, map->mlookup_entry_id);
	map->l_ids_start = MENTRY_LID_START(mlookup_entry);

	retval = blk_map_io(map, QS_IO_READ);
	if (unlikely(retval != 0)) {
		debug_warn("failed to read map meta at b_start %llu bid %u\n", (unsigned long long)map->b_start, map->bint->bid);
		blk_map_put(map);
		return NULL;
	}

	if (!async) {
		retval = blk_map_load_meta(map);
		if (unlikely(retval != 0)) {
			debug_warn("blk_map_load_meta failed for map at  %llu bid %u\n", (unsigned long long)map->b_start, bint->bid);
			blk_map_put(map);
			return NULL;
		}

		if (!poslast)
			blk_map_position_bop(map);
		else
			blk_map_position_eod(map);
	}

	return map;
}

static void
__blk_map_free_all(struct blkmap_list *map_list)
{
	struct blk_map *map;

	while ((map = TAILQ_FIRST(map_list)) != NULL) {
		TAILQ_REMOVE(map_list, map, m_list);
		blk_map_put(map);
	}
}

void
blk_map_free_all(struct tape_partition *partition)
{
	__blk_map_free_all(&partition->map_list);
}

static void
blk_map_free_till_cur(struct tape_partition *partition)
{
	struct blk_map *map;

	while ((map = TAILQ_FIRST(&partition->map_list)) != NULL) {
		if (map == partition->cur_map)
			break;
		TAILQ_REMOVE(&partition->map_list, map, m_list);
		blk_map_put(map);
	}
	map_lookup_free_till_cur(partition);
}

void
blk_map_free_from_map(struct tape_partition *partition, struct blk_map *map)
{
	struct blk_map *next;

	while (map) {
		next = TAILQ_NEXT(map, m_list);
		TAILQ_REMOVE(&partition->map_list, map, m_list);
		blk_map_put(map);
		map = next;
	}
}

static void
blk_map_flush_entries(struct blk_map *map)
{
	struct blk_entry *entry;

	TAILQ_FOREACH(entry, &map->entry_list, e_list) {
		blk_map_write_entry(map, entry);
	}
}

static void
blk_map_write_header(struct blk_map *map)
{
	struct raw_blk_map *raw_map;
	uint16_t csum;

	debug_check(map->nr_entries > BLK_MAX_ENTRIES);
	raw_map = (struct raw_blk_map *)(vm_pg_address(map->metadata) + (LBA_SIZE - sizeof(*raw_map)));
	csum = net_calc_csum16(vm_pg_address(map->metadata), LBA_SIZE - sizeof(*raw_map));
	raw_map->csum = csum;
	raw_map->segment_id = map->segment_id;
	raw_map->nr_entries = map->nr_entries;
}

static int
blk_map_flush_meta(struct blk_map *map)
{
	int retval;

	if (!atomic_test_bit(META_IO_PENDING, &map->flags))
		return 0;

	wait_on_chan(map->blk_map_wait, !atomic_test_bit(META_DATA_DIRTY, &map->flags));
	if (atomic_test_bit(META_DATA_ERROR, &map->flags) || atomic_test_bit(CACHE_DATA_ERROR, &map->flags))
		return -1;

	blk_map_flush_entries(map);
	blk_map_write_header(map);
	retval = blk_map_io(map, QS_IO_SYNC);
	return retval;
}

static int
blk_map_next_mlookup_avail(struct blk_map *map)
{
	int avail = 1;
	if (map->mlookup_entry_id == (map->mlookup->map_nrs - 1)) {

		avail = map_lookup_next_avail(map->mlookup);
		if (!avail)
			map_lookup_load_next_async(map->mlookup);
	}
	return avail;
}

struct blk_map *
blk_maps_readahead(struct tape_partition *partition, int count)
{
	struct blk_map *prev;
	struct blk_map *next = NULL;
	int i;
	struct map_lookup *ret_lookup;
	struct map_lookup_entry *entry;
	uint16_t entry_id;

	prev = tape_partition_last_map(partition);
	for (i = 0; i < count; i++) {
		struct blk_map *tmp;

		if (next && !blk_map_next_mlookup_avail(prev))
		{
			break;
		}

		if (!map_lookup_map_has_next(prev))
			break;

		entry = map_lookup_next_entry(prev->mlookup, prev->mlookup_entry_id, &ret_lookup, &entry_id);
		if (!entry)
		{
			break;
		}

		tmp = blk_map_load(partition, BLOCK_BLOCKNR(entry->block), BLOCK_BID(entry->block), 1, ret_lookup, entry_id, 0);

		if (!next)
			next = tmp;

		if (!tmp)
			break;

		blk_map_insert(partition, tmp);
		prev = tmp;
	}
	return next;
}

struct blk_map *
blk_map_set_next_map(struct blk_map *map, int set_cur)
{
	struct blk_map *next = NULL;
	struct tape_partition *partition = map->partition;
	int retval;

	if (unlikely(!map_lookup_map_has_next(map))) {
		return NULL;
	}

	next = blk_map_get_next(map);
	if (unlikely(!next)) {
	 	if (unlikely(!map_lookup_map_has_next(map)))
			return NULL;
		next = blk_maps_readahead(partition, MIN_MAP_READ_AHEAD);
	}

	if (unlikely(!next)) {
		return NULL;
	}

	retval = blk_map_load_meta(next);
	if (unlikely(retval != 0)) {
		debug_warn("blk_map_load_meta failed for map at %llu, bid %u\n", (unsigned long long)next->b_start, next->bint->bid);
		blk_map_free_from_map(partition, next);
		return NULL;
	}

	blk_map_position_bop(next);
	if (set_cur)
		tape_partition_set_cur_map(partition, next);
	return next;
}

static struct blk_map *
blk_map_set_prev(struct blk_map *map)
{
	struct map_lookup_entry *prev;
	struct map_lookup *rel;
	struct blk_map *tmap;
	struct tape_partition *partition = map->partition;
	uint16_t entry_id;

	prev = map_lookup_prev_entry(map->mlookup, map->mlookup_entry_id, &rel, &entry_id);
	if (!prev) {
		debug_warn("Cannot locate previous map\n");
		return NULL;
	}

	tmap = blk_map_load(partition, BLOCK_BLOCKNR(prev->block), BLOCK_BID(prev->block), 0, rel, entry_id, 1);
	if (!tmap) {
		debug_warn("failed to load prev map\n");
		return NULL;
	}

	return tmap;
}

int
blk_map_locate_file(struct blk_map *map, uint64_t block_address)
{
	uint64_t f_ids_start, s_ids_start;
	struct blk_entry *entry;

	map_lookup_get_ids_start(map->mlookup, map->mlookup_entry_id, &f_ids_start, &s_ids_start);

	debug_check(TAILQ_EMPTY(&map->entry_list));
	TAILQ_FOREACH(entry, &map->entry_list, e_list) {
		if (!entry_is_filemark(entry))
			continue;
		if (f_ids_start == block_address) {
			map->c_entry = entry;
			return 0;
		}
		f_ids_start++;
	}
	map->c_entry = NULL;
	return EOD_REACHED;
}

int
blk_map_locate(struct blk_map *map, uint64_t block_address)
{
	struct blk_entry *entry;

	debug_check(TAILQ_EMPTY(&map->entry_list));
	TAILQ_FOREACH(entry, &map->entry_list, e_list) {
		if (entry->lid_start == block_address) {
			map->c_entry = entry;
			return 0;
		}
	}

	map->c_entry = NULL;
	entry = blk_map_last_entry(map);
	if (entry && (block_address == (entry->lid_start + 1)))
		return 0;
	else
		return EOD_REACHED;
}

void
blk_map_read_position(struct tape_partition *partition, struct tape_position_info *info)
{
	struct blk_map *map = partition->cur_map;
	struct blk_entry *entry;
	uint64_t f_id_count = 0;
	uint64_t s_id_count = 0;
	uint32_t cached_blocks, cached_data;

	if (atomic_test_bit(PARTITION_DIR_WRITE, &partition->flags)) {
		cached_blocks = partition->cached_blocks;
		cached_data = partition->cached_data;
	}
	else {
		cached_blocks = cached_data = 0;
	}

	if (!map) {
		info->bop = 1;
		goto skip;
	}

	if (!map->l_ids_start && map->c_entry && !map->c_entry->entry_id)
		info->bop = 1;
	else if (!map_lookup_map_has_next(map) && !map->c_entry)
		info->eop = 1;

	map_lookup_get_ids_start(map->mlookup, map->mlookup_entry_id, &f_id_count, &s_id_count);

	TAILQ_FOREACH(entry, &map->entry_list, e_list) {
		if (entry_is_filemark(entry))
			f_id_count++;
		else if (entry_is_setmark(entry))
			s_id_count++;
		if (entry == map->c_entry)
			break;
	}

skip:
	info->block_number = blk_map_current_lid(map);
	info->file_number = f_id_count;
	info->set_number = s_id_count;
	info->blocks_in_buffer = cached_blocks;
	info->bytes_in_buffer = cached_data;
}

static inline struct blk_entry *
blk_entry_read_next(struct blk_entry *entry, struct blk_map **end_map, int load, int readahead)
{
	struct tape_partition *partition;
	struct blk_entry *next;
	struct blk_map *map;
	int retval;

	partition = entry->map->partition;
	next = blk_entry_get_next(entry);
	if (next)
		return next;

	map = blk_map_get_next(entry->map);
	if (!map) {
		if (!load && !readahead)
			return NULL;
		if (readahead) {
			blk_maps_readahead(partition, MIN_MAP_READ_AHEAD);
			return 0;
		}
		goto load_next;
	}

	if (atomic_test_bit(META_DATA_READ_DIRTY, &map->flags)) {
		if (readahead)
			return NULL;
	}

	retval = blk_map_load_meta(map);
	if (unlikely(retval != 0)) {
		debug_warn("blk_map_load_meta failed for map at %llu, bid %u\n", (unsigned long long)map->b_start, map->bint->bid);
		blk_map_free_from_map(partition, map);
		return NULL;
	}

	*end_map = map;
	return blk_map_first_entry(map);

load_next:
	map = blk_map_set_next_map(entry->map, 0);
	if (unlikely(!map))
		return NULL;

	*end_map = map;
	if (unlikely(TAILQ_EMPTY(&map->entry_list))) {
		debug_warn("next map has empty list map b_start %llu bid %u nr_entries %d\n", (unsigned long long)map->b_start, map->bint->bid, map->nr_entries);
		return NULL;
	}
	return blk_map_first_entry(map);
}

static int
blk_entry_add_to_tcache(struct tcache *tcache, struct blk_map *map, struct blk_entry *entry, int rw)
{
	int i, pglist_cnt, retval;
	uint64_t b_start;
	struct pgdata **pglist, *pgdata;
	uint32_t sector_size = (1U << entry->bint->sector_shift);
	uint32_t write_size, todo;

	if (entry->comp_size) {
		write_size = entry->comp_size;
		pglist = entry->cpglist;
		pglist_cnt = entry->cpglist_cnt;
		debug_check(!pglist);
	} else {
		write_size = entry->block_size;
		pglist = entry->pglist;
		pglist_cnt = entry->pglist_cnt;
		debug_check(!pglist);
		if (!pglist) {
			printf("Invalid entry entry id %d b_start %llu lid_start %llu data block %d file mark %d set mark %d\n", entry->entry_id, (unsigned long long)entry->b_start, (unsigned long long)entry->lid_start, entry_is_data_block(entry), entry_is_filemark(entry), entry_is_setmark(entry));
		}
	}

	if (unlikely(!pglist))
		return -1;
	debug_check(!tcache);
	if (unlikely(!tcache))
		return -1;
	b_start = entry->b_start;
	for (i = 0; i < pglist_cnt; i++) {
		pgdata = pglist[i];
		if ((i + 1) != pglist_cnt)
			todo = LBA_SIZE;
		else {
			todo = write_size & LBA_MASK;
			if (!todo)
				todo = LBA_SIZE;
			else
				todo = align_size(todo, sector_size);
		}

		retval = tcache_add_page(tcache, pgdata->page, b_start, entry->bint, todo, rw);
		if (unlikely(retval != 0)) {
			tcache_put(tcache);
			return -1;
		}
		b_start += (todo >> entry->bint->sector_shift);
	}

	tcache_get(tcache);
	debug_check(entry->tcache);
	entry->tcache = tcache;
	return 0;
}

static int
blk_map_setup_read(struct blk_entry *entry)
{
	struct blk_map *map;
	struct blk_entry *start = entry;
	struct tcache *tcache;
	int read_size, entry_pglist_cnt, retval;
	struct pgdata **entry_pglist;
	int pages;

	if (entry->cpglist || entry->pglist)
		return 0;

	map = entry->map;
	pages = max_t(int, 128, pgdata_get_count(entry->block_size, 1));
	tcache = tcache_alloc(pages);
	while (entry && entry_is_data_block(entry)) {
		if (entry->comp_size && entry->cpglist) {
			debug_check(!entry->tcache);
			entry = blk_entry_get_next(entry);
			continue;
		}
		else if (entry->pglist) {
			debug_check(!entry->tcache);
			entry = blk_entry_get_next(entry);
			continue;
		}
		read_size = entry->comp_size ? entry->comp_size : entry->block_size; 
		entry_pglist_cnt = pgdata_get_count(read_size, 1);
		if ((atomic_read(&tcache->bio_remain) + entry_pglist_cnt) > pages)
			break;

		entry_pglist = pgdata_allocate(read_size, 1, &entry_pglist_cnt, Q_NOWAIT, 1);
		if (!entry_pglist)
			goto err;


		if (entry->comp_size) {
			entry->cpglist = entry_pglist;
			entry->cpglist_cnt = entry_pglist_cnt;
		}
		else {
			entry->pglist = entry_pglist;
			entry->pglist_cnt = entry_pglist_cnt;
		}
		retval = blk_entry_add_to_tcache(tcache, map, entry, QS_IO_READ);
		if (retval != 0)
			goto err;
		cache_data_incr(map, read_size);
		if (map->partition->cached_data > PARTITION_READ_CACHE_MAX)
			break;
		if (tcache->size > TCACHE_MAX_SIZE)
			break;

		entry = blk_entry_get_next(entry);
	}

	debug_check(!atomic_read(&tcache->bio_remain));
	SLIST_INSERT_HEAD(&map->tcache_list, tcache, t_list);
	tcache_entry_rw(tcache, QS_IO_READ);
	return 0;
err:
	while (start) {
		blk_entry_free_data(start);
		if (start == entry)
			break;
		start = blk_entry_get_next(start);
	}
	tcache_put(tcache);
	return -1;
}

static void 
blk_map_readahead(struct blk_entry *entry)
{
	struct blk_map *map;
	struct tape_partition *partition;
	int retval;

	if (!entry || entry_is_filemark(entry))
		return;

	map = entry->map;
	partition = map->partition;
	if (partition->cached_data > PARTITION_READ_CACHE_MIN)
		return;

	entry = blk_entry_read_next(entry, &map, 0, 1);
	while (entry && partition->cached_data <= PARTITION_READ_CACHE_MAX) {
		if (unlikely(!entry_is_data_block(entry)))
			break;

		retval = blk_map_setup_read(entry);
		if (retval != 0)
			break;

		entry = blk_entry_read_next(entry, &map, 0, 1);
	}
}

static int
__blk_map_read(struct blk_map *map, uint32_t read_block_size, uint32_t needed_blocks, int *pglist_cnt, uint32_t *data_blocks, int *error, uint8_t fixed, uint32_t *ili_block_size)
{
	struct blk_map *end_map;
	struct blk_entry *entry;
	struct tape_partition *partition;

	debug_check(!map);
 	partition = map->partition;

	debug_check(atomic_test_bit(META_IO_READ_PENDING, &map->flags));
	debug_check(atomic_test_bit(META_DATA_READ_DIRTY, &map->flags));

	if (atomic_test_bit(META_DATA_ERROR, &map->flags))
		return -1;

	if (!map->c_entry && !map_lookup_map_has_next(map)) {
		*error = EOD_REACHED;
		return 0;
	}

	if (!map->c_entry) {
		struct blk_map *next;

		if (!map_lookup_map_has_next(map)) {
			*error = EOD_REACHED;
			return 0;
		}

		next = blk_map_set_next_map(map, 1);
		if (unlikely(!next))
			return -1;

		return __blk_map_read(next, read_block_size, needed_blocks, pglist_cnt, data_blocks, error, fixed, ili_block_size);
	}

	entry = map->c_entry;
	end_map = map;
	debug_check(!entry);

	while ((needed_blocks != *data_blocks) && !(*error)) {

		debug_check(!entry);
		debug_check(!end_map);

		if (unlikely(!entry_is_data_block(entry))) {
			debug_check(end_map != entry->map);
			if (entry_is_filemark(entry))
				*error = FILEMARK_ENCOUNTERED;
			else
				*error = SETMARK_ENCOUNTERED;
			entry = blk_entry_read_next(entry, &end_map, 1, 0);
			break;
		}

		blk_map_setup_read(entry);
		if (entry->block_size != read_block_size) {
			if (entry->block_size < read_block_size)
				*error = UNDERLENGTH_COND_ENCOUNTERED;
			else
				*error = OVERLENGTH_COND_ENCOUNTERED;
			*ili_block_size = entry->block_size;
			entry = blk_entry_read_next(entry, &end_map, 1, 0);
			break;
		}
		else
			(*data_blocks)++;

		entry = blk_entry_read_next(entry, &end_map, 1, 0);
		if (!entry)
			break;
	}

	if (needed_blocks != *data_blocks && !(*error))
		*error = EOD_REACHED;

	debug_check(!end_map);
	end_map->c_entry = entry;
	tape_partition_set_cur_map(partition, end_map);
	return 0;
}

static void
entry_pglist_map(struct pgdata **entry_pglist, int *entry_idx, int entry_pglist_cnt, struct pgdata **src_pglist, int *src_idx, int src_pglist_cnt)
{
	int i;
	int j;
	struct pgdata *src_pgdata, *entry_pgdata;

	for (i = *entry_idx, j = *src_idx; i < entry_pglist_cnt && j < src_pglist_cnt; i++, j++) {
		src_pgdata = src_pglist[j];
		entry_pgdata = entry_pglist[i];
		pgdata_add_ref(entry_pgdata, src_pgdata);
	}
	*entry_idx = i;
	*src_idx = j;
}

static int
blk_entry_uncompress(struct blk_entry *entry)
{
	pagestruct_t **cpages, **upages;
	uint8_t *uaddr = NULL, *caddr = NULL;
	int retval;

	if (entry->pglist)
		return 0;

	entry->pglist = pgdata_allocate(entry->block_size, 1, &entry->pglist_cnt, Q_NOWAIT, 1);
	if (unlikely(!entry->pglist))
		return -1;

	cpages = malloc(sizeof(pagestruct_t *) * entry->cpglist_cnt, M_GDEVQ, Q_WAITOK);
	upages = malloc(sizeof(pagestruct_t *) * entry->pglist_cnt, M_GDEVQ, Q_WAITOK);
	map_pglist_pages(entry->pglist, entry->pglist_cnt, upages);
	uaddr = vm_pg_map(upages, entry->pglist_cnt);
	if (!uaddr)
		goto err;

	map_pglist_pages(entry->cpglist, entry->cpglist_cnt, cpages);
	caddr = vm_pg_map(cpages, entry->cpglist_cnt);
	if (!caddr)
		goto err;

	retval = qs_inflate_block(caddr, entry->comp_size, uaddr, entry->block_size);
	if (unlikely(retval != 0)) {
		struct blk_entry *prev, *next;
		debug_warn("Uncompress failed for entry id %d lid_start %llu b_start %llu bid %u comp size %u block size %u\n", entry->entry_id, (unsigned long long)entry->lid_start, (unsigned long long)entry->b_start, entry->bint->bid, entry->comp_size, entry->block_size); 
		prev = blk_entry_get_prev(entry);
		if (prev) 
			debug_warn("Uncompress failed for prev id %d lid_start %llu b_start %llu bid %u comp size %u block size %u\n", prev->entry_id, (unsigned long long)prev->lid_start, (unsigned long long)prev->b_start, prev->bint->bid, prev->comp_size, prev->block_size); 
		next = blk_entry_get_next(entry);
		if (next)  {
			debug_warn("Uncompress failed for next id %d lid_start %llu b_start %llu bid %u comp size %u block size %u data block %d\n", next->entry_id, (unsigned long long)next->lid_start, (unsigned long long)next->b_start, next->bint->bid, next->comp_size, next->block_size, entry_is_data_block(next)); 
			next = blk_entry_get_next(next);
			if (next)
				debug_warn("Uncompress failed for next id %d lid_start %llu b_start %llu bid %u comp size %u block size %u data block %d\n", next->entry_id, (unsigned long long)next->lid_start, (unsigned long long)next->b_start, next->bint->bid, next->comp_size, next->block_size, entry_is_data_block(next)); 
		}
		
	}

	vm_pg_unmap(caddr, entry->cpglist_cnt);
	vm_pg_unmap(uaddr, entry->pglist_cnt);
	free(upages, M_GDEVQ);
	free(cpages, M_GDEVQ);
	return 0;
err:
	if (uaddr)
		vm_pg_unmap(uaddr, entry->pglist_cnt);
	if (caddr)
		vm_pg_unmap(caddr, entry->cpglist_cnt);
	pglist_free(entry->pglist, entry->pglist_cnt);
	entry->pglist = NULL;
	free(upages, M_GDEVQ);
	free(cpages, M_GDEVQ);
	return -1;
}

int
blk_map_read(struct tape_partition *partition, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint8_t fixed, uint32_t *done_blocks, uint32_t *ili_block_size, uint32_t *ret_compressed_size)
{
	int pglist_cnt = 0;
	int retval;
	struct pgdata **pglist = NULL;
	struct blk_map *orig_map, *map;
	struct blk_entry *orig_entry;
	uint32_t data_blocks = 0;
	int error = 0, i;
	uint32_t read_size;
	int todo;
	int pg_idx, entry_idx;
	struct blk_entry *read_entry;
	struct blk_map *read_map;
	uint32_t compressed_size = 0;

	*done_blocks = 0;
	map = partition->cur_map;
	if (!map)
		return EOD_REACHED;

	blk_map_free_till_cur(partition);

	if (!map->c_entry) {
		if (!map_lookup_map_has_next(map))
			return EOD_REACHED;

		map = blk_map_set_next_map(map, 1);
		if (unlikely(!map))
			return MEDIA_ERROR;
	}

	if (!map->c_entry && !map_lookup_map_has_next(map))
		return EOD_REACHED;

	orig_map = map;
	orig_entry = map->c_entry;

	retval = __blk_map_read(map, block_size, num_blocks, &pglist_cnt, &data_blocks, &error, fixed, ili_block_size);

	if (unlikely(retval != 0)) {
		debug_warn("Internal read failed\n");
		goto reset_and_return;
	}

	if (!error)
	{
		read_size = num_blocks * block_size;
	}
	else if (fixed)
	{
		read_size = data_blocks * block_size;
	}
	else
	{
		if (*ili_block_size > block_size)
		{
			read_size = block_size;
		}
		else
		{
			read_size = *ili_block_size;
		}
	}

	if (!read_size)
	{
		debug_check(!error);
		goto skip;
	}

	pglist = pgdata_allocate(block_size, num_blocks, &pglist_cnt, Q_WAITOK, 0);
	read_entry = orig_entry;
	read_map = map;
	debug_check(!read_entry);	
	pg_idx = 0;

	todo = read_size;
	while (read_entry) {
		if (!entry_is_data_block(read_entry) || read_entry == partition->cur_map->c_entry)
			break;
		debug_check(!read_entry->tcache);
		wait_for_done(read_entry->tcache->completion);

		if (read_entry->comp_size) {
			retval = blk_entry_uncompress(read_entry);
			if (retval != 0) {
				debug_warn("Uncompress failed for lid_start %llu b_start %llu bid %u comp size %u\n", (unsigned long long)read_entry->lid_start, (unsigned long long)read_entry->b_start, read_entry->bint->bid, read_entry->comp_size); 
				goto reset_and_return;
			}
			compressed_size += read_entry->comp_size;
		}
			
		entry_idx = 0;
		entry_pglist_map(pglist, &pg_idx, pglist_cnt, read_entry->pglist, &entry_idx, read_entry->pglist_cnt);
		blk_entry_free_data(read_entry);
		debug_check(pg_idx > pglist_cnt);
		if (pg_idx == pglist_cnt)
			break;
		todo -= read_entry->block_size;
		if (todo <= 0)
			break;
		read_entry = blk_entry_read_next(read_entry, &read_map, 0, 0);
		debug_check(!read_entry);
		debug_check(!read_map);
	}

	if (read_entry)
		blk_map_readahead(read_entry);

	for (i = pg_idx; i < pglist_cnt; i++)
		pgdata_free(pglist[i]);
	pglist_cnt = pg_idx;
skip:
	ctio->data_ptr = (void *)pglist;
	ctio->dxfer_len = read_size;
	ctio->pglist_cnt = pglist_cnt;
	*done_blocks = data_blocks;
	*ret_compressed_size = compressed_size;
	return error;

reset_and_return:
	debug_warn("read failed reset and return\n");
	if (pglist)
		pglist_free(pglist, pglist_cnt);
	orig_map->c_entry = orig_entry;
	tape_partition_set_cur_map(orig_map->partition, orig_map);
	return MEDIA_ERROR;
}

static uint64_t
blk_map_current_lid_tape(struct blk_map *map)
{
	struct blk_entry *entry;

	if (map->c_entry)
		return map->c_entry->lid_start;

	if (map->nr_entries == 0)
		return map->l_ids_start;

	entry = blk_map_last_entry(map);
	return (entry->lid_start + 1);
}

uint64_t
blk_map_current_lid(struct blk_map *map)
{
	if (!map)
		return 0ULL;
	return blk_map_current_lid_tape(map);
}

static void
blk_map_update_ids(struct blk_map *map)
{
	struct blk_entry *entry;
	uint64_t f_ids = 0;
	uint64_t s_ids = 0;

	TAILQ_FOREACH(entry, &map->entry_list, e_list) {
		if (entry_is_filemark(entry))
			f_ids++;
		else if (entry_is_setmark(entry))
			s_ids++;
	}

	map->f_ids = f_ids;
	map->s_ids = s_ids;
}

static int 
blk_map_write_eod(struct blk_map *map)
{
	struct blk_map *nmap;
	int retval;

	nmap = blk_map_get_next(map);
	if (nmap || map_lookup_map_has_next(map)) {
		blk_map_free_from_map(map->partition, nmap);
		map_lookup_resync_ids(map->mlookup);
		retval = map_lookup_write_eod(map);
		if (unlikely(retval != 0))
			return MEDIA_ERROR;
	}

	if (map->c_entry) {
		map->nr_entries = map->c_entry->entry_id;
		blk_map_entry_free_from_entry(map, map->c_entry);
		map->c_entry = NULL;
		blk_map_update_ids(map);
		map_lookup_entry_update(map);
		map_lookup_resync_ids(map->mlookup);
		atomic_set_bit(META_IO_PENDING, &map->mlookup->flags);
		retval = map_lookup_flush_meta(map->mlookup);
		if (unlikely(retval != 0))
			return MEDIA_ERROR;
		retval = blk_map_flush_meta(map);
		if (unlikely(retval != 0))
			return MEDIA_ERROR;
	}

	retval = tape_partition_write_eod(map->partition);
	return retval;
}

static int
blk_map_overwrite_check(struct blk_map *map)
{
	struct tape_partition *partition;
	int retval;

	if (!map)
		return 0;

	partition = map->partition;
	if (atomic_test_bit(PARTITION_DIR_WRITE, &partition->flags))
		return 0;

	if (!map_lookup_map_has_next(map) && !map->c_entry)
		return 0;

	if (partition->tape->worm)
		return OVERWRITE_WORM_MEDIA;

	if (!map->nr_entries && map->l_ids_start) {
		map = blk_map_set_prev(map);
		if (unlikely(!map))
			return MEDIA_ERROR;
		partition->cur_map = map;
	}

	tape_partition_print_cur_position(partition, "Before OVERWRITE");
	retval = blk_map_write_eod(map);
	tape_partition_print_cur_position(partition, "After OVERWRITE");
	if (unlikely(retval != 0))
		return MEDIA_ERROR;

	tape_update_volume_change_reference(partition->tape);
	return 0;
}

#define MAX_CACHED_WRITES		(1 * 1024 * 1024)

static int 
blk_map_setup_writes(struct blk_map *map)
{
	struct blk_entry *entry;
	struct tcache *tcache, *prev = NULL;
	int retval;
	int pending_pglist_cnt, pages, entry_pglist_cnt;
	struct tcache_list tcache_list;

	pending_pglist_cnt = map->pending_pglist_cnt;
	if (!pending_pglist_cnt)
		return 0;

	SLIST_INIT(&tcache_list);
	pages = min_t(int, 128, pending_pglist_cnt);
	tcache = tcache_alloc(pages);
	if (unlikely(!tcache)) {
		debug_warn("Cannot allocate tcache for pglist cnt %d\n", map->pending_pglist_cnt);
		return -1;
	}
	SLIST_INSERT_HEAD(&tcache_list, tcache, t_list);

	TAILQ_FOREACH(entry, &map->entry_list, e_list) {
		if (!entry_is_data_block(entry))
			continue;
		if (!atomic_test_bit(BLK_ENTRY_NEW, &entry->flags))
			continue;
		atomic_clear_bit(BLK_ENTRY_NEW, &entry->flags);
		if (entry->comp_size)
			entry_pglist_cnt = entry->cpglist_cnt;
		else
			entry_pglist_cnt = entry->pglist_cnt;

		if ((atomic_read(&tcache->bio_remain) + entry_pglist_cnt) > pages || tcache->size > TCACHE_MAX_SIZE) {
			pages = min_t(int, 128, pending_pglist_cnt);
			pages = max_t(int, pages, entry_pglist_cnt);
			prev = tcache;
			tcache = tcache_alloc(pages);
			if (unlikely(!tcache))
				goto err;
			SLIST_INSERT_AFTER(prev, tcache, t_list);
		}

		retval = blk_entry_add_to_tcache(tcache, map, entry, QS_IO_WRITE);
		if (unlikely(retval != 0))
			goto err;
		pending_pglist_cnt -= entry_pglist_cnt;
		debug_check(pending_pglist_cnt < 0);
	}
	map->pending_pglist_cnt = 0;
	while ((tcache = SLIST_FIRST(&tcache_list)) != NULL) {
		SLIST_REMOVE_HEAD(&tcache_list, t_list);
		if (!atomic_read(&tcache->bio_remain)) {
			tcache_put(tcache);
			continue;
		}
		tcache_entry_rw(tcache, QS_IO_WRITE);
		SLIST_INSERT_HEAD(&map->tcache_list, tcache, t_list);
	}

	return 0;
err:
	while ((tcache = SLIST_FIRST(&tcache_list)) != NULL) {
		SLIST_REMOVE_HEAD(&tcache_list, t_list);
		tcache_put(tcache);
	}
	return -1;
}

static int
blk_entries_write_insert(struct tape_partition *partition, struct blk_map *start, struct blkentry_list *entry_list, int tmark, int new, uint64_t f_ids_start, uint64_t s_ids_start, uint32_t *ret_compressed_size)
{
	struct blkmap_list map_list;
	struct maplookup_list mlookup_list;
	struct blk_map *map = start;
	struct map_lookup *mlookup;
	struct tsegment saved_meta_segment;
	struct tsegment saved_data_segment;
	struct blk_entry *entry;
	uint32_t compressed_size = 0;
	int retval;
	int entry_id;

	TAILQ_INIT(&map_list);
	TAILQ_INIT(&mlookup_list);

	memcpy(&saved_meta_segment, &partition->msegment, sizeof(saved_meta_segment));
	memcpy(&saved_data_segment, &partition->dsegment, sizeof(saved_data_segment));
	if (map) {
		mlookup = map->mlookup;
		entry_id = map->nr_entries;
	}
	else {
		mlookup = NULL;
		entry_id = 0;
	}

	TAILQ_FOREACH(entry, entry_list, e_list) {
		if (!map || entry_id == BLK_MAX_ENTRIES || new) {
			map = tape_partition_add_map(partition, entry->lid_start, f_ids_start, s_ids_start, &mlookup, &mlookup_list, &map_list);
			if (unlikely(!map))
				goto reset;
			if (!start)
				start = map;
			entry_id = 0;
		}

		retval = tape_partition_check_data_segment(partition, entry);
		if (unlikely(retval != 0))
			goto reset;

		entry_set_segment_id(entry, tape_partition_get_dsegment_id(partition));
		entry->map = map;
		entry->entry_id = entry_id++;
	}

	while ((entry = TAILQ_FIRST(entry_list)) != NULL) {
		TAILQ_REMOVE(entry_list, entry, e_list);
		map = entry->map;
		if (entry->comp_size) {
			debug_check(!entry->cpglist);
			cache_data_incr(map, entry->comp_size);
			map->pending_pglist_cnt += entry->cpglist_cnt;
			pglist_free(entry->pglist, entry->pglist_cnt);
			entry->pglist = NULL;
			entry->pglist_cnt = 0;
			compressed_size += align_size(entry->comp_size, 512);
		}
		else if (entry->block_size) {
			cache_data_incr(map, entry->block_size);
			map->pending_pglist_cnt += entry->pglist_cnt;
		}
		map->nr_entries++;
		TAILQ_INSERT_TAIL(&map->entry_list, entry, e_list);
		atomic_set_bit(META_IO_PENDING, &map->flags);
	}

	partition->cur_map = map;

	if (start && start != partition->cur_map) {
		retval = blk_map_setup_writes(start);
		if (retval != 0)
			goto reset;
	}

	TAILQ_FOREACH(map, &map_list, m_list) {
		if (tmark || map != partition->cur_map || map->cached_data > MAX_CACHED_WRITES) {
			retval = blk_map_setup_writes(map);
			if (retval != 0)
				goto reset;
		}
	}

	while ((map = TAILQ_FIRST(&map_list)) != NULL) {
		TAILQ_REMOVE(&map_list, map, m_list);
		TAILQ_INSERT_TAIL(&partition->map_list, map, m_list);
	}

	while ((mlookup = TAILQ_FIRST(&mlookup_list)) != NULL) {
		TAILQ_REMOVE(&mlookup_list, mlookup, l_list);
		TAILQ_INSERT_TAIL(&partition->mlookup_list, mlookup, l_list);
	}

	if (ret_compressed_size)
		*ret_compressed_size = compressed_size;
	return 0;
reset:
	memcpy(&partition->msegment, &saved_meta_segment, sizeof(saved_meta_segment));
	memcpy(&partition->dsegment, &saved_data_segment, sizeof(saved_data_segment));
	__map_lookup_free_all(&mlookup_list);
	__blk_map_free_all(&map_list);
	partition->cur_map = start;
	return -1;
}

int
blk_map_write_filemarks(struct tape_partition *partition, uint8_t wsmk)
{
	struct blk_map *map;
	struct map_lookup *mlookup;
	struct blk_entry *entry;
	int retval, new = 0;
	uint64_t f_ids_start, s_ids_start;
	struct blkentry_list entry_list;

	retval = blk_map_overwrite_check(partition->cur_map);
	if (unlikely(retval != 0))
		return retval;

	retval = tape_partition_lookup_segments(partition);
	if (unlikely(retval != 0))
		return retval;

	entry = blk_entry_new();
	if (!entry)
		return MEDIA_ERROR;

	map = partition->cur_map;
	entry->b_start =  tape_partition_get_dsegment_cur(partition);
	entry->bint = tape_partition_get_dsegment_bint(partition); 
	entry->lid_start = blk_map_current_lid(map);
	entry_set_segment_id(entry, tape_partition_get_dsegment_id(partition));
	entry_set_tapemark_type(entry, wsmk);

	TAILQ_INIT(&entry_list);
	TAILQ_INSERT_TAIL(&entry_list, entry, e_list);

	if (map) {
		if (wsmk && map->s_ids == MAX_SET_MARKS_PER_MAP)
			new = 1;
		else if (!wsmk && map->f_ids == MAX_FILE_MARKS_PER_MAP)
			new = 1;
		mlookup = map->mlookup;
		f_ids_start = mlookup->f_ids_start + mlookup->f_ids;
		s_ids_start = mlookup->s_ids_start + mlookup->s_ids;
	}
	else {
		f_ids_start = 0;
		s_ids_start = 0;
	}
	retval = blk_entries_write_insert(partition, map, &entry_list, 1, new, f_ids_start, s_ids_start, NULL);
	if (unlikely(retval != 0))
		goto err;

	retval = tape_partition_flush_writes(partition);

	map = partition->cur_map;

	if (wsmk) {
		map->s_ids++;
		map->mlookup->s_ids++;
	}
	else {
		map->f_ids++;
		map->mlookup->f_ids++;
	}
	map_lookup_entry_update(map);
	if (!entry->lid_start)
		tape_update_volume_change_reference(partition->tape);
	return retval;
err:
	blk_entry_free_all(&entry_list);
	return MEDIA_ERROR;
}

static struct blk_entry *
new_write_entry(uint32_t block_size, uint64_t lid_start)
{
	struct blk_entry *entry;

	entry = blk_entry_new();
	if (!entry)
		return NULL;

	entry->lid_start = lid_start;
	entry->block_size = block_size;
	entry_set_data_block(entry);
	atomic_set_bit(BLK_ENTRY_NEW, &entry->flags);
	return entry;
}

static int
setup_entries(struct blkentry_list *entry_list, struct tape_partition *partition, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint64_t lid_start, uint8_t compression_enabled)
{
	int i, src_idx, entry_idx, entry_pglist_cnt, src_pglist_cnt;
	struct blk_entry *entry;
	struct pgdata **src_pglist;
	struct pgdata **entry_pglist;

	src_idx = 0;
	src_pglist = (struct pgdata **)(ctio->data_ptr);
	src_pglist_cnt = ctio->pglist_cnt;

	for (i = 0; i < num_blocks; i++) {
		entry = new_write_entry(block_size, lid_start+i);
		entry_pglist = pgdata_allocate(block_size, 1, &entry_pglist_cnt, Q_WAITOK, 0);
		entry_idx = 0;
		entry_pglist_map(entry_pglist, &entry_idx, entry_pglist_cnt, src_pglist, &src_idx, src_pglist_cnt);
		entry->pglist = entry_pglist;
		debug_check(!entry->pglist);
		entry->pglist_cnt = entry_pglist_cnt;
		if (compression_enabled)
			gdevq_comp_insert(entry);
		TAILQ_INSERT_TAIL(entry_list, entry, e_list);
	}

	if (!compression_enabled)
		return 0;

	TAILQ_FOREACH(entry, entry_list, e_list) {
		wait_for_done(entry->completion);
		wait_completion_free(entry->completion);
		entry->completion = NULL;
	}
	return 0;
}

static void
blk_entry_free_pglist(struct blk_map *map, int error)
{
	struct blk_entry *entry;

	TAILQ_FOREACH(entry, &map->entry_list, e_list) {
		blk_entry_free_data(entry);
	}
}

static int 
blk_map_wait_for_data_completion(struct blk_map *map)
{
	int error;

	error = tcache_list_wait(&map->tcache_list);
	blk_entry_free_pglist(map, error);
	if (error)
		atomic_set_bit(CACHE_DATA_ERROR, &map->flags);
	return error;
}

static int
blk_map_check_data_completion(struct blk_map *map)
{
	struct tcache *tcache;
	int error = 0;

	while ((tcache = SLIST_FIRST(&map->tcache_list)) != NULL) {
		if (!tcache->completion->done)
			return 0;
		SLIST_REMOVE_HEAD(&map->tcache_list, t_list);
		if (atomic_test_bit(TCACHE_IO_ERROR, &tcache->flags))
			error = -1;
		tcache_put(tcache);
	}

	blk_entry_free_pglist(map, error);
	if (error)
		atomic_set_bit(CACHE_DATA_ERROR, &map->flags);
	return (error < 0) ? error : 1;
}

static void
blk_map_remove(struct tape_partition *partition, struct blk_map *map)
{
	if (atomic_read(&map->refs) > 1)
		return;

	if (map == partition->cur_map)
		return;

	TAILQ_REMOVE(&partition->map_list, map, m_list);
	blk_map_put(map);
}

void
tape_partition_flush_reads(struct tape_partition *partition)
{
	struct blk_map *map = partition->cur_map;
	struct blk_entry *entry;

	if (!map)
		return;

	blk_map_free_till_cur(partition);
	blk_map_free_from_map(partition, blk_map_get_next(map));
	map_lookup_free_from_mlookup(partition, mlookup_get_next(map->mlookup));
	TAILQ_FOREACH(entry, &map->entry_list, e_list) {
		blk_entry_free_pglist(map, 0);
	}
	tcache_list_wait(&map->tcache_list);	
	atomic_clear_bit(PARTITION_DIR_READ, &partition->flags);
}

void
tape_partition_pre_read(struct tape_partition *partition)
{
	if (atomic_test_bit(PARTITION_DIR_READ, &partition->flags))
		return;

	tape_partition_flush_writes(partition);
	blk_map_free_till_cur(partition);
}

void
tape_partition_post_write(struct tape_partition *partition)
{
	atomic_set_bit(PARTITION_DIR_WRITE, &partition->flags);
}

void
tape_partition_post_read(struct tape_partition *partition)
{
	atomic_set_bit(PARTITION_DIR_READ, &partition->flags);
}

void
tape_partition_pre_write(struct tape_partition *partition)
{
	if (atomic_test_bit(PARTITION_DIR_WRITE, &partition->flags))
		return;
	tape_partition_flush_reads(partition);
}

void
tape_partition_pre_space(struct tape_partition *partition)
{
	tape_partition_flush_writes(partition);
	atomic_clear_bit(PARTITION_DIR_READ, &partition->flags);
}

void
tape_partition_post_space(struct tape_partition *partition)
{
	blk_map_free_till_cur(partition);
	atomic_set_bit(PARTITION_LOOKUP_SEGMENTS, &partition->flags);
}

static int 
__tape_partition_flush_writes(struct tape_partition *partition, int wait)
{
	struct blk_map *map, *next;
	struct map_lookup *mlookup, *prev_mlookup = NULL, *next_mlookup;
	int retval;
	int error = 0;

	TAILQ_FOREACH_SAFE(map, &partition->map_list, m_list, next) {
		if (!wait && map == partition->cur_map)
			continue;
		blk_map_setup_writes(map);
		if (wait || error)
			blk_map_wait_for_data_completion(map);
		else {
			retval = blk_map_check_data_completion(map);
			if (!retval) {
				break;
			}
		}

		retval = blk_map_flush_meta(map);
		if (unlikely(retval != 0)) {
			debug_warn("Flushing metadata for blk map at %llu failed\n", (unsigned long long)map->l_ids_start);
			error = MEDIA_ERROR;
		}

		if (!error && !wait && atomic_test_bit(META_DATA_DIRTY, &map->flags))
			continue;

		wait_on_chan(map->blk_map_wait, !atomic_test_bit(META_DATA_DIRTY, &map->flags));
		if (atomic_test_bit(META_DATA_ERROR, &map->flags)) {
			debug_warn("Flushing metadata for blk map at %llu failed\n", (unsigned long long)map->l_ids_start);
			error = MEDIA_ERROR;
		}

		if (!atomic_test_bit(META_DATA_NEW, &map->flags) || error) {
			blk_map_remove(partition, map);
			continue;
		}

		mlookup = map->mlookup;
		debug_check(!mlookup);
		map_lookup_add_map(mlookup, map);
		atomic_clear_bit(META_DATA_NEW, &map->flags);
		blk_map_remove(partition, map);
	}

	TAILQ_FOREACH_REVERSE_SAFE(mlookup, &partition->mlookup_list, maplookup_list, l_list, next_mlookup) {
		if (!atomic_test_bit(META_IO_PENDING, &mlookup->flags)) {
			if (!error && atomic_test_bit(META_DATA_NEW, &mlookup->flags)) {
				debug_check(wait);
				break;
			}
			map_lookup_remove(partition, mlookup);
			continue;
		}

		retval = map_lookup_flush_meta(mlookup);
		if (unlikely(retval != 0)) {
			debug_warn("Flushing metadata for map lookup at %llu failed\n", (unsigned long long)mlookup->l_ids_start);
			error = MEDIA_ERROR;
		}

		if (wait || atomic_test_bit(META_DATA_NEW, &mlookup->flags))
			wait_on_chan(mlookup->map_lookup_wait, !atomic_test_bit(META_DATA_DIRTY, &mlookup->flags));

		if (atomic_test_bit(META_DATA_ERROR, &mlookup->flags)) {
			debug_warn("Flushing metadata for map lookup at %llu failed\n", (unsigned long long)mlookup->l_ids_start);
			error = MEDIA_ERROR;
		}

		if (!error && atomic_test_bit(META_DATA_NEW, &mlookup->flags)) {
			if (!mlookup->map_nrs) { /* Can happen on error */
				map_lookup_remove(partition, mlookup);
				continue;
			}
			prev_mlookup = mlookup_get_prev(mlookup);
			if (prev_mlookup)
				map_lookup_link(prev_mlookup, mlookup);
			atomic_clear_bit(META_DATA_NEW, &mlookup->flags);
		}

		if (atomic_test_bit(META_DATA_DIRTY, &mlookup->flags))
			continue;

		if (atomic_test_bit(META_DATA_ERROR, &mlookup->flags)) {
			debug_warn("Flushing metadata for map lookup at %llu failed\n", (unsigned long long)mlookup->l_ids_start);
			error = MEDIA_ERROR;
		}

		map_lookup_remove(partition, mlookup);
	}
	return error;
}

int
tape_partition_flush_writes(struct tape_partition *partition)
{
	int retval;

	if (!atomic_test_bit(PARTITION_DIR_WRITE, &partition->flags))
		return 0;
	retval = __tape_partition_flush_writes(partition, 1);
	atomic_clear_bit(PARTITION_DIR_WRITE, &partition->flags);
	return retval;
}

static int
tape_partition_start_writes(struct tape_partition *partition, int wait)
{
	return __tape_partition_flush_writes(partition, wait);
}

int
blk_map_write(struct tape_partition *partition, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint32_t *blocks_written, uint8_t compression_enabled, uint32_t *compressed_size)
{
	int retval;
	struct blk_map *map;
	struct map_lookup *mlookup;
	struct blkentry_list entry_list;
	uint64_t lid_start, f_ids_start, s_ids_start;

	if (partition->cached_data > PARTITION_CACHED_WRITES_MAX) {
		retval = tape_partition_start_writes(partition, !ctio_buffered(ctio));
		if (unlikely(retval != 0)) {
			ctio_free_data(ctio);
			return retval;
		}
	}
 
	retval = blk_map_overwrite_check(partition->cur_map);
	if (unlikely(retval != 0)) {
		debug_warn("Overwrite check failed with retval %d\n", retval);
		ctio_free_data(ctio);
		return retval;
	}

	retval = tape_partition_lookup_segments(partition);
	if (unlikely(retval != 0)) {
		debug_warn("Lookup segments failed with retval %d\n", retval);
		ctio_free_data(ctio);
		return retval;
	}

	map = partition->cur_map;
	TAILQ_INIT(&entry_list);

	lid_start = blk_map_current_lid(map);
	retval = setup_entries(&entry_list, partition, ctio, block_size, num_blocks, lid_start, compression_enabled);
	if (unlikely(retval != 0)) {
		debug_warn("Setup entries failed\n");
		goto err;
	}

	if (map) {
		mlookup = map->mlookup;
		f_ids_start = mlookup->f_ids_start + mlookup->f_ids;
		s_ids_start = mlookup->s_ids_start + mlookup->s_ids;
	}
	else {
		f_ids_start = 0;
		s_ids_start = 0;
	}

	retval = blk_entries_write_insert(partition, map, &entry_list, 0, 0, f_ids_start, s_ids_start, compressed_size);
	if (unlikely(retval != 0)) {
		debug_warn("Insert entries failed\n");
		goto err;
	}

	*blocks_written = num_blocks;
	ctio_free_data(ctio);
	if (!ctio_buffered(ctio)) {
		retval = tape_partition_start_writes(partition, 1);
	}
	else
		retval = 0;

	if (retval == 0 && !lid_start)
		tape_update_volume_change_reference(partition->tape);

	return retval;

err:
	ctio_free_data(ctio);
	blk_entry_free_all(&entry_list);
	return MEDIA_ERROR;
}

int
blk_map_erase(struct blk_map *map)
{
	int retval;

	retval = blk_map_overwrite_check(map);
	return retval;
}

static int
iter_space_valid(struct blk_entry *entry, uint8_t code, int *error)
{
	if (code == SPACE_CODE_BLOCKS)
	{
		if (entry_is_data_block(entry))
		{
			return 1;
		}
		else if (entry_is_filemark(entry))
		{
			*error = FILEMARK_ENCOUNTERED;
		}
		else
		{
			*error = SETMARK_ENCOUNTERED;
		}
		return 0;
	}
	else if (code == SPACE_CODE_FILEMARKS)
	{
		if (entry_is_filemark(entry) || entry_is_data_block(entry))
			return 1;
		*error = SETMARK_ENCOUNTERED;
		return 0;
	}
	else
	{ 
		return 1;
	}
}

static int
iter_space_count(struct blk_entry *entry, uint8_t code)
{
	if (code == SPACE_CODE_BLOCKS && entry_is_data_block(entry))
		return 1;
	else if (code == SPACE_CODE_FILEMARKS && entry_is_filemark(entry))
		return 1;
	else if (code == SPACE_CODE_SETMARKS && entry_is_setmark(entry))
		return 1;
	return 0;
}

int
blk_map_space_backward(struct blk_map *map, uint8_t code, int *count)
{
	struct blk_entry *entry;
	int todo = *count;
	int error = 0, space_count;

	if (!map->c_entry)
		entry = blk_map_last_entry(map);
	else
		entry = blk_entry_get_prev(map->c_entry);

	while (entry) {
		space_count = iter_space_count(entry, code);
		todo -= space_count;
		map->c_entry = entry;
		if (!iter_space_valid(entry, code, &error))
			break;
		if (!todo)
			break;
		entry = blk_entry_get_prev(entry);
	}
	*count = todo;
	return error;
}

int
blk_map_space_forward(struct blk_map *map, uint8_t code, int *count)
{
	struct blk_entry *entry;
	int todo = *count;
	int error = 0, space_count;

	entry = map->c_entry;
	if (!entry)
		return 0;

	while (entry) {
		space_count = iter_space_count(entry, code);
		todo -= space_count;
		if (!iter_space_valid(entry, code, &error)) {
			entry = blk_entry_get_next(entry);
			break;
		}
		entry = blk_entry_get_next(entry);
		if (!todo)
			break;
	}
	map->c_entry = entry;
	*count = todo;
	return error;
}


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

#ifndef QUADSTOR_BLK_MAP_H_
#define QUADSTOR_BLK_MAP_H_

#include "coredefs.h"
#include "tdrive.h"
#include "tape_partition.h"

#include "blk_entry.h"

enum {
	BLK_ENTRY_NEW,
	BLK_ENTRY_WRITE_SETUP_DONE,
	BLK_ENTRY_READ_SETUP_DONE,
};

struct raw_blk_map {
	uint32_t segment_id;
	uint16_t csum;
	uint16_t  nr_entries;
	uint64_t pad; /* Each blk entry is 16 bytes long, so this is is free */
} __attribute__ ((__packed__));

#define BLK_MAX_ENTRIES		((LBA_SIZE - sizeof(struct raw_blk_map)) / sizeof(struct raw_blk_entry))
#define MIN_MAP_READ_AHEAD	4

struct blk_map * blk_map_new(struct tape_partition *partition, struct map_lookup *mlookup, uint64_t l_ids_start, uint64_t b_start, struct bdevint *bint, uint32_t segment_id);
struct blk_map * blk_map_load(struct tape_partition *partition, uint64_t b_start, uint32_t bid, int async, struct map_lookup *map_lookup, uint16_t mlookup_entry_id, int poslast);

void blk_map_entry_insert(struct blk_map *map, struct blk_entry *entry);

void blk_map_free_all(struct tape_partition *partition);
void blk_map_free_all2(struct blk_map *head);
void blk_map_release(struct blk_map *map);

int blk_map_read_entry_position(struct blk_map *map, struct tl_entryinfo *entryinfo);

/* support commands */
int blk_map_locate(struct blk_map *map, uint64_t block_address);
int blk_map_locate_file(struct blk_map *map, uint64_t block_address);
void blk_map_read_position(struct tape_partition *partition, struct tape_position_info *info);
int blk_map_write_filemarks(struct tape_partition *partition, uint8_t wmsk);
int blk_map_read(struct tape_partition *partition, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint8_t fixed, uint32_t *bytes_read, uint32_t *ili_block_size);
int blk_map_write(struct tape_partition *partition, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint32_t *blocks_written, uint8_t compression_enabled);

/* Spacing support functions */
int blk_map_space_forward(struct blk_map *map, uint8_t code, int *count);
int blk_map_space_backward(struct blk_map *map, uint8_t code, int *count);
int blk_map_erase(struct blk_map *map);

/* Misc */
void print_map_location_info(struct blk_map *map);

static inline struct blk_map *
blk_map_get_prev(struct blk_map *map)
{
	struct blk_map *prev;

	prev = TAILQ_PREV(map, blkmap_list, m_list);
	return prev;
}

static inline struct blk_map *
blk_map_get_first(struct blkmap_list *map_list)
{
	return TAILQ_FIRST(map_list);
}
 
static inline struct blk_map *
blk_map_get_last(struct blkmap_list *map_list)
{
	return TAILQ_LAST(map_list, blkmap_list);
}
 
static inline struct blk_map *
blk_map_get_next(struct blk_map *map)
{
	return TAILQ_NEXT(map, m_list);
}

static inline int 
blk_map_has_next(struct blk_map *map)
{
	return (blk_map_get_next(map) != NULL);
}

static inline int 
blk_map_has_prev(struct blk_map *map)
{
	return (blk_map_get_prev(map) != NULL);
}

static inline struct blk_entry *
blk_entry_get_next(struct blk_entry *entry)
{
	struct blk_entry *next;

	next = TAILQ_NEXT(entry, e_list);
	return next;
}

static inline int
blk_entry_has_next(struct blk_entry *entry)
{
	return (blk_entry_get_next(entry) != NULL);
}

static inline struct blk_entry *
blk_entry_get_prev(struct blk_entry *entry)
{
	struct blk_entry *prev;

	prev = TAILQ_PREV(entry, blkentry_list, e_list);
	return prev;
}

static inline int
blk_entry_has_prev(struct blk_entry *entry)
{
	return (blk_entry_get_prev(entry) != NULL);
}

static inline struct blk_entry *
blk_map_first_entry(struct blk_map *map)
{
	return TAILQ_FIRST(&map->entry_list);
}

static inline struct blk_entry *
blk_map_last_entry(struct blk_map *map)
{
	return TAILQ_LAST(&map->entry_list, blkentry_list);
}

static inline struct blk_entry *
blk_entry_get_prev2(struct blk_entry *entry)
{
	struct blk_entry *prev;
	struct blk_map *map;

	prev = blk_entry_get_prev(entry);
	if (prev)
		return prev;

	map = blk_map_get_prev(entry->map);
	if (!map)
		return NULL;
	return blk_map_last_entry(map);
}

static inline struct blk_map *
tape_partition_first_map(struct tape_partition *partition)
{
	return TAILQ_FIRST(&partition->map_list);
}

static inline struct blk_map *
tape_partition_last_map(struct tape_partition *partition)
{
	return TAILQ_LAST(&partition->map_list, blkmap_list);
}

static inline void
blk_map_insert(struct tape_partition *partition, struct blk_map *map)
{
	TAILQ_INSERT_TAIL(&partition->map_list, map, m_list);
}

static inline uint64_t
blk_map_first_entry_block(struct blk_map *map)
{
	struct blk_entry *entry;

	entry = blk_map_first_entry(map);
	if (!entry)
		return 0;
	return entry->b_start;
}

static inline void
blk_map_write_entry(struct blk_map *map, struct blk_entry *entry)
{
	struct raw_blk_entry *raw_entry;

	raw_entry = (struct raw_blk_entry *)(vm_pg_address(map->metadata) + (entry->entry_id * sizeof(*raw_entry)));
	entry_set_comp_size(entry);
	entry_set_block_size(entry);
	SET_BLOCK(raw_entry->block, entry->b_start, entry->bint->bid);
	raw_entry->bits = entry->bits;
}

static inline void 
blk_map_position_bop(struct blk_map *map)
{
	map->c_entry = blk_map_first_entry(map);
}

uint64_t blk_map_current_lid(struct blk_map *map);

struct blk_map * blk_map_set_next_map(struct blk_map *map, int set_cur);
struct blk_map * blk_maps_readahead(struct tape_partition *partition, int count);

static inline int
next_till_end_count(struct blk_map *map)
{
	int count = 0;

	while (map)
	{
		map = blk_map_get_next(map);
		count++;
	}
	return count;
}
void blk_map_free_from_map(struct tape_partition *partition, struct blk_map *map);
#endif /* BLK_MAP_H_ */

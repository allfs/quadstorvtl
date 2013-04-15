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

#ifndef MAP_LOOKUP_H_
#define MAP_LOOKUP_H_

/* 
 * Fast lookups of blk maps
 */

#include "coredefs.h"
#include "tdrive.h"

struct map_lookup_entry {
	uint64_t l_ids_info;
	uint64_t block;
} __attribute__ ((__packed__));

#define MAX_SET_MARKS_PER_MAP		15
#define MAX_FILE_MARKS_PER_MAP		15

#define MENTRY_LID_START(mentry) ((uint64_t)(mentry->l_ids_info & 0xFFFFFFFFFFFFFFULL))
#define MENTRY_FILEMARKS(mentry) ((uint8_t)(mentry->l_ids_info >> 56) & 0xF)
#define MENTRY_SETMARKS(mentry) ((uint8_t)(mentry->l_ids_info >> 60) & 0xF)

#define MENTRY_SET_LID_INFO(mentry,lstart,fids,sids) (mentry->l_ids_info = ((uint64_t)sids << 60 | (uint64_t)fids << 56 | lstart))

struct raw_map_lookup {
	uint64_t next_block;		
	uint64_t prev_block;
	uint64_t l_ids_start;
	uint32_t f_ids_start;
	uint32_t s_ids_start;
	uint16_t csum;
	uint16_t map_nrs;
	uint32_t reserved1;
} __attribute__ ((__packed__));

#define NR_MAP_DATA_ENTRIES	((LBA_SIZE - sizeof(struct raw_map_lookup))/sizeof(struct map_lookup_entry))

static inline void
map_lookup_get(struct map_lookup *mlookup)
{
	atomic_inc(&mlookup->refs);
}

struct map_lookup * map_lookup_new(struct tape_partition *partition, uint64_t l_ids_start, uint64_t f_ids_start, uint64_t s_ids_start, uint64_t b_start, struct bdevint *bint);
struct map_lookup * map_lookup_load(struct tape_partition *partition, uint64_t b_start, uint32_t bid);
void map_lookup_free(struct map_lookup *mlookup);
struct blk_map;
void map_lookup_add_map(struct map_lookup *mlookup, struct blk_map *map);
void map_lookup_release2(struct map_lookup *mlookup);
void map_lookup_reserve(struct map_lookup *mlookup);
void map_lookup_link(struct map_lookup *last, struct map_lookup *mlookup);

/* support functions */
struct map_lookup_entry* map_lookup_first_blkmap(struct map_lookup *mlookup);
struct map_lookup_entry* map_lookup_locate(struct tape_partition *partition, uint64_t block_address, struct map_lookup **ret_mlookup, uint16_t *ret_entry_id);
struct map_lookup_entry* map_lookup_locate_file(struct tape_partition *partition, uint64_t block_address, struct map_lookup **ret_mlookup, uint16_t *ret_entry_id);
int map_lookup_flush_meta(struct map_lookup *mlookup);
int map_lookup_write_eod(struct blk_map *map);
struct map_lookup *map_lookup_find(struct tape_partition *partition, uint32_t bid, uint64_t b_start, int nodisk);
struct map_lookup * map_lookup_load_prev(struct map_lookup *mlookup);
struct map_lookup * map_lookup_load_next(struct map_lookup *mlookup);
int map_lookup_find_id(struct map_lookup *mlookup, uint64_t block_address);
void map_lookup_entry_update(struct blk_map *map);
struct map_lookup_entry *map_lookup_find_identifier(struct tape_partition *partition, struct blk_map *cmap, uint8_t code, int count, int *done, int *error, struct map_lookup **ret_lookup);
struct map_lookup_entry *map_lookup_last_blkmap(struct map_lookup *mlookup);
struct map_lookup_entry *map_lookup_next_entry(struct map_lookup *mlookup, uint16_t mlookup_entry_id, struct map_lookup **ret, uint16_t *next_entry_id);
struct map_lookup_entry* map_lookup_prev_entry(struct map_lookup *mlookup, uint16_t entry_id, struct map_lookup **ret_lookup, uint16_t *prev_entry_id);
void map_lookup_print_entries(struct map_lookup *mlookup);
void map_lookup_resync_ids(struct map_lookup *mlookup);
void map_lookup_load_next_async(struct map_lookup *mlookup);
int map_lookup_next_avail(struct map_lookup *mlookup);
void map_lookup_insert(struct tape_partition *partition, struct map_lookup *mlookup);

static inline void
map_lookup_put(struct map_lookup *mlookup)
{
	if (atomic_dec_and_test(&mlookup->refs))
		map_lookup_free(mlookup);
}

struct map_lookup * mlookup_get_prev(struct map_lookup *mlookup);
struct map_lookup * mlookup_get_next(struct map_lookup *mlookup);
int mlookup_has_next(struct map_lookup *mlookup);
int mlookup_has_prev(struct map_lookup *mlookup);

int map_lookup_map_has_next(struct blk_map *map);

static inline struct map_lookup_entry *
map_lookup_get_entry(struct map_lookup *map_lookup, uint16_t entry_id)
{
	return (struct map_lookup_entry *)(vm_pg_address(map_lookup->metadata) + (entry_id * sizeof(struct map_lookup_entry)));
}

void __map_lookup_free_all(struct maplookup_list *mlookup_list);
void map_lookup_free_all(struct tape_partition *partition);
struct map_lookup_entry * map_lookup_space_forward(struct tape_partition *partition, uint8_t code, int *count, int *error, struct map_lookup **ret_lookup, uint16_t *ret_entry_id);
struct map_lookup_entry * map_lookup_space_backward(struct tape_partition *partition, uint8_t code, int *count, int *error, struct map_lookup **ret_lookup, uint16_t *ret_entry_id);
struct map_lookup * map_lookup_find_last(struct tape_partition *partition);
void map_lookup_get_ids_start(struct map_lookup *mlookup, uint16_t entry_id, uint64_t *ret_f_ids_start, uint64_t *ret_s_ids_start);
void map_lookup_remove(struct tape_partition *partition, struct map_lookup *mlookup);
void map_lookup_free_till_cur(struct tape_partition *partition);
void map_lookup_free_from_mlookup(struct tape_partition *partition, struct map_lookup *mlookup);
#endif /* MAP_LOOKUP_H_ */

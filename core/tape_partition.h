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

#ifndef QUADSTOR_TAPE_PARTITION_H_
#define QUADSTOR_TAPE_PARTITION_H_

#include "coredefs.h"
#include "tdrive.h"
#include "bdev.h"
#include "tcache.h"

struct raw_tsegment_map {
	uint16_t csum;
	uint16_t pad[3];
};

struct tsegment_map {
	pagestruct_t *metadata;
	uint64_t b_start;
	uint16_t tmap_id;
	TAILQ_ENTRY(tsegment_map) t_list;
};
TAILQ_HEAD(tmap_list, tsegment_map);

struct raw_mam {
	uint16_t csum;
	uint16_t pad[3];
} __attribute__ ((__packed__));

struct raw_attribute {
	uint16_t length;
	uint8_t valid;
} __attribute__ ((__packed__));

struct mam_attribute {
	uint16_t identifier;
	uint8_t format;
	uint16_t length;
	uint8_t valid;
	struct raw_attribute *raw_attr;
	uint8_t *value;
};
#define MAX_MAM_ATTRIBUTES	90
#define MAM_ATTRIBUTE_DATA_OFFSET		(MAX_MAM_ATTRIBUTES * sizeof(struct read_attribute))

struct tsegment_entry {
	uint64_t block;
};

#define TSEGMENT_MAP_MAX_SEGMENTS	((LBA_SIZE - sizeof(struct raw_tsegment_map)) / sizeof(struct tsegment_entry))

static inline struct tsegment_entry *
__tmap_segment_entry(pagestruct_t *metadata, uint16_t entry_id)
{
	struct tsegment_entry *entry;

	entry = (struct tsegment_entry *)(vm_pg_address(metadata) + (sizeof(*entry) * entry_id));
	return entry;
}

static inline struct tsegment_entry *
tmap_segment_entry(struct tsegment_map *tmap, uint16_t entry_id)
{
	return __tmap_segment_entry(tmap->metadata, entry_id);
}

struct tsegment {
	uint64_t b_start;
	uint64_t b_cur;
	uint64_t b_end;
	uint32_t segment_id;
	int tmap_id;
	int tmap_entry_id;
	struct bdevint *bint;
};

struct blk_entry {
	uint64_t bits;
	uint64_t b_start;
	uint64_t lid_start;
	struct blk_map *map;
	struct bdevint *bint;
	STAILQ_ENTRY(blk_entry) c_list;
	uint16_t entry_id;
	uint32_t comp_size;
	uint32_t block_size;

	int flags;
	int pglist_cnt;
	int cpglist_cnt;
	/* Comp related */
	struct pgdata **pglist;
	struct pgdata **cpglist;
	struct tcache *tcache;
	wait_compl_t *completion;
	TAILQ_ENTRY(blk_entry) e_list;
};

TAILQ_HEAD(blkentry_list, blk_entry);
STAILQ_HEAD(blkentry_clist, blk_entry);

struct map_lookup {
	uint16_t map_nrs;
	uint16_t pending_new_maps;
	uint64_t b_start;
	uint64_t next_block;
	uint64_t prev_block;

	struct bdevint *bint;
	struct tape_partition *partition; 
	pagestruct_t *metadata;
	int flags;
	TAILQ_ENTRY(map_lookup) l_list;

	uint64_t l_ids_start;
	uint32_t f_ids_start;
	uint32_t s_ids_start;
//	uint64_t l_ids;
	uint16_t f_ids;
	uint16_t s_ids;

	wait_chan_t *map_lookup_wait;
	atomic_t refs;
};
TAILQ_HEAD(maplookup_list, map_lookup);

struct blk_map {
	uint64_t b_start; 
	struct bdevint *bint;
	uint16_t nr_entries;
	uint32_t segment_id;
	uint16_t f_ids; /* number of file marks */
	uint16_t s_ids; /* number of set marks */ 
	uint64_t l_ids_start;
	int flags;

	struct blkentry_list entry_list;
	TAILQ_ENTRY(blk_map) m_list;
	struct tcache_list tcache_list;

	int32_t cached_data;
	int pending_pglist_cnt;

	pagestruct_t *metadata;
	wait_chan_t *blk_map_wait;
	struct tape_partition *partition;
	struct map_lookup *mlookup;
	uint16_t mlookup_entry_id;
	struct blk_entry *c_entry;

	/* Error Related */
	struct blk_entry *read_error_entry; /* If not null error in blk map */
	struct blk_entry *write_error_entry; /* If not null error in blk map */
	sx_t *blk_map_lock;

	atomic_t in_use;
	atomic_t in_ra;
	atomic_t refs;
};
TAILQ_HEAD(blkmap_list, blk_map);

#define PARTITION_CACHED_WRITES_MAX	(4 * 1024 * 1024)

enum {
	PARTITION_LOOKUP_SEGMENTS,
	PARTITION_DIR_WRITE,
	PARTITION_DIR_READ,
	PARTITION_MAM_CORRUPT,
};

struct tape_partition {
	uint64_t size;
	uint64_t used;
	uint8_t partition_id;

	int32_t cached_data;
	int32_t cached_blocks;

	atomic_t pending_size;
	atomic_t pending_writes;

	uint64_t tmaps_b_start;
	struct bdevint *tmaps_bint;

	/* segment information */
	struct tsegment dsegment;
	struct tsegment msegment;
	struct tmap_list meta_tmap_list;
	struct tmap_list data_tmap_list;
	int max_data_tmaps;
	int max_meta_tmaps;

	struct tape *tape;
	struct blk_map *cur_map;
	struct blkmap_list map_list;
	struct maplookup_list mlookup_list;
	SLIST_ENTRY(tape_partition) p_list;
	int flags;
	pagestruct_t *mam_data;
	struct mam_attribute mam_attributes[MAX_MAM_ATTRIBUTES];
};

void partition_set_cmap(struct tape_partition *partition, struct blk_map *map);

/* Stream Commands */
int tape_partition_rewind(struct tape_partition *partition);

int tape_partition_space(struct tape_partition *partition, uint8_t code, int *count);

void tape_partition_free(struct tape_partition *partition, int free_alloc);
int tape_partition_free_alloc(struct tape_partition *partition, int ignore_errors);
int tape_partition_free_tmaps_block(struct tape_partition *partition);
void tape_partition_set_current_blkmap(struct tape_partition *partition, struct blk_map *map);
int tape_partition_write_eod(struct tape_partition *partition);
int tape_partition_read_entry_position(struct tape_partition *partition, struct tl_entryinfo *entryinfo);
int partition_alloc_segment(struct tape_partition *partition, int type);
int partition_locate_cur_segment(struct tape_partition *partition, uint32_t segment_id,  int type);

/* support commands */
int tape_partition_position_bop(struct tape_partition *partition);
void tape_partition_read_position(struct tape_partition *partition, struct qsio_scsiio *ctio, uint8_t service_action);
int tape_partition_locate(struct tape_partition *partition, uint64_t block_address, uint8_t locate_type);
int tape_partition_write_filemarks(struct tape_partition *partition, uint8_t wmsk, uint32_t transfer_length);
int tape_partition_read(struct tape_partition *partition, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint8_t fixed,  uint32_t *blocks_read, uint32_t *ili_block_size, uint32_t *compressed_size);
int tape_partition_write(struct tape_partition *partition, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint32_t *blocks_written, uint8_t compression_enabled, uint32_t *compressed_size);
int tape_partition_validate_write(struct tape_partition *partition, uint32_t block_size, uint32_t num_blocks);
int tape_partition_at_bop(struct tape_partition *partition);

static inline uint32_t
tape_partition_get_dsegment_id(struct tape_partition *partition)
{
	return partition->dsegment.segment_id;
}

static inline uint64_t
tape_partition_get_dsegment_start(struct tape_partition *partition)
{
	return partition->dsegment.b_start;
}

static inline struct bdevint * 
tape_partition_get_dsegment_bint(struct tape_partition *partition)
{
	return partition->dsegment.bint;
}

static inline void
tape_partition_set_dsegment_cur(struct tape_partition *partition, uint64_t b_cur)
{
	partition->dsegment.b_cur = b_cur;
}

static inline uint64_t
tape_partition_get_dsegment_cur(struct tape_partition *partition)
{
	return partition->dsegment.b_cur;
}

static inline struct blk_map *
tape_partition_get_cur_map(struct tape_partition *partition)
{
	return partition->cur_map;
}

static inline void
tape_partition_set_cur_map(struct tape_partition *partition, struct blk_map *map)
{
	partition->cur_map = map;
}

struct map_lookup;
struct map_lookup *tape_partition_last_mlookup(struct tape_partition *partition);
struct map_lookup *tape_partition_first_mlookup(struct tape_partition *partition);

#define PARTITION_READ_CACHE_MAX	(16 * 1024 * 1024)
#define PARTITION_READ_CACHE_MIN	(4 * 1024 * 1024)

void tape_partition_set_cmap(struct tape_partition *partition, struct blk_map *map);
struct tape_partition *tape_partition_new(struct tape *tape, uint64_t size, int partition_id);
struct tape_partition *tape_partition_load(struct tape *tape, int partition_id);
int tape_partition_erase(struct tape_partition *partition);

int tape_partition_alloc_segment(struct tape_partition *partition, int type);
struct blk_entry;
int tape_partition_check_data_segment(struct tape_partition *partition, struct blk_entry *entry);
struct blk_map * tape_partition_add_map(struct tape_partition *partition, uint64_t lid_start, uint64_t f_ids_start, uint64_t s_ids_start, struct map_lookup **ret_map_lookup, struct maplookup_list *mlookup_list, struct blkmap_list *map_list);
int tape_partition_flush_buffers(struct tape_partition *partition);
void tape_partition_unload(struct tape_partition *partition);
int tape_partition_flush_writes(struct tape_partition *partition);
void tape_partition_flush_reads(struct tape_partition *partition);
int tape_partition_lookup_segments(struct tape_partition *partition);
int tape_partition_lookup_segment(struct tape_partition *partition, int type, uint32_t segment_id, struct tsegment *tsegment);
void tape_partition_pre_read(struct tape_partition *partition);
void tape_partition_post_read(struct tape_partition *partition);
void tape_partition_pre_write(struct tape_partition *partition);
void tape_partition_post_write(struct tape_partition *partition);
void tape_partition_pre_space(struct tape_partition *partition);
void tape_partition_post_space(struct tape_partition *partition);

int tape_partition_get_max_tmaps(struct tape_partition *partition, int type);
struct tsegment_map * tmap_locate(struct tape_partition *partition, int type, uint16_t tmap_id);
void tape_partition_print_cur_position(struct tape_partition *partition, char *msg);

struct tape_position_info {
	uint8_t bop;
	uint8_t eop;
	uint32_t blocks_in_buffer;
	uint64_t bytes_in_buffer;
	uint64_t block_number; 
	uint64_t file_number;
	uint64_t set_number;
};
int tape_partition_mam_set_byte(struct tape_partition *partition, uint16_t identifier, uint8_t val);
int tape_partition_mam_set_word(struct tape_partition *partition, uint16_t identifier, uint32_t val);
int tape_partition_mam_set_long(struct tape_partition *partition, uint16_t identifier, uint64_t val);
int tape_partition_mam_set_ascii(struct tape_partition *partition, uint16_t identifier, char *val);
int tape_partition_mam_memset(struct tape_partition *partition, uint16_t identifier, uint8_t val, int vali);
int tape_partition_mam_set_text(struct tape_partition *partition, uint16_t identifier, char *val);
struct mam_attribute * tape_partition_mam_get_attribute(struct tape_partition *partition, uint16_t identifier);
int tape_partition_write_mam(struct tape_partition *partition);
int mam_attr_length_valid(struct read_attribute *attr, struct mam_attribute *mam_attr);
void mam_attr_set_length(struct read_attribute *attr, struct mam_attribute *mam_attr);
void tape_update_volume_change_reference(struct tape *tape);
void tape_partition_update_mam(struct tape_partition *partition, uint16_t first_attribute);
void tape_partition_invalidate_pointers(struct tape_partition *partition);

static inline int
tmap_skip_segment(struct tape_partition *partition, int tmap_id, int tmap_segment_id, int type)
{
	int v2_tape = is_v2_tape(partition->tape);

	if (!tmap_id && !tmap_segment_id && type == SEGMENT_TYPE_META && v2_tape)
		return 1;
	else
		return 0;
}

#endif

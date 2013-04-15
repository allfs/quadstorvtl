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
#include "blk_map.h"
#include "map_lookup.h"
#include "tape.h"
#include "qs_lib.h"

static int tape_partition_space_eod(struct tape_partition *partition);

void
tape_partition_print_cur_position(struct tape_partition *partition, char *msg)
{
#if 0
	struct blk_map *map;
	struct map_lookup *mlookup;
	struct blk_entry *entry;
	struct tsegment *tsegment;

	printf("%s >>>>>>>\n", msg);
	printf("pnum %d size %llu used %llu\n", partition->partition_id, (unsigned long long)partition->size, (unsigned long long)partition->used);
	printf("cached data %u cached blocks %u\n", partition->cached_data, partition->cached_blocks);
	tsegment = &partition->msegment;
	printf("msegment id %u tmap id %u tmap entry id %u\n", tsegment->segment_id, tsegment->tmap_id, tsegment->tmap_entry_id);
	printf("msegment b_start %llu b_cur %llu b_end %llu\n", (unsigned long long)tsegment->b_start, (unsigned long long)tsegment->b_cur, (unsigned long long)tsegment->b_end);
	tsegment = &partition->dsegment;
	printf("dsegment id %u tmap id %u tmap entry id %u\n", tsegment->segment_id, tsegment->tmap_id, tsegment->tmap_entry_id);
	printf("dsegment b_start %llu b_cur %llu b_end %llu\n", (unsigned long long)tsegment->b_start, (unsigned long long)tsegment->b_cur, (unsigned long long)tsegment->b_end);
	map = partition->cur_map;
	printf("cur map: %p\n", map);
	if (!map)
		return;
	printf("map b_start %llu l_ids_start %llu nr_entries %d\n", (unsigned long long)map->b_start, (unsigned long long)map->l_ids_start, map->nr_entries);
	printf("map f_ids %u s_ids %u\n", map->f_ids, map->s_ids);
	mlookup = map->mlookup;
	printf("mlookup b_start %llu next_block %llu prev_block %llu\n", (unsigned long long)mlookup->b_start, (unsigned long long)mlookup->next_block, (unsigned long long)mlookup->prev_block);
	printf("mlookup l_ids_start %llu f_ids_start %llu s_ids_start %llu nr maps %u\n", (unsigned long long)mlookup->l_ids_start, (unsigned long long)mlookup->f_ids_start, (unsigned long long)mlookup->s_ids_start, mlookup->map_nrs);
	printf ("mlookup f_ids %llu s_ids %llu\n", (unsigned long long)mlookup->f_ids, (unsigned long long)mlookup->s_ids);

	entry = map->c_entry;
	if (entry) {
		printf("Cur entry entry id %d b_start %llu lid_start %llu data block %d file mark %d set mark %d\n", entry->entry_id, (unsigned long long)entry->b_start, (unsigned long long)entry->lid_start, entry_is_data_block(entry), entry_is_filemark(entry), entry_is_setmark(entry));
	}
	else {
		entry = blk_map_last_entry(map);
		if (entry) {
			printf("Last entry entry id %d b_start %llu lid_start %llu data block %d file mark %d set mark %d\n", entry->entry_id, (unsigned long long)entry->b_start, (unsigned long long)entry->lid_start, entry_is_data_block(entry), entry_is_filemark(entry), entry_is_setmark(entry));
		}
		else {
			printf("Last entry is NULL\n");
		}
	}
	printf(">>>>>>>\n");
#endif
}

static int
segment_get_tmap_id(uint32_t segment_id, int *tmap_entry_id)
{
	uint32_t tmap_id;

	tmap_id = segment_id / TSEGMENT_MAP_MAX_SEGMENTS;
	*tmap_entry_id = segment_id % TSEGMENT_MAP_MAX_SEGMENTS;
	return tmap_id;
}

static uint32_t
tmap_get_segment_id(int tmap_id, int tmap_entry_id)
{
	uint32_t segment_id;

	segment_id = (tmap_id * TSEGMENT_MAP_MAX_SEGMENTS) + tmap_entry_id;
	return segment_id;
}

struct map_lookup *
tape_partition_first_mlookup(struct tape_partition *partition)
{
	return TAILQ_FIRST(&partition->mlookup_list);
}

struct map_lookup *
tape_partition_last_mlookup(struct tape_partition *partition)
{
	return TAILQ_LAST(&partition->mlookup_list, maplookup_list);
}

static struct tmap_list *
tmap_head(struct tape_partition *partition, int type)
{
	if (type == SEGMENT_TYPE_DATA)
		return &partition->data_tmap_list;
	else
		return &partition->meta_tmap_list;
}

#define MAX_META_TSEGMENT_MAPS		4096
#define DATA_TSEGMENT_MAPS_OFFSET	(MAX_META_TSEGMENT_MAPS * LBA_SIZE)
#define PARTITION_HEADER_SIZE		262144
#define META_TSEGMENT_MAPS_OFFSET	PARTITION_HEADER_SIZE

static uint64_t
tmap_bstart(struct tape_partition *partition, int type, uint16_t tmap_id)
{
	uint32_t offset;
	uint64_t b_start;

	offset = (type == SEGMENT_TYPE_DATA) ? DATA_TSEGMENT_MAPS_OFFSET : META_TSEGMENT_MAPS_OFFSET;
	offset += (tmap_id * LBA_SIZE);

	b_start = partition->tmaps_b_start + (offset >> partition->tmaps_bint->sector_shift);
	return b_start;
}

static void
tmap_free(struct tsegment_map *tmap)
{
	vm_pg_free(tmap->metadata);
	free(tmap, M_TMAPS);
}

static void
tmap_write_csum(pagestruct_t *metadata)
{
	struct raw_tsegment_map *raw_tmap;
	uint16_t csum;

	raw_tmap = (struct raw_tsegment_map *)(vm_pg_address(metadata) + (LBA_SIZE - sizeof(*raw_tmap)));
	csum = net_calc_csum16(vm_pg_address(metadata), LBA_SIZE - sizeof(*raw_tmap));
	raw_tmap->csum = csum;
}

static int
zero_page(pagestruct_t *page)
{
	uint64_t *pgdata_addr = (uint64_t *)vm_pg_address(page);
	uint64_t val;
	int i;

	for (i = 0; i < (LBA_SIZE / 8); i+=8) {
		val = pgdata_addr[i] | pgdata_addr[i+1] | pgdata_addr[i+2] | pgdata_addr[i+3] | pgdata_addr[i+4] | pgdata_addr[i+5] | pgdata_addr[i+6] | pgdata_addr[i+7];
		if (val)
			return 0;
	}
	return 1;
}

static int
tmap_validate(struct tsegment_map *tmap)
{
	struct raw_tsegment_map *raw_tmap;
	uint16_t csum;

	raw_tmap = (struct raw_tsegment_map *)(vm_pg_address(tmap->metadata) + (LBA_SIZE - sizeof(*raw_tmap)));
	csum = net_calc_csum16(vm_pg_address(tmap->metadata), LBA_SIZE - sizeof(*raw_tmap));
	if (csum != raw_tmap->csum) {
		if (zero_page(tmap->metadata))
			return 0;
		debug_warn("Mismatch in csum got %x stored %x\n", csum, raw_tmap->csum);
		return -1;
	}
	return 0;
}

static void
tmap_list_free_all(struct tmap_list *tmap_list)
{
	struct tsegment_map *tmap;

	while ((tmap = TAILQ_FIRST(tmap_list)) != NULL) {
		TAILQ_REMOVE(tmap_list, tmap, t_list);
		free(tmap, M_TMAPS);
	}
}

struct tsegment_map *
tmap_locate(struct tape_partition *partition, int type, uint16_t tmap_id)
{
	struct tmap_list *tmap_list;
	struct tsegment_map *tmap;
	int retval;
	uint64_t b_start;

	tmap_list = tmap_head(partition, type); 
	TAILQ_FOREACH(tmap, tmap_list, t_list) {
		if (tmap->tmap_id == tmap_id)
			return tmap; 
	}

	b_start = tmap_bstart(partition, type, tmap_id);
	tmap = zalloc(sizeof (*tmap), M_TMAPS, Q_WAITOK);
	if (unlikely(!tmap))
		return NULL;

	tmap->metadata = vm_pg_alloc(0);
	if (unlikely(!tmap->metadata)) {
		free(tmap, M_TMAPS);
		return NULL;
	}
	tmap->b_start = b_start;
	tmap->tmap_id = tmap_id;

	retval = qs_lib_bio_lba(partition->tmaps_bint, tmap->b_start, tmap->metadata, QS_IO_READ, 0);
	if (unlikely(retval != 0)) {
		tmap_free(tmap);
		return NULL;
	}

	retval = tmap_validate(tmap);
	if (unlikely(retval != 0)) {
		tmap_free(tmap);
		return NULL;
	}
	TAILQ_INSERT_TAIL(tmap_list, tmap, t_list);
	return tmap;
}

static int
tmap_write(struct tape_partition *partition, struct tsegment_map *tmap)
{
	int retval;

	tmap_write_csum(tmap->metadata);
	retval = qs_lib_bio_lba(partition->tmaps_bint, tmap->b_start, tmap->metadata, QS_IO_WRITE, 0);
	return retval;
}

static int
tmap_id_get_next_segment(int tmap_id, int *ret_tmap_entry_id)
{
	int tmap_entry_id = *ret_tmap_entry_id;

	if ((tmap_entry_id + 1) < TSEGMENT_MAP_MAX_SEGMENTS) {
		*ret_tmap_entry_id = (tmap_entry_id + 1);
		return tmap_id;
	}
	*ret_tmap_entry_id = 0;
	return (tmap_id + 1);
}

int
tape_partition_alloc_segment(struct tape_partition *partition, int type)
{
	struct tsegment_map *tmap;
	struct tsegment *tsegment;
	struct tsegment_entry *entry;
	uint64_t b_start, b_end;
	struct bdevint *bint;
	int tmap_id;
	int tmap_entry_id;
	int retval;

	tsegment = (type == SEGMENT_TYPE_DATA) ? &partition->dsegment : &partition->msegment;

	tmap_id = tsegment->tmap_id;
	tmap_entry_id = tsegment->tmap_entry_id;

	if (tsegment->b_start)
		tmap_id = tmap_id_get_next_segment(tmap_id, &tmap_entry_id);

	tmap = tmap_locate(partition, type, tmap_id);
	if (unlikely(!tmap)) {
		debug_warn("Cannot find tmap at id %d\n", tmap_id);
		return -1;
	}

	entry = tmap_segment_entry(tmap, tmap_entry_id);
	if (!entry->block) {
		b_start = bdev_get_block(partition->tmaps_bint, &bint, &b_end);
		if (unlikely(!b_start)) {
			debug_warn("Getting new block segment failed, partition used %llu\n", (unsigned long long)partition->used);
			return -1;
		}

		if (!tmap_id && !tmap_entry_id && type == SEGMENT_TYPE_META) {
			retval = tcache_zero_range(bint, b_start, 4); /* first mlookup, first map */
			if (unlikely(retval != 0)) {
				bdev_release_block(bint, b_start);
				return -1;
			}
		}

		SET_BLOCK(entry->block, b_start, bint->bid);
		retval = tmap_write(partition, tmap);
		if (unlikely(retval != 0)) {
			entry->block = 0;
			bdev_release_block(bint, b_start);
			return -1;
		}
		partition->used += BINT_UNIT_SIZE;
		debug_info("New segment type %d tmap id %u tmap entry id %u segment id %u b_start %llu used %llu\n", type, tmap_id, tmap_entry_id, tmap_get_segment_id(tmap_id, tmap_entry_id), (unsigned long long)b_start, (unsigned long long)partition->used);
	}
	else {
		b_start = BLOCK_BLOCKNR(entry->block);
		bint = bdev_find(BLOCK_BID(entry->block));
		if (unlikely(!bint)) {
			debug_warn("Cannot locate bint at %u\n", BLOCK_BID(entry->block));
			return -1;
		}
		b_end = BINT_UNIT_SIZE >> bint->sector_shift;
		debug_info("Old segment type %d tmap id %u tmap entry id %u segment id %u b_start %llu\n", type, tmap_id, tmap_entry_id, tmap_get_segment_id(tmap_id, tmap_entry_id), (unsigned long long)b_start);
	}

	tsegment->tmap_id = tmap_id;
	tsegment->tmap_entry_id = tmap_entry_id;
	tsegment->segment_id = tmap_get_segment_id(tmap_id, tmap_entry_id);
	tsegment->b_start = b_start;
	tsegment->b_cur = b_start;
	tsegment->b_end = b_end;
	tsegment->bint = bint;
	return 0;
}

static struct map_lookup *
tape_partition_add_mlookup(struct tape_partition *partition, uint64_t l_ids_start, uint64_t f_ids_start, uint64_t s_ids_start, struct maplookup_list *mlookup_list)
{
	struct tsegment *meta_segment = &partition->msegment;
	struct map_lookup *mlookup;
	uint32_t meta_blocks;
	int retval;

	if (!meta_segment->bint)
		goto alloc_new;

	meta_blocks = LBA_SIZE >> meta_segment->bint->sector_shift;
	if (meta_segment->b_start && (meta_segment->b_cur + meta_blocks) <= meta_segment->b_end)
		goto skip_new;

alloc_new:
	retval =  tape_partition_alloc_segment(partition, SEGMENT_TYPE_META);
	if (unlikely(retval != 0)) {
		debug_warn("Cannot create a new meta segment\n");
		return NULL;
	}
skip_new:
	meta_blocks = LBA_SIZE >> meta_segment->bint->sector_shift;
	mlookup = map_lookup_new(partition, l_ids_start, f_ids_start, s_ids_start, meta_segment->b_cur, meta_segment->bint);
	if (unlikely(!mlookup))
		return NULL;

	TAILQ_INSERT_TAIL(mlookup_list, mlookup, l_list);
	meta_segment->b_cur += meta_blocks;
	return mlookup;
}

struct blk_map *
tape_partition_add_map(struct tape_partition *partition, uint64_t l_ids_start, uint64_t f_ids_start, uint64_t s_ids_start, struct map_lookup **ret_mlookup, struct maplookup_list *mlookup_list, struct blkmap_list *map_list)
{
	struct tsegment *meta_segment = &partition->msegment;
	struct map_lookup *mlookup = *ret_mlookup;
	struct blk_map *map;
	uint32_t meta_blocks;
	int retval;

	debug_check(mlookup && (mlookup->map_nrs + mlookup->pending_new_maps + 1) > NR_MAP_DATA_ENTRIES);
	if (!mlookup || ((mlookup->map_nrs + mlookup->pending_new_maps + 1) == NR_MAP_DATA_ENTRIES)) {
		mlookup = tape_partition_add_mlookup(partition, l_ids_start, f_ids_start, s_ids_start, mlookup_list);
		if (!mlookup)
			return NULL;
	}

	debug_check(!meta_segment->bint);
	meta_blocks = LBA_SIZE >> meta_segment->bint->sector_shift;
	debug_check(!meta_segment->b_cur);
	if ((meta_segment->b_cur + meta_blocks) <= meta_segment->b_end)
		goto skip_new;

	retval = tape_partition_alloc_segment(partition, SEGMENT_TYPE_META);
	if (unlikely(retval != 0)) { 
		debug_warn("Cannot create a new meta segment\n");
		return NULL;
	}

skip_new:
	debug_check(!mlookup);
	map = blk_map_new(partition, mlookup, l_ids_start, meta_segment->b_cur, meta_segment->bint, meta_segment->segment_id);
	if (unlikely(!map)) {
		debug_warn("Cannot create a new map\n");
		return NULL;
	}
	mlookup->pending_new_maps++;
	TAILQ_INSERT_TAIL(map_list, map, m_list);
	meta_segment->b_cur += meta_blocks;
	*ret_mlookup = mlookup;
	return map;
}

int
tape_partition_check_data_segment(struct tape_partition *partition, struct blk_entry *entry)
{
	struct tsegment *data_segment = &partition->dsegment;
	uint32_t blocks = 0;
	uint32_t write_size = entry->comp_size ? entry->comp_size : entry->block_size;
	int retval;

	if (!data_segment->bint)
		goto alloc_new;

	if (!entry_is_data_block(entry))
		goto skip_new;

	blocks = bint_blocks(data_segment->bint, write_size);

	if ((data_segment->b_cur + blocks) <= data_segment->b_end)
		goto skip_new;

alloc_new:
	retval = tape_partition_alloc_segment(partition, SEGMENT_TYPE_DATA);
	if (unlikely(retval != 0))
		return -1;

	blocks = bint_blocks(data_segment->bint, write_size);

skip_new:
	entry->b_start = data_segment->b_cur;
	entry->bint = data_segment->bint;
	data_segment->b_cur += blocks;
	return 0;
}

int
tape_partition_at_bop(struct tape_partition *partition)
{
	struct blk_map *map = partition->cur_map;

	if (!map || (!map->l_ids_start && map->c_entry && !map->c_entry->entry_id))
		return 1;
	else
		return 0;
}

static void 
tape_partition_read_position_long(struct tape_partition *partition, struct qsio_scsiio *ctio, struct tape_position_info *info)
{
	struct read_position_long *readpos = (struct read_position_long *)ctio->data_ptr;

	bzero(readpos, sizeof(*readpos));
	if (info->bop)
		readpos->pos_info |= 0x80;
	else if (info->eop)
		readpos->pos_info |= 0x40;
	readpos->partition_number = htobe32(partition->partition_id);
	readpos->block_number = htobe64(info->block_number);
	readpos->file_number = htobe64(info->file_number);
	readpos->set_number = htobe64(info->set_number);
}

static void
tape_partition_read_position_extended(struct tape_partition *partition, struct qsio_scsiio *ctio, struct tape_position_info *info)
{
	struct read_position_extended readpos;

	bzero(&readpos, sizeof(readpos));
	if (info->bop)
		readpos.pos_info |= 0x80;
	else if (info->eop)
		readpos.pos_info |= 0x40;
	readpos.partition_number = partition->partition_id;
	readpos.additional_length = htobe16(0x1C);
	readpos.blocks_in_buffer[0] = info->blocks_in_buffer & 0xFF;
	readpos.blocks_in_buffer[1] = (info->blocks_in_buffer >> 8) & 0xFF;
	readpos.blocks_in_buffer[2] = (info->blocks_in_buffer >> 16) & 0xFF;
	readpos.first_block_location = htobe64(info->block_number);
	readpos.last_block_location = htobe64(info->block_number - info->blocks_in_buffer);
	readpos.bytes_in_buffer = htobe64(info->bytes_in_buffer);
	memcpy(ctio->data_ptr, &readpos, ctio->dxfer_len);
}

static void 
tape_partition_read_position_short(struct tape_partition *partition, struct qsio_scsiio *ctio, struct tape_position_info *info)
{
	struct read_position_short *readpos = (struct read_position_short *)ctio->data_ptr;

	bzero(readpos, sizeof(*readpos));
	if (info->bop)
		readpos->pos_info |= 0x80;
	else if (info->eop)
		readpos->pos_info |= 0x40;

	readpos->partition_number = partition->partition_id;
	readpos->first_block_location = htobe32(info->block_number);
	readpos->last_block_location = htobe32(info->block_number - info->blocks_in_buffer);
	readpos->blocks_in_buffer[0] = info->blocks_in_buffer & 0xFF;
	readpos->blocks_in_buffer[1] = (info->blocks_in_buffer >> 8) & 0xFF;
	readpos->blocks_in_buffer[2] = (info->blocks_in_buffer >> 16) & 0xFF;
	readpos->bytes_in_buffer = htobe32(info->bytes_in_buffer);
}

void
tape_partition_read_position(struct tape_partition *partition, struct qsio_scsiio *ctio, uint8_t service_action)
{
	struct tape_position_info info;

	tape_partition_print_cur_position(partition, "At READ POSITION");
	bzero(&info, sizeof(info));
	blk_map_read_position(partition, &info);
	if (info.eop) {
		if ((partition->used + EW_SIZE) <= partition->size)
			info.eop = 0;
	}

	switch (service_action) {
	case READ_POSITION_SHORT:
		tape_partition_read_position_short(partition, ctio, &info);
		break;
	case READ_POSITION_LONG:
		tape_partition_read_position_long(partition, ctio, &info);
		break;
	case READ_POSITION_EXTENDED:
		tape_partition_read_position_extended(partition, ctio, &info);
		break;
	}
}

int
tape_partition_write_filemarks(struct tape_partition *partition, uint8_t wmsk, uint32_t transfer_length)
{
	int retval;
	int i;

	tape_partition_print_cur_position(partition, "Before WRITE FILEMARKS");
	tape_partition_pre_write(partition);

	for (i = 0; i < transfer_length; i++) {
		retval = blk_map_write_filemarks(partition, wmsk);
		if (retval != 0)
			return retval;
	}
	tape_partition_post_write(partition);
	tape_partition_flush_writes(partition);
	tape_partition_print_cur_position(partition, "After WRITE FILEMARKS");
	return 0;
}

static inline int
map_lookups_needed(int num_maps)
{
	int avail = 0;
	int nlookups = 0;

	debug_check(avail < 0);
	while (avail < num_maps)
	{
		nlookups++;
		avail += NR_MAP_DATA_ENTRIES;
	}
	return nlookups;
}

int
tape_partition_validate_write(struct tape_partition *partition, uint32_t block_size, uint32_t num_blocks)
{
	uint64_t data_size;
	uint64_t phys_cur = partition->used;
	struct blk_map *map = partition->cur_map;

	data_size = atomic_read(&partition->pending_size);
	if (partition->tape->worm && !data_size && map) {
		if (map_lookup_map_has_next(map) || map->c_entry)
			return OVERWRITE_WORM_MEDIA;
	}

	data_size += phys_cur;
	data_size += (block_size * num_blocks);

	if ((data_size + EW_SIZE) < partition->tape->size)
		return 0;

	if (data_size < partition->tape->size)
		return EW_REACHED;

	return VOLUME_OVERFLOW_ENCOUNTERED;
}

int
tape_partition_erase(struct tape_partition *partition)
{
	return blk_map_erase(partition->cur_map);
}

int
tape_partition_lookup_segment(struct tape_partition *partition, int type, uint32_t segment_id, struct tsegment *tsegment)
{
	struct tsegment_map *tmap;
	struct tsegment_entry *entry;
	struct bdevint *bint;
	uint64_t b_start;
	int tmap_id, tmap_entry_id;

	tmap_id = segment_get_tmap_id(segment_id, &tmap_entry_id);

	tmap = tmap_locate(partition, type, tmap_id);
	if (unlikely(!tmap)) {
		debug_warn("Cannot find tmap at id %d\n", tmap_id);
		return -1;
	}

	entry = tmap_segment_entry(tmap, tmap_entry_id);
	if (!entry->block)
		return 0;

	bint = bdev_find(BLOCK_BID(entry->block));
	if (unlikely(!bint)) {
		debug_warn("Cannot find bint at %u\n", BLOCK_BID(entry->block));
		return -1;
	}

	b_start = BLOCK_BLOCKNR(entry->block);
	tsegment->tmap_id = tmap_id;
	tsegment->tmap_entry_id = tmap_entry_id;
	tsegment->segment_id = segment_id;
	tsegment->b_start = b_start;
	tsegment->b_cur = b_start;
	tsegment->b_end = b_start + (BINT_UNIT_SIZE >> bint->sector_shift);
	tsegment->bint = bint;

	return 0;
}

static int
tape_partition_lookup_cur_meta_segment(struct tape_partition *partition)
{
	struct blk_map *map = partition->cur_map;
	int retval;

	if (!map) {
		bzero(&partition->msegment, sizeof(partition->msegment));
		return 0;
	}

	retval = tape_partition_lookup_segment(partition, SEGMENT_TYPE_META, map->segment_id, &partition->msegment);
	if (retval != 0 || !partition->msegment.b_start)
		return MEDIA_ERROR;

	partition->msegment.b_cur = map->b_start + (LBA_SIZE >> map->bint->sector_shift);
	return 0;
}

static void
blk_entry_position_dsegment(struct tape_partition *partition, struct blk_entry *entry, int end)
{
	struct tsegment *dsegment = &partition->dsegment;
	uint32_t blocks;

	debug_check(dsegment->b_end < entry->b_start);
	debug_check(dsegment->b_start > entry->b_start);
	blocks = end ? bint_blocks(entry->bint, entry_disk_size(entry)) : 0; 
	dsegment->b_cur = entry->b_start + blocks;
	debug_check(dsegment->b_cur > dsegment->b_end);
}

static int
tape_partition_lookup_cur_data_segment(struct tape_partition *partition)
{
	struct blk_map *cur_map = partition->cur_map;
	struct blk_entry *cur_entry;
	int end = 0;
	int retval;

	if (!cur_map) {
		bzero(&partition->dsegment, sizeof(partition->dsegment));
		return 0;
	}

	cur_entry = cur_map->c_entry;
	if (cur_entry)
		goto set;

	if (cur_map->nr_entries) {
		cur_entry = blk_map_last_entry(cur_map);
		end = 1;
		debug_check(!cur_entry);
		goto set;
	}

	/* Can never happen*/
	if (cur_map->l_ids_start) {
		tape_partition_print_cur_position(partition, "Invalid data segement position error");
		return MEDIA_ERROR;
	}

	retval = tape_partition_lookup_segment(partition, SEGMENT_TYPE_DATA, 0, &partition->dsegment);
	if (retval != 0 || !partition->dsegment.b_start)
		return MEDIA_ERROR;
	return 0;
set:
	retval = tape_partition_lookup_segment(partition, SEGMENT_TYPE_DATA, entry_segment_id(cur_entry), &partition->dsegment);
	if (retval != 0 || !partition->dsegment.b_start)
		return MEDIA_ERROR;
	blk_entry_position_dsegment(partition, cur_entry, end);
	return 0;
}

int
tape_partition_lookup_segments(struct tape_partition *partition)
{
	int retval;

	if (!atomic_test_bit(PARTITION_LOOKUP_SEGMENTS, &partition->flags))
		return 0;

	tape_partition_print_cur_position(partition, "Before LOOKUP segments");
	retval = tape_partition_lookup_cur_meta_segment(partition);
	if (retval != 0)
		return retval;

	retval = tape_partition_lookup_cur_data_segment(partition);
	tape_partition_print_cur_position(partition, "After LOOKUP segments");
	if (retval != 0)
		return retval;
	atomic_clear_bit(PARTITION_LOOKUP_SEGMENTS, &partition->flags);
	return 0;
}

int
tape_partition_write(struct tape_partition *partition, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint32_t *blocks_written, uint8_t compression_enabled)
{
	int retval;

	tape_partition_pre_write(partition);
	retval = blk_map_write(partition, ctio, block_size, num_blocks, blocks_written, compression_enabled);
	if (retval == 0)
		tape_partition_post_write(partition);
	return retval;
}

int
tape_partition_read(struct tape_partition *partition, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint8_t fixed, uint32_t *blocks_read, uint32_t *ili_block_size)
{
	int retval;

	tape_partition_pre_read(partition);
	retval = blk_map_read(partition, ctio, block_size, num_blocks, fixed, blocks_read, ili_block_size); 
	if (retval == 0)
		tape_partition_post_read(partition);
	return retval;
}

static int
tape_partition_locate_eod(struct tape_partition *partition)
{
	int retval;

	retval = tape_partition_space_eod(partition);
	tape_partition_print_cur_position(partition, "After LOCATE EOD");
	return retval;
}

static int
tape_partition_locate_file(struct tape_partition *partition, uint64_t block_address)
{
	struct map_lookup *mlookup;
	struct blk_map *map;
	struct map_lookup_entry *entry;
	int retval;
	uint16_t entry_id;

	entry = map_lookup_locate_file(partition, block_address, &mlookup, &entry_id);
	if (!entry) {
		debug_warn("Cannot locate entry for block address %llu\n", (unsigned long long)block_address);
		return MEDIA_ERROR;
	}

	map = partition->cur_map;
	debug_check(!map);
	if (MENTRY_LID_START(entry) != map->l_ids_start) {
		map = blk_map_load(partition, BLOCK_BLOCKNR(entry->block), BLOCK_BID(entry->block), 0, mlookup, entry_id, 0);
		if (unlikely(!map))
			return MEDIA_ERROR;
		blk_map_free_all(partition);
		blk_map_insert(partition, map);
		partition->cur_map = map;
	}

	retval = blk_map_locate_file(map, block_address);
	tape_partition_print_cur_position(partition, "After LOCATE FILE");
	return retval;
}

static int
tape_partition_locate_block(struct tape_partition *partition, uint64_t block_address)
{
	struct map_lookup *mlookup;
	struct blk_map *map;
	struct map_lookup_entry *entry;
	int retval;
	uint16_t entry_id;

	entry = map_lookup_locate(partition, block_address, &mlookup, &entry_id);
	if (!entry) {
		debug_warn("Cannot locate entry for block address %llu\n", (unsigned long long)block_address);
		return MEDIA_ERROR;
	}

	map = partition->cur_map;
	debug_check(!map);
	if (MENTRY_LID_START(entry) != map->l_ids_start) {
		map = blk_map_load(partition, BLOCK_BLOCKNR(entry->block), BLOCK_BID(entry->block), 0, mlookup, entry_id, 0);
		if (unlikely(!map))
			return MEDIA_ERROR;
		blk_map_free_all(partition);
		blk_map_insert(partition, map);
		partition->cur_map = map;
	}

	retval = blk_map_locate(map, block_address);
	tape_partition_print_cur_position(partition, "After LOCATE BLOCK");
	return retval;
}

int
tape_partition_locate(struct tape_partition *partition, uint64_t block_address, uint8_t locate_type)
{
	int retval = 0;

	tape_partition_print_cur_position(partition, "Before LOCATE");
	tape_partition_pre_space(partition);
	if (TAILQ_EMPTY(&partition->mlookup_list)) {
		tape_partition_print_cur_position(partition, "After LOCATE");
		if (!block_address)
			return 0;
		else
			return BLANK_CHECK_ENCOUNTERED;
	}

	switch (locate_type) {
	case LOCATE_TYPE_BLOCK:
		retval = tape_partition_locate_block(partition, block_address);
		break;
	case LOCATE_TYPE_FILE:
		retval = tape_partition_locate_file(partition, block_address);
		break;
	case LOCATE_TYPE_EOD:
		retval = tape_partition_locate_eod(partition);
		break;
	}
	return retval;
}

static int
tape_partition_space_eod(struct tape_partition *partition)
{
	struct map_lookup *mlookup;
	struct map_lookup_entry *entry;
	struct blk_map *map;

	mlookup = map_lookup_find_last(partition);
	if (unlikely(!mlookup))
		return MEDIA_ERROR;

	entry = map_lookup_last_blkmap(mlookup);
	if (unlikely(!entry))
		return MEDIA_ERROR;

	map = blk_map_load(partition, BLOCK_BLOCKNR(entry->block), BLOCK_BID(entry->block), 0, mlookup, mlookup->map_nrs - 1, 1);
	if (unlikely(!map))
		return MEDIA_ERROR;
	blk_map_free_all(partition);
	blk_map_insert(partition, map);
	partition->cur_map = map;

	return 0;
}

static int
tape_partition_space_forward(struct tape_partition *partition, uint8_t code, int *count)
{
	struct map_lookup *mlookup;
	struct map_lookup_entry *entry;
	struct blk_map *map;
	uint16_t entry_id;
	int retval, error = 0;

	map = partition->cur_map;
	debug_check(!map);

	retval = blk_map_space_forward(map, code, count);
	if (retval != 0)
		return retval;

	if (!*count)
		return 0;

	entry = map_lookup_space_forward(partition, code, count, &error, &mlookup, &entry_id);
	if (!entry)
		return error;

	map = blk_map_load(partition, BLOCK_BLOCKNR(entry->block), BLOCK_BID(entry->block), 0, mlookup, entry_id, 0);
	if (unlikely(!map))
		return MEDIA_ERROR;
	blk_map_free_all(partition);
	blk_map_insert(partition, map);
	partition->cur_map = map;

	retval = blk_map_space_forward(map, code, count);
	return retval;
}

static int
tape_partition_space_backward(struct tape_partition *partition, uint8_t code, int *count)
{
	struct map_lookup *mlookup;
	struct map_lookup_entry *entry;
	struct blk_map *map;
	uint16_t entry_id;
	int retval, error = 0;

	*count = -(*count);
	map = partition->cur_map;
	debug_check(!map);

	retval = blk_map_space_backward(map, code, count);
	if (retval != 0)
		return retval;

	if (!*count)
		return 0;

	entry = map_lookup_space_backward(partition, code, count, &error, &mlookup, &entry_id);
	if (!entry)
		return error;

	map = blk_map_load(partition, BLOCK_BLOCKNR(entry->block), BLOCK_BID(entry->block), 0, mlookup, entry_id, 1);
	if (unlikely(!map))
		return MEDIA_ERROR;
	blk_map_free_all(partition);
	blk_map_insert(partition, map);
	partition->cur_map = map;

	retval = blk_map_space_backward(map, code, count);
	return retval;
}

int
tape_partition_space(struct tape_partition *partition, uint8_t code, int *count)
{
	int retval;

	tape_partition_print_cur_position(partition, "Before SPACE");
	tape_partition_pre_space(partition);

	if (TAILQ_EMPTY(&partition->mlookup_list)) {
		if (code == SPACE_CODE_END_OF_DATA)
			return 0;
		if (*count < 0)
			return BOM_REACHED;
		else
			return BLANK_CHECK_ENCOUNTERED;
	}

	if (code == SPACE_CODE_END_OF_DATA)
		retval = tape_partition_space_eod(partition);
	else if (*count > 0)
		retval = tape_partition_space_forward(partition, code, count);
	else
		retval = tape_partition_space_backward(partition, code, count);
	tape_partition_print_cur_position(partition, "After SPACE");
	return retval;
}

int
tape_partition_flush_buffers(struct tape_partition *partition)
{
	tape_partition_pre_space(partition);
	return 0;
}


void tape_partition_set_cmap(struct tape_partition *partition, struct blk_map *map)
{
	partition->cur_map = map;
}

int
tape_partition_position_bop(struct tape_partition *partition)
{
	struct map_lookup_entry *entry;
	struct map_lookup *mlookup;
	struct blk_map *first_map, *map;

	tape_partition_print_cur_position(partition, "Before BOP");
	tape_partition_flush_writes(partition);

	mlookup = tape_partition_first_mlookup(partition);
	if (!mlookup)
		return 0;

	entry = map_lookup_first_blkmap(mlookup);
	debug_check(!entry);
	if (!entry)
		return 0;

	first_map = tape_partition_first_map(partition);
	if (first_map && (BLOCK_BLOCKNR(entry->block) == first_map->b_start) && (BLOCK_BID(entry->block) == first_map->bint->bid)) {
		struct blk_map *next_map;

		next_map = blk_map_get_next(first_map);
		blk_map_free_from_map(partition, next_map);
		blk_map_position_bop(first_map);
		atomic_set_bit(PARTITION_LOOKUP_SEGMENTS, &partition->flags);
		return 0;
	}

	map = blk_map_load(partition, BLOCK_BLOCKNR(entry->block), BLOCK_BID(entry->block), 0, mlookup, 0, 0);
	if (unlikely(!map))
		return -1;

	blk_map_insert(partition, map);
	partition->cur_map = map;
	blk_map_position_bop(map);
	atomic_set_bit(PARTITION_LOOKUP_SEGMENTS, &partition->flags);
	tape_partition_print_cur_position(partition, "After BOP");
	return 0;
}

int
tape_partition_get_max_tmaps(struct tape_partition *partition, int type)
{
	if (type == SEGMENT_TYPE_META)
		return partition->max_meta_tmaps;
	else
		return partition->max_data_tmaps;
}

static int
tmap_eod_segments(struct tape_partition *partition, struct tsegment_map *tmap, int tmap_entry_id)
{
	int i, done = 0, retval;
	struct tsegment_entry *entry;
	struct bdevint *bint;
	pagestruct_t *page;

	debug_info("partition used before %llu\n", (unsigned long long)partition->used);
	page = vm_pg_alloc(0);
	if (unlikely(!page))
		return -1;

	memcpy(vm_pg_address(page), vm_pg_address(tmap->metadata), LBA_SIZE);
	for (i = tmap_entry_id; i < TSEGMENT_MAP_MAX_SEGMENTS; i++) {
		entry = __tmap_segment_entry(page, i);
		if (!entry->block)
			break;
		debug_info("eod tmap id %u entry id %d block %llu\n", tmap->tmap_id, i, (unsigned long long)entry->block);
		entry->block = 0;
	}

	tmap_write_csum(page);
	retval = qs_lib_bio_lba(partition->tmaps_bint, tmap->b_start, page, QS_IO_WRITE, 0);
	vm_pg_free(page);
	if (unlikely(retval != 0))
		return retval;

	for (i = tmap_entry_id; i < TSEGMENT_MAP_MAX_SEGMENTS; i++) {
		entry = tmap_segment_entry(tmap, i);
		if (!entry->block)
			break;
		done++;
		bint = bdev_find(BLOCK_BID(entry->block));
		if (unlikely(!bint)) {
			debug_warn("Cannot find bint at id %u\n", BLOCK_BID(entry->block));
			continue;
		}
		bdev_release_block(bint, BLOCK_BLOCKNR(entry->block));
		entry->block = 0;
	}
	debug_check((done * BINT_UNIT_SIZE) > partition->used);
	partition->used -= (done * BINT_UNIT_SIZE);
	debug_info("partition used after %llu\n", (unsigned long long)partition->used);
	return done;
}

static int 
eod_segments(struct tape_partition *partition, int type, int segment_id, int inclusive)
{
	int tmap_id, tmap_entry_id;
	int max_tmaps;
	int retval;
	int i;
	struct tsegment_map *tmap;

	debug_info("eod segments type %d segment id %d inclusive %d\n", type, segment_id, inclusive);
	if (!inclusive)
		segment_id++;

	tmap_id = segment_get_tmap_id(segment_id, &tmap_entry_id);
	max_tmaps = tape_partition_get_max_tmaps(partition, type);

	for (i = tmap_id; i < max_tmaps; i++) {
		tmap = tmap_locate(partition, type, i);
		if (unlikely(!tmap)) {
			debug_warn("Cannot locate tmap for type %d tmap_id %d\n", type, tmap_id);
			return -1;
		}
		retval = tmap_eod_segments(partition, tmap, tmap_entry_id);
		if (retval < 0) {
			debug_warn("tmap eod segments failed for type %d tmap_id %d\n", type, tmap_id);
			return -1;
		}
		else if (!retval)
			return 0;

		tmap_entry_id = 0;
	}
	return 0;
}

static int
tape_partition_eod_cur_segment(struct tape_partition *partition)
{
	int retval;

	retval = tape_partition_lookup_segments(partition);
	if (unlikely(retval != 0))
		return MEDIA_ERROR;

	eod_segments(partition, SEGMENT_TYPE_DATA, partition->dsegment.segment_id, 0);
	eod_segments(partition, SEGMENT_TYPE_META, partition->msegment.segment_id, 0);
	return 0;
}

int
tape_partition_write_eod(struct tape_partition *partition)
{
	int retval;

	tape_partition_flush_buffers(partition);
	retval = tape_partition_eod_cur_segment(partition);
	return retval;
}

int
tape_partition_free_tmaps_block(struct tape_partition *partition)
{
	struct raw_partition *raw_partition;
	int retval;

	if (!partition->tmaps_b_start)
		return 0;

	raw_partition = tape_get_raw_partition_info(partition->tape, partition->partition_id);
	bzero(raw_partition, sizeof(*raw_partition));
	retval = tape_write_metadata(partition->tape);
	if (retval != 0)
		goto err;

	bdev_release_block(partition->tmaps_bint, partition->tmaps_b_start);
	return 0;
err:
	SET_BLOCK(raw_partition->tmaps_block, partition->tmaps_b_start, partition->tmaps_bint->bid);
	raw_partition->size = partition->size;
	return -1;
}

int
tape_partition_free_alloc(struct tape_partition *partition, int ignore_errors)
{
	int retval;

	retval = eod_segments(partition, SEGMENT_TYPE_META, 0, 1);
	if (retval != 0 && !ignore_errors)
		return -1;

	retval = eod_segments(partition, SEGMENT_TYPE_DATA, 0, 1);
	return retval;
}

void
tape_partition_free(struct tape_partition *partition, int free_alloc)
{
	if (free_alloc) {
		tape_partition_free_alloc(partition, 1);
		tape_partition_free_tmaps_block(partition);
	}
	debug_info("free alloc %d, partition used %llu\n", free_alloc, (unsigned long long)partition->used);
	blk_map_free_all(partition);
	map_lookup_free_all(partition);
	tmap_list_free_all(&partition->meta_tmap_list);
	tmap_list_free_all(&partition->data_tmap_list);
	free(partition, M_TAPE_PARTITION);
}

void
tape_partition_unload(struct tape_partition *partition)
{
	tmap_list_free_all(&partition->meta_tmap_list);
	tmap_list_free_all(&partition->data_tmap_list);
}

static void
partition_calc_max_tmaps(struct tape_partition *partition)
{
	uint64_t units = partition->size >> BINT_UNIT_SHIFT;
	int data_tmaps, meta_tmaps;

	if (partition->tape->size & BINT_UNIT_MASK)
		units++;

	data_tmaps = (units / TSEGMENT_MAP_MAX_SEGMENTS); 
	if (units % TSEGMENT_MAP_MAX_SEGMENTS)
		data_tmaps++;
	if (!data_tmaps)
		data_tmaps = 1;

	meta_tmaps = data_tmaps >> 2;
	if (!meta_tmaps)
		meta_tmaps = 1;
	partition->max_data_tmaps = data_tmaps;
	partition->max_meta_tmaps = meta_tmaps;
}

static void 
tape_partition_init(struct tape *tape, struct tape_partition *partition)
{
	partition->tape = tape;
	TAILQ_INIT(&partition->map_list);
	TAILQ_INIT(&partition->mlookup_list);
	TAILQ_INIT(&partition->meta_tmap_list);
	TAILQ_INIT(&partition->data_tmap_list);
	atomic_set(&partition->pending_size, 0);
	atomic_set(&partition->pending_writes, 0);
	partition_calc_max_tmaps(partition);
}

struct tape_partition *
tape_partition_new(struct tape *tape, uint64_t size, int partition_id)
{
	struct tape_partition *partition;
	struct raw_partition *raw_partition;
	uint64_t b_start, b_end;
	int pages, retval;
	struct bdevint *bint;

	b_start = bdev_get_block(tape->bint, &bint, &b_end);
	if (!b_start) {
		debug_warn("Failed to allocate  a new block\n");
		return NULL;
	}

	partition = zalloc(sizeof(*partition), M_TAPE_PARTITION, Q_WAITOK);
	partition->size = size;
	partition->partition_id = partition_id;
	partition->tmaps_b_start = b_start;
	partition->tmaps_bint = bint;
	tape_partition_init(tape, partition);

	pages = partition->max_meta_tmaps + (PARTITION_HEADER_SIZE >> LBA_SHIFT);
	retval = tcache_zero_range(bint, b_start, pages);
	if (unlikely(retval != 0)) {
		debug_warn("Cannot zero tmaps metadata\n");
		bdev_release_block(bint, b_start);
		free(partition, M_TAPE_PARTITION);
		return NULL;
	}

	pages = partition->max_data_tmaps;
	retval = tcache_zero_range(bint, tmap_bstart(partition, SEGMENT_TYPE_DATA, 0), pages);
	if (unlikely(retval != 0)) {
		debug_warn("Cannot zero tmaps metadata\n");
		bdev_release_block(bint, b_start);
		free(partition, M_TAPE_PARTITION);
		return NULL;
	}

	raw_partition = tape_get_raw_partition_info(tape, partition_id);
	SET_BLOCK(raw_partition->tmaps_block, b_start, bint->bid);
	raw_partition->size = size;
 
	return partition;
}

static uint64_t
tmap_get_usage(struct tsegment_map *tmap)
{
	struct tsegment_entry *entry;
	uint64_t used = 0;
	int i;

	for (i = 0; i < TSEGMENT_MAP_MAX_SEGMENTS; i++) {
		entry = tmap_segment_entry(tmap, i);
		if (!entry->block)
			continue;
		used += BINT_UNIT_SIZE;
	}
	return used;
}

static uint64_t 
tmaps_get_usage(struct tape_partition *partition, int type, int *error)
{
	struct tsegment_map *tmap;
	int max_tmaps, i;
	uint64_t used, total_used = 0;

	max_tmaps = tape_partition_get_max_tmaps(partition, type);
	for (i = 0; i < max_tmaps; i++) {
		tmap = tmap_locate(partition, type, i);
		if (unlikely(!tmap)) {
			*error = -1;
			continue;
		}

		used = tmap_get_usage(tmap);
		if (!used)
			break;
		total_used += used;
	}
	return total_used;
}

struct tape_partition *
tape_partition_load(struct tape *tape, int partition_id)
{
	struct raw_partition *raw_partition;
	struct tape_partition *partition;
	uint64_t b_start;
	struct bdevint *bint;
	struct tsegment *tsegment;
	struct map_lookup *mlookup;
	struct map_lookup_entry *entry;
	struct blk_map *map;
	int retval, error, iszero;
	uint64_t total_used = 0, used;
	pagestruct_t *page;

	raw_partition = tape_get_raw_partition_info(tape, partition_id);
	debug_check(!raw_partition->tmaps_block);

	b_start = BLOCK_BLOCKNR(raw_partition->tmaps_block);
	bint = bdev_find(BLOCK_BID(raw_partition->tmaps_block));
	if (unlikely(!bint)) {
		debug_warn("Cannot find bint at bid %u\n", BLOCK_BID(raw_partition->tmaps_block));
		return NULL;
	}

	partition = zalloc(sizeof(*partition), M_TAPE_PARTITION, Q_WAITOK);
	partition->size = raw_partition->size;
	debug_check(!raw_partition->size);
	partition->partition_id = partition_id;
	partition->tmaps_b_start = b_start;
	partition->tmaps_bint = bint;
	tape_partition_init(tape, partition);

	error = 0;
	used = tmaps_get_usage(partition, SEGMENT_TYPE_META, &error);
	if (unlikely(error != 0)) {
		free(partition, M_TAPE_PARTITION);
		return NULL;
	}
	total_used += used;

	used = tmaps_get_usage(partition, SEGMENT_TYPE_DATA, &error);
	if (unlikely(error != 0)) {
		free(partition, M_TAPE_PARTITION);
		return NULL;
	}
	total_used += used;
	partition->used = total_used;
	atomic_set_bit(PARTITION_LOOKUP_SEGMENTS, &partition->flags);

	tsegment = &partition->msegment;
	retval = tape_partition_lookup_segment(partition, SEGMENT_TYPE_META, 0, tsegment);
	if (!tsegment->b_start)
		return partition;

	page = vm_pg_alloc(0);
	if (unlikely(!page)) {
		tape_partition_free(partition, 0);
		return NULL;
	}

	retval = qs_lib_bio_lba(tsegment->bint, tsegment->b_start, page, QS_IO_READ, 0);
	if (unlikely(retval != 0)) {
		vm_pg_free(page);
		tape_partition_free(partition, 0);
		return NULL;
	}

	iszero = zero_page(page);
	vm_pg_free(page);
	if (iszero)
		return partition;

	mlookup = map_lookup_load(partition, tsegment->b_start, tsegment->bint->bid);
	if (unlikely(!mlookup)) {
		debug_warn("Cannot load first mlookup. Cannot continue with this partition\n");
		tape_partition_free(partition, 0);
		return NULL;
	}
	TAILQ_INSERT_HEAD(&partition->mlookup_list, mlookup, l_list);

	entry = map_lookup_first_blkmap(mlookup);
	if (unlikely(!entry)) {
		debug_warn("Cannot load first map. Cannot continue with this partition\n");
		tape_partition_free(partition, 0);
		return NULL;
	}

	map = blk_map_load(partition, BLOCK_BLOCKNR(entry->block), BLOCK_BID(entry->block), 0, mlookup, 0, 0);
	if (unlikely(!map)) {
		debug_warn("Cannot load first map. Cannot continue with this partition\n");
		tape_partition_free(partition, 0);
		return NULL;
	}
	TAILQ_INSERT_HEAD(&partition->map_list, map, m_list);
	partition->cur_map = map;
	tape_partition_print_cur_position(partition, "After LOAD");
	return partition;
}

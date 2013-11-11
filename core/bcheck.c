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
#include "tape.h"
#include "bdev.h"
#include "mchanger.h"
#include "blk_map.h"

uint64_t reclaimed;
uint64_t restored;

static struct bintindex *
bint_get_check_index(struct bdevint *bint, int index_id, int alloc)
{
	struct bintindex *index;

	STAILQ_FOREACH(index, &bint->check_list, i_list) {
		if (index->index_id == index_id)
			return index;
	}

	if (!alloc)
		return NULL;

	index = zalloc(sizeof(struct bintindex), M_BINDEX, Q_WAITOK);
	index->metadata = vm_pg_alloc(VM_ALLOC_ZERO);
	if (unlikely(!index->metadata)) {
		debug_warn("Unable to allocate index's metadata\n");
		free(index, M_BINDEX);
		return NULL;
	}

	TAILQ_INIT(&index->unmap_list);
	index->index_wait = wait_chan_alloc("bint index wait");
	index->b_start = bint_index_bstart(bint, index_id);
	index->index_id = index_id;
	index->bint = bint;
	STAILQ_INSERT_TAIL(&bint->check_list, index, i_list);
	return index;
}

static int
check_block(struct bdevint *bint, uint64_t block, uint32_t bid)
{
	struct bintindex *index;
	int index_id;
	int entry_id, pos_id;
	uint8_t *bmap;

	if (bint->bid != bid)
	{
		return 0;
	}

	index_id = calc_index_id(bint, block, &entry_id, &pos_id);
	index = bint_get_check_index(bint, index_id, 1);
	if (!index) {
		debug_warn("Failed to get index bid %u block %llu index_id %llu\n", bid, (unsigned long long)block, (unsigned long long)index_id);
		return -1;
	}

	bmap = (uint8_t *)(vm_pg_address(index->metadata));
	if (bmap[entry_id] & (1 << (uint8_t)pos_id)) {
		debug_warn("Multiple refs index bid %u block %llu index_id %llu entry_id %d pos id %d\n", bid, (unsigned long long)block, (unsigned long long)index_id, entry_id, pos_id);
	}
	bmap[entry_id] |= (1 << (uint8_t)pos_id);
	return 0;
}

static int
tmap_check_block(struct tape_partition *partition, struct tsegment_map *tmap, struct bdevint *bint, int type)
{
	struct tsegment_entry *entry;
	int i;

	for (i = 0; i < TSEGMENT_MAP_MAX_SEGMENTS; i++) {
		entry = tmap_segment_entry(tmap, i);
		if (!entry->block)
			return 1;

		if (tmap_skip_segment(partition, tmap->tmap_id, i, type))
			continue;
		if (check_block(bint, BLOCK_BLOCKNR(entry->block), BLOCK_BID(entry->block)) != 0)
			return -1;
	}
	return 0;
}

static int
tape_partition_check_tmap_blocks(struct tape_partition *partition, struct bdevint *bint, int type)
{
	struct tsegment_map *tmap;
	int max_tmaps, i, retval;

	max_tmaps = tape_partition_get_max_tmaps(partition, type);
	for (i = 0; i < max_tmaps; i++) {
		tmap = tmap_locate(partition, type, i);
		if (unlikely(!tmap))
			return -1;

		retval = tmap_check_block(partition, tmap, bint, type);
		if (retval < 0)
			return retval;
		else if (retval > 0)
			break;
	}
	return 0;
}


static int
tape_partition_check_block(struct tape_partition *partition, struct bdevint *bint)
{
	int retval;

	retval = tape_partition_check_tmap_blocks(partition, bint, SEGMENT_TYPE_META);
	if (retval != 0)
		return -1;
 
	retval = tape_partition_check_tmap_blocks(partition, bint, SEGMENT_TYPE_DATA);
	if (retval != 0)
		return -1;

	if (partition->tmaps_bint != bint)
		return 0;

	retval = check_block(bint, partition->tmaps_b_start, partition->tmaps_bint->bid);
	return retval;
}

static int
tape_check_block(struct tape *tape, struct bdevint *bint)
{
	struct tape_partition *partition;
	int retval;

	SLIST_FOREACH(partition, &tape->partition_list, p_list) {
		retval = tape_partition_check_block(partition, bint);
		tape_partition_unload(partition);
		if (retval)
			return retval;
	}
	return 0;
}

static int
mchanger_check_export_list(struct mchanger *mchanger, struct bdevint *bint)
{
	struct tape *tape;
	int retval;

	LIST_FOREACH(tape, &mchanger->export_list, t_list) {
		retval = tape_check_block(tape, bint);
		if (retval)
			return retval;
	}
	return 0;
}

static int
mchanger_check_elements(struct mchanger_element_list *element_list, struct bdevint *bint)
{
	struct mchanger_element *element;
	struct tape *tape;
	int retval;

	STAILQ_FOREACH(element, element_list, me_list) {
		tape = element_vcartridge(element);
		if (!tape)
			continue;

		retval = tape_check_block(tape, bint);
		if (retval)
			return retval;
	}
	return 0;
}

extern struct tdevice *tdevices[];

static int
tdrive_check_block(struct tdrive *tdrive, struct bdevint *bint)
{
	struct tape *tape;
	int retval;

	LIST_FOREACH(tape, &tdrive->media_list, t_list) {
		retval = tape_check_block(tape, bint);
		if (retval)
			return retval;
	}
	return 0;
}

static int
mchanger_check_block(struct mchanger *mchanger, struct bdevint *bint)
{
	int retval;

	retval = mchanger_check_elements(&mchanger->selem_list, bint);
	if (retval)
		return retval;

	retval = mchanger_check_elements(&mchanger->ielem_list, bint);
	if (retval)
		return retval;

	retval = mchanger_check_elements(&mchanger->delem_list, bint);
	if (retval)
		return retval;

	retval = mchanger_check_export_list(mchanger, bint);
	return retval;
}

static int
device_check_block(struct bdevint *bint)
{
	struct tdevice *device;
	int i;
	int retval;

	for (i = 0; i < TL_MAX_DEVICES; i++)
	{
		device = tdevices[i];
		if (!device)
			continue;

		if (device->type == T_CHANGER)
			retval = mchanger_check_block((struct mchanger *)device, bint);
		else
			retval = tdrive_check_block((struct tdrive *)device, bint);

		if (retval != 0)
			return retval;
	}
	return 0;
}

static uint8_t
get_alloced_blocks(uint8_t val)
{
	int j;
	int alloced = 0;

	for (j = 0; j < 8; j++) {
		if (val & (1 << j))
			alloced++;
	}
	return alloced;
}

static int
bint_index_check(struct bdevint *bint, int index_id)
{
	uint8_t *bmap, *check_bmap;
	int i, bmap_entries;
	int retval, need_sync = 0;
	uint64_t freed_blocks = 0;
	uint64_t alloced_blocks = 0;
	int index_used, check_used;
	struct bintindex *index;
	struct bintindex *check_index;

	check_index = bint_get_check_index(bint, index_id, 0);
	index = bint_get_index(bint, (int)index_id);
	if (unlikely(!index)) {
		debug_warn("Cannot locate index at id %d\n", (int)index_id);
		if (check_index) {
			STAILQ_REMOVE(&bint->check_list, check_index, bintindex, i_list); 
			bint_index_free(check_index);
		}

		return -1;
	}

	bmap = (uint8_t *)(vm_pg_address(index->metadata));
	if (check_index)
		check_bmap = (uint8_t *)(vm_pg_address(check_index->metadata));
	bmap_entries = calc_bmap_entries(bint, index_id);
	for (i = 0; i < bmap_entries; i++) {
		uint8_t val;
		uint8_t check_val = 0;

	       	val = bmap[i];
		if (check_index)
	       		check_val = check_bmap[i];

		if (val == check_val)
			continue;

		debug_warn("index id %llu val %u check_val %u check_used %u index_used %u \n", (unsigned long long)index_id, val, check_val, check_used, index_used);
		index_used = get_alloced_blocks(val);
		check_used = get_alloced_blocks(check_val);
		if (check_used > index_used)
			alloced_blocks += (check_used - index_used);
		else
			freed_blocks += (index_used - check_used);
	       	bmap[i] = check_val;
		need_sync = 1;
	}

	if (check_index) {
		STAILQ_REMOVE(&bint->check_list, check_index, bintindex, i_list); 
		bint_index_free(check_index);
	}

	if (!need_sync)
		return 0;

	retval = bint_index_io(bint, index, QS_IO_SYNC);
	if (unlikely(retval < 0)) {
		return -1;
	}

	reclaimed += freed_blocks;
	bint_incr_free(bint, (freed_blocks << BINT_UNIT_SHIFT));
	restored += alloced_blocks;
	bint_decr_free(bint, (alloced_blocks << BINT_UNIT_SHIFT));
	return 0;
}

static int
bint_check(struct bdevint *bint)
{
	int i, retval, nindexes;

	retval = device_check_block(bint);
	if (retval != 0) {
		debug_warn("bint check failed\n");
		return -1;
	}

	nindexes = bint_nindexes(bint->usize);
	for (i = 0; i < nindexes; i++) {
		retval = bint_index_check(bint, i);
		if (unlikely(retval < 0)) {
			debug_warn("index check failed for %u index id %d\n", bint->bid, i);
			return -1;
		}
	}
	return 0;
}

int
bdev_check_disks(void)
{
	int i;
	struct bdevint *bint;

	for (i = 0; i < TL_MAX_DISKS; i++) {
		bint = bint_list[i];
		if (!bint)
			continue;
		reclaimed = 0;
		restored = 0;
		bint_check(bint);
		if (restored) {
			debug_warn("For bint %u restored %llu units\n", bint->bid, (unsigned long long)(restored));
		}
		if (reclaimed) {
			debug_warn("For bint %u reclaimed back %llu units\n", bint->bid, (unsigned long long)(reclaimed));
		}
	}
	return 0;
}

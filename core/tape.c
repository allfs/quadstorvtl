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
#include "tape.h"
#include "map_lookup.h"
#include "bdevgroup.h"
#include "qs_lib.h"
#include "mchanger.h"

extern uma_t *tape_cache;

struct raw_partition *
tape_get_raw_partition_info(struct tape *tape, int partition_id)
{
	struct raw_tape *raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));

	return &raw_tape->raw_partitions[partition_id];
}

static void
tape_free_partitions(struct tape *tape, int free_alloc)
{
	struct tape_partition *partition;

	while ((partition = SLIST_FIRST(&tape->partition_list))) {
		SLIST_REMOVE_HEAD(&tape->partition_list, p_list);
		tape_partition_free(partition, free_alloc);
	}	
}

void
tape_free(struct tape *tape, int free_alloc)
{
	tape_free_partitions(tape, free_alloc);

	if (tape->metadata)
		vm_pg_free(tape->metadata);

	uma_zfree(tape_cache, tape);
}

static struct tape *
tape_alloc(struct vcartridge *vinfo)
{
	struct tape *tape;

	tape = __uma_zalloc(tape_cache, Q_WAITOK|Q_ZERO, sizeof(*tape));
	if (unlikely(!tape))
		return NULL;

	tape->metadata = vm_pg_alloc(VM_ALLOC_ZERO);
	if (unlikely(!tape->metadata)) {
		uma_zfree(tape_cache, tape);
		return NULL;
	}

	SLIST_INIT(&tape->partition_list);
	tape->group = bdev_group_locate(vinfo->group_id);
	if (unlikely(!tape->group)) {
		debug_warn("Cannot locate pool at %u\n", vinfo->group_id);
		tape_free(tape, 0);
		return NULL;
	}

	tape->bint = tape->group->master_bint;
	if (unlikely(!tape->bint)) {
		debug_warn("Cannot locate bint master for pool %s\n", tape->group->name);
		tape_free(tape, 0);
		return NULL;
	}
 
	tape->make = vinfo->type;
	tape->set_size = tape->size = vinfo->size;
	tape->tl_id = vinfo->tl_id;
	tape->tape_id = vinfo->tape_id;
	tape->worm = vinfo->worm || tape->group->worm;
	tape->b_start = bdev_tape_bstart(tape->bint, tape->tape_id);
	debug_info("tape bstart %llu tape_id %u\n", (unsigned long long)tape->b_start, tape->tape_id);
	strcpy(tape->label, vinfo->label);
	return tape;
}

static int
tape_validate_csum(struct tape *tape)
{
	struct raw_tape *raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));
	uint16_t csum;

	csum = net_calc_csum16(vm_pg_address(tape->metadata) + sizeof(uint16_t), LBA_SIZE - (sizeof(uint16_t)));
	if (raw_tape->csum != csum) {
		debug_warn("Mismatch in csum got %x stored %x\n", csum, raw_tape->csum);
	}
	return 0;
}

static int 
tape_validate_metadata(struct tdevice *tdevice, struct tape *tape, struct vcartridge *vinfo)
{
	struct raw_tape *raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));

	if (tape_validate_csum(tape) != 0)
		return -1;

	debug_info("raw tape label %s\n", raw_tape->label);
	if (strcmp(raw_tape->label, tape->label)) {
		debug_warn("Mismatch in tape label expected %s found %.36s\n", tape->label, raw_tape->label);
		return -1;
	}

	if (vinfo->worm != tape->worm) {
		debug_warn("Mismatch in worm property expected %d found %d\n", tape->worm, raw_tape->worm);
		return -1;
	}

	tape->set_size = raw_tape->set_size;
	debug_info("size %llu set size %llu\n", (unsigned long long)tape->size, (unsigned long long)tape->set_size);
	return 0;
}

static void
tape_init_metadata(struct tdevice *tdevice, struct tape *tape)
{
	struct raw_tape *raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));

	strcpy(raw_tape->label, tape->label);
	raw_tape->tape_id = tape->tape_id;
	raw_tape->device_type = tdevice->type;
	raw_tape->group_id = tape->group->group_id;
	raw_tape->make = tape->make;
	raw_tape->size = tape->size; 
	raw_tape->set_size = tape->set_size; 
	raw_tape->worm = tape->worm;

	if (tdevice->type == T_SEQUENTIAL)
		tdrive_init_tape_metadata(tdevice, tape);
	else
		mchanger_init_tape_metadata(tdevice, tape);
}

static int
tape_read_metadata(struct tape *tape)
{
	int retval;

	retval = qs_lib_bio_lba(tape->bint, tape->b_start, tape->metadata, QS_IO_READ, 0);
	return retval;
}

static void 
tape_write_csum(struct tape *tape)
{
	struct raw_tape *raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));
	uint16_t csum;

	csum = net_calc_csum16(vm_pg_address(tape->metadata) + sizeof(uint16_t), LBA_SIZE - (sizeof(uint16_t)));
	raw_tape->csum = csum;
}

int
tape_write_metadata(struct tape *tape)
{
	int retval;

	tape_write_csum(tape);
	retval = qs_lib_bio_lba(tape->bint, tape->b_start, tape->metadata, QS_IO_WRITE, 0);
	return retval;
}

int
tape_partition_count(struct tape *tape)
{
	struct tape_partition *partition;
	int count = 0;

	SLIST_FOREACH(partition, &tape->partition_list, p_list) {
		count++;
	}
	return count;
}

int
tape_delete_partition(struct tape *tape, uint8_t pnum)
{
	struct tape_partition *partition;
	int retval;

	debug_info("partition id %d\n", pnum);
	partition = tape_get_partition(tape, pnum);

	if (unlikely(!partition)) {
		debug_warn("Invalid partition number %d\n", pnum);
		return -1;
	}

	debug_info("partition size %llu used %llu\n", (unsigned long long)partition->size, (unsigned long long)partition->used);
	retval = tape_partition_free_alloc(partition, 1);
	if (unlikely(retval != 0)) {
		debug_warn("free alloc failed\n");
		return -1;
	}

	retval = tape_partition_free_tmaps_block(partition);
	if (unlikely(retval != 0)) {
		debug_warn("free tmaps block failed\n");
		return -1;
	}

	SLIST_REMOVE(&tape->partition_list, partition, tape_partition, p_list);
	tape_partition_free(partition, 0);
	return 0;
}

int
tape_add_partition(struct tape *tape, uint64_t size, uint8_t pnum)
{
	struct tape_partition *partition;
	int retval;

	debug_info("size %llu pnum %d\n", (unsigned long long)size, pnum);
	partition = tape_partition_new(tape, size, pnum);
	if (!partition) {
		debug_warn("Cannot create a new partition\n");
		return -1;
	}

	retval = tape_write_metadata(tape);
	if (unlikely(retval != 0)) {
		tape_partition_free(partition, 1);
		return -1;
	}
	SLIST_INSERT_HEAD(&tape->partition_list, partition, p_list);

	return 0;
}

struct tape *
tape_new(struct tdevice *tdevice, struct vcartridge *vinfo)
{
	struct tape *tape;
	struct tape_partition *partition;
	int retval;

	tape = tape_alloc(vinfo);
	if (unlikely(!tape))
		return NULL;

	tape_init_metadata(tdevice, tape);
	partition = tape_partition_new(tape, tape->size, 0);
	if (!partition) {
		debug_warn("Cannot create a new partition\n");
		tape_free(tape, 0);
		return NULL;
	}
	SLIST_INSERT_HEAD(&tape->partition_list, partition, p_list);
	tape->cur_partition = partition;

	retval = tape_write_metadata(tape);
	if (unlikely(retval != 0)) {
		debug_warn("Failed to write tape metadata\n");
		tape_free(tape, 1);
		return NULL;
	}
	return tape;
}

struct tape *
tape_load(struct tdevice *tdevice, struct vcartridge *vinfo)
{
	struct tape *tape;
	struct raw_partition *raw_partition;
	struct tape_partition *partition;
	int retval;
	int i;

	tape = tape_alloc(vinfo);
	if (unlikely(!tape))
		return NULL;

	retval = tape_read_metadata(tape);
	if (unlikely(retval != 0)) {
		debug_warn("Failed to read tape metadata\n");
		tape_free(tape, 0);
		return NULL;
	}

	tape_validate_metadata(tdevice, tape, vinfo);

	for (i = 0; i < MAX_TAPE_PARTITIONS; i++) {
		raw_partition = tape_get_raw_partition_info(tape, i);
		if (!raw_partition->size)
			break;

		partition = tape_partition_load(tape, i);
		if (!partition) {
			debug_warn("Cannot load partition\n");
			tape_free(tape, 0);
			return NULL;
		}
		tape_partition_unload(partition);
		SLIST_INSERT_HEAD(&tape->partition_list, partition, p_list);
		if (!i)
			tape->cur_partition = partition;
	}

	if (unlikely(!tape->cur_partition)) {
		tape_free(tape, 0);
		return NULL;
	}

	return tape;
}

int
tape_partition_set_size(struct tape_partition *partition, uint64_t set_size, int set_tape_size)
{
	struct tape *tape = partition->tape;
	struct raw_tape *raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));
	struct raw_partition *raw_partition;
	int retval;

	debug_info("partition id %d set size %llu set tape size %d\n", partition->partition_id, (unsigned long long)set_size, set_tape_size);
	debug_info("partition size %llu tape size %llu tape set size %llu\n", (unsigned long long)partition->size, (unsigned long long)tape->size, (unsigned long long)tape->set_size);
	raw_partition = tape_get_raw_partition_info(tape, 0);
	raw_partition->size = set_size;
	if (set_tape_size)
		raw_tape->set_size = set_size;

	retval = tape_write_metadata(tape);
	if (retval != 0)
		goto err;

	if (set_tape_size)
		tape->set_size = set_size;
	tape->cur_partition->size = set_size;
	debug_info("now partition size %llu tape size %llu tape set size %llu\n", (unsigned long long)partition->size, (unsigned long long)tape->size, (unsigned long long)tape->set_size);

	return 0;
err:
	raw_partition->size = tape->cur_partition->size;
	if (set_tape_size)
		raw_tape->set_size = tape->set_size;
	return -1;
}

int
tape_format_default(struct tape *tape, uint64_t set_size, int set_tape_size)
{
	struct tape_partition *partition;
	int retval;
	int count, i;

	count = tape_partition_count(tape);
	debug_info("set size %llu set tape size %d count %d\n", (unsigned long long)set_size, set_tape_size, count);
	for (i = 1; i < count; i++) {
		retval = tape_delete_partition(tape, i);
		if (unlikely(retval != 0)) {
			return -1;
		}
	}

	partition = tape_get_partition(tape, 0);
	debug_check(tape->cur_partition != partition);
	debug_info("before format partition used %llu\n", (unsigned long long)partition->used);
	retval = tape_partition_free_alloc(partition, 1);
	debug_info("after format partition used %llu\n", (unsigned long long)partition->used);
	if (retval != 0)
		return -1;

	retval = tape_partition_set_size(partition, set_size, set_tape_size);
	return retval;
}

uint64_t
tape_usage(struct tape *tape)
{
	uint64_t used = 0;
	struct tape_partition *partition;

	SLIST_FOREACH(partition, &tape->partition_list, p_list) {
		used += partition->used;
	}

	return used;
}

int
tape_cmd_erase(struct tape *tape)
{
	int retval;

	retval = tape_flush_buffers(tape);
	if (unlikely(retval != 0)) {
		debug_warn("tape_flush_buffers failed\n");
		return -1;
	}

	return tape_partition_erase(tape->cur_partition);
}

/* Rewind the tape to the begining of the the current partition */

int tape_flush_buffers(struct tape *tape)
{
	/* Flushes the cache basically */
	return tape_partition_flush_buffers(tape->cur_partition);
}

static int
tape_position_bot(struct tape *tape, int bot)
{
	int retval;

	if (bot) {
		retval = tape_set_partition(tape, 0);
		if (unlikely(retval != 0))
			return retval;
	}
	return tape_partition_position_bop(tape->cur_partition);
}

int tape_cmd_load(struct tape *tape, uint8_t eot)
{
	int retval;

	if (eot)
	{
		/* Not supported till figured out */
		debug_warn("Received tape load for eot. Not supported\n");
		return -1;
	}

	retval = tape_position_bot(tape, 1);
	if (unlikely(retval != 0))
	{
		debug_warn("tape_position_bot failed\n");
		return -1;
	}

	return 0; 
}

int tape_cmd_unload(struct tape *tape, int rewind)
{
	struct tape_partition *partition;
	int retval;

	retval = tape_flush_buffers(tape);
	if (unlikely(retval != 0))
	{
		debug_warn("tape_flush_buffers failed\n");
		return -1;
	}


	if (rewind) {
		retval = tape_cmd_rewind(tape, 1);
		if (unlikely(retval != 0))
		{
			debug_warn("tape_cmd_rewind failed\n");
			return -1;
		}
	}

	SLIST_FOREACH(partition, &tape->partition_list, p_list) {
		tape_partition_unload(partition);
	}
	return 0;
}

int
tape_cmd_rewind(struct tape *tape, int bot)
{
	int retval;

	retval = tape_flush_buffers(tape);
	if (unlikely(retval != 0))
	{
		debug_warn("tape_flush_buffers failed\n");
		return -1;
	}

	retval = tape_position_bot(tape, bot);

	if (unlikely(retval != 0)) {
		debug_warn("tape position bot/bop failed\n");
		return -1;
	}
	return 0;
}

int
tape_at_bop(struct tape *tape)
{
	return tape_partition_at_bop(tape->cur_partition);
}

int
tape_at_bot(struct tape *tape)
{
	if (tape->cur_partition->partition_id)
		return 0;

	return tape_at_bop(tape);
}

void
tape_cmd_read_position(struct tape *tape, struct qsio_scsiio *ctio, uint8_t service_action)
{
	tape_partition_read_position(tape->cur_partition, ctio, service_action);
}

struct tape_partition *
tape_get_partition(struct tape *tape, uint8_t pnum)
{
	struct tape_partition *partition;

	SLIST_FOREACH(partition, &tape->partition_list, p_list) {
		if (partition->partition_id == pnum)
			return partition;
	}
	return NULL;
}

int 
tape_set_partition(struct tape *tape, uint8_t pnum)
{
	struct tape_partition *partition;

	if (tape->cur_partition->partition_id == pnum)
		return 0;

	tape_flush_buffers(tape);
	SLIST_FOREACH(partition, &tape->partition_list, p_list) {
		if (partition->partition_id == pnum) {
			tape->cur_partition = partition;
			return 0;
		}
	}
	return INVALID_PARTITION;
}

int
tape_cmd_locate(struct tape *tape, uint64_t block_address, uint8_t cp, uint8_t pnum, uint8_t locate_type)
{
	int retval;

	if (cp) {
		retval = tape_set_partition(tape, pnum);
		if (unlikely(retval != 0))
			return retval;
	}
	return tape_partition_locate(tape->cur_partition, block_address, locate_type);
}

int
tape_cmd_read(struct tape *tape, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint8_t fixed, uint32_t *blocks_read, uint32_t *ili_block_size)
{
	return tape_partition_read(tape->cur_partition, ctio, block_size, num_blocks, fixed, blocks_read, ili_block_size);
}

int
tape_cmd_write_filemarks(struct tape *tape, uint8_t wmsk, uint32_t transfer_length)
{
	return tape_partition_write_filemarks(tape->cur_partition, wmsk, transfer_length);
}

int
tape_cmd_write(struct tape *tape, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint32_t *blocks_written, uint8_t compression_enabled)
{
	return tape_partition_write(tape->cur_partition, ctio, block_size, num_blocks, blocks_written, compression_enabled);
}

int
tape_cmd_space(struct tape *tape, uint8_t code, int *count)
{
	return tape_partition_space(tape->cur_partition, code, count);
}

int
tape_validate_format(struct tape *tape)
{
	struct tape_partition *partition;

	if (!tape->worm)
		return 0;

	SLIST_FOREACH(partition, &tape->partition_list, p_list) {
		if (!TAILQ_EMPTY(&partition->mlookup_list))
			return -1;
	}
	return 0;
}

void
tape_get_info(struct tape *tape, struct vcartridge *vcartridge)
{
	struct raw_tape *raw_tape;

	raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));
	vcartridge->used = tape_usage(tape);
	vcartridge->vstatus = raw_tape->vstatus;
}

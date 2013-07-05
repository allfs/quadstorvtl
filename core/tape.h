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

#ifndef QUADSTOR_TAPE_H_
#define QUADSTOR_TAPE_H_

#include "tdrive.h"
#include "bdev.h"
#include "../common/commondefs.h"

enum {
	TAPE_FLAGS_DISABLE = 0x02,
};

struct tape {
	struct bdevint *bint;
	struct bdevgroup *group;

	uint64_t size; /* Size of tape in MB/GB */
	uint64_t set_size; /* SET CAPACITY size */
	uint64_t b_start; /* Start block on disk */
	uint64_t export_date;

	struct tdrive *locked_by;
	struct tdrive *prev_drive;
	LIST_ENTRY(tape) t_list;

	int tl_id;
	uint32_t tape_id;
	uint16_t locked; /* Locked for export */
	uint16_t locked_op;

	int ddenabled;
	int make; /* Make for this tape */
	int worm; /* Worm enabled */
	char label[40];
	unsigned long flags;
	struct tape_partition *cur_partition;
	SLIST_HEAD(, tape_partition) partition_list;
	pagestruct_t *metadata;
};

struct tape *tape_new(struct tdevice *tdevice, struct vcartridge *vinfo);
struct tape *tape_load(struct tdevice *tdevice, struct vcartridge *vinfo);
void tape_free(struct tape *tape, int free_alloc);
int tape_read_entry_position(struct tape *tape, struct tl_entryinfo *entryinfo);
int tape_flush_buffers(struct tape *tape);

/* SCSI command supports */
int tape_cmd_erase(struct tape *tape);
uint64_t tape_usage(struct tape *tape);

int tape_cmd_load(struct tape *tape, uint8_t eot);
int tape_cmd_unload(struct tape *tape, int rewind);
int tape_cmd_rewind(struct tape *tape, int bot);
void tape_cmd_read_position(struct tape *tape, struct qsio_scsiio *ctio, uint8_t service_action);
int tape_cmd_locate(struct tape *tape, uint64_t block_address, uint8_t cp, uint8_t pnum, uint8_t locate_type);
int tape_cmd_write_filemarks(struct tape *tape, uint8_t wmsk, uint32_t transfer_length);
int tape_cmd_write(struct tape *tape, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint32_t *blocks_written, uint8_t compression_enabled, uint32_t *compressed_size);
int tape_cmd_read(struct tape *tape, struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, uint8_t fixed,  uint32_t *blocks_read, uint32_t *ili_block_size, uint32_t *compressed_size);
int tape_cmd_space(struct tape *tape, uint8_t code, int *count);
int tape_at_bop(struct tape *tape);
int tape_at_bot(struct tape *tape);
int tape_write_metadata(struct tape *tape);
struct raw_partition *tape_get_raw_partition_info(struct tape *tape, int partition_id);
int tape_format_default(struct tape *tape, uint64_t set_size, int set_tape_size);
int tape_add_partition(struct tape *tape, uint64_t size, uint8_t pnum);
int tape_delete_partition(struct tape *tape, uint8_t pnum);
int tape_partition_count(struct tape *tape);
struct tape_partition * tape_get_partition(struct tape *tape, uint8_t pnum);
int tape_set_partition(struct tape *tape, uint8_t pnum);
int tape_partition_set_size(struct tape_partition *partition, uint64_t set_size, int set_tape_size);
int tape_validate_format(struct tape *tape);
void tape_get_info(struct tape *tape, struct vcartridge *vcartridge);

#endif

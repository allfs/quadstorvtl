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

#ifndef QUADSTOR_BLK_ENTRY_H_
#define QUADSTOR_BLK_ENTRY_H_

/*
 * Comp Size  - 15 ( 512 blocks )
 * Block Size - 24
 * Block Type - 1 (1 bits for type)
 * TMap ID - 12 (Using a 12 k tmap block size - upto 32768 tmaps)
 * Segment ID - 9 (Using a 12 k tmap block size - upto 512 segments)
 */ 

struct raw_blk_entry {
	uint64_t block;
	uint64_t bits;
} __attribute__ ((__packed__));

#define ENTRY_SEGMENT_ID(BITS) (BITS & 0x3FFFFF)
#define SET_ENTRY_SEGMENT_ID(BITS,newid) (BITS = ((BITS & ~0x3FFFFF) | (uint64_t)newid))

#define ENTRY_BTYPE(BITS)  ((BITS >> 22) & 0x1)
#define SET_ENTRY_BTYPE(BITS,btype) (BITS |= ((uint64_t)btype << 22))

#define ENTRY_ESIZE(BITS) ((BITS >> 23) & 0xFFFFFF)
#define SET_ENTRY_ESIZE(BITS,esiz) (BITS |= ((uint64_t)esiz << 23))

#define ENTRY_CSIZE(BITS) ((BITS >> 47) & 0x7FFF) 
#define SET_ENTRY_CSIZE(BITS,csiz) (BITS |= ((uint64_t)csiz << 47))

static inline int
entry_is_data_block(struct blk_entry *entry)
{
	if (ENTRY_BTYPE(entry->bits))
		return 1;
	else
		return 0;
}

static inline int
entry_compressed(uint64_t bits)
{
	return (ENTRY_CSIZE(bits));
}

static inline int
entry_is_tapemark(struct blk_entry *entry)
{
	return (!entry_is_data_block(entry));
}

static inline int
entry_is_filemark(struct blk_entry *entry)
{
	if (entry_is_data_block(entry))
	{
		return 0;
	}

	return (ENTRY_CSIZE(entry->bits) == 0);
}

static inline int
entry_is_setmark(struct blk_entry *entry)
{
	if (entry_is_data_block(entry))
	{
		return 0;
	}

	return (ENTRY_CSIZE(entry->bits) != 0);
}

static inline void 
entry_set_data_block(struct blk_entry *entry)
{
	SET_ENTRY_BTYPE(entry->bits, 1);
}

static inline void 
entry_set_tapemark_type(struct blk_entry *entry, int wmsk)
{
	if (wmsk)
		SET_ENTRY_CSIZE(entry->bits, 1);
}

static inline uint32_t
entry_segment_id(struct blk_entry *entry)
{
	return (ENTRY_SEGMENT_ID(entry->bits));
}

static inline void
entry_set_segment_id(struct blk_entry *entry, uint32_t segment_id)
{
	debug_check(segment_id > 0x3FFFFFF);
	SET_ENTRY_SEGMENT_ID(entry->bits, segment_id);

}

static inline void
entry_clear_segment_id(uint64_t *cbits)
{
	*cbits &= ~0x3FFFFFF;
}

static inline void
entry_set_block_size(struct blk_entry *entry)
{
	SET_ENTRY_ESIZE(entry->bits, entry->block_size);
}

static inline void
entry_set_comp_size(struct blk_entry *entry)
{
	uint32_t cblocks;

	cblocks = entry->comp_size >> 9;
	if (entry->comp_size & 0x1FF)
		cblocks++;

	SET_ENTRY_CSIZE(entry->bits, cblocks);
}

static inline uint32_t
entry_comp_size(struct blk_entry *entry)
{
	uint32_t cblocks;

	cblocks = ENTRY_CSIZE(entry->bits);
	return (((cblocks) << 9));
}

static inline uint32_t
entry_block_size(struct blk_entry *entry)
{
	return ENTRY_ESIZE(entry->bits);
}

static inline uint32_t
entry_disk_size(struct blk_entry *entry)
{
	if (unlikely(!entry_is_data_block(entry)))
		return 0;

	if (entry_compressed(entry->bits))
		return entry_comp_size(entry);
	else
		return entry_block_size(entry);
}

#endif

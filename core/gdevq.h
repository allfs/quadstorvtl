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

#ifndef QUADSTOR_GDEVQ_H_
#define QUADSTOR_GDEVQ_H_

#include "coredefs.h"
#include "lzfP.h"
#include "blk_map.h"

struct qs_cdevq {
	uint8_t wrkmem[65536];
	kproc_t *task;
	SLIST_ENTRY(qs_cdevq) c_list;
	int exit_flags;
	int id;
};

#define GDEVQ_EXIT		0x02

extern struct blkentry_clist pending_comp_queue;
extern wait_chan_t *devq_comp_wait;

static inline void
gdevq_comp_insert(struct blk_entry *entry)
{
	entry->completion = wait_completion_alloc("entry compl");
	chan_lock(devq_comp_wait);
	STAILQ_INSERT_TAIL(&pending_comp_queue, entry, c_list);
	chan_wakeup_one_unlocked(devq_comp_wait);
	chan_unlock(devq_comp_wait);
}

int init_gdevq_threads(void);
void exit_gdevq_threads(void);
#endif

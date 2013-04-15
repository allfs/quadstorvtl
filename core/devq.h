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

#ifndef QUADSTOR_DEVQ_H_
#define QUADSTOR_DEVQ_H_

#include "coredefs.h"

struct tdevice;
struct qs_devq {
	struct ccb_list pending_queue;
	wait_chan_t *devq_wait;
	atomic_t pending_cmds;
	int flags;
	struct tdevice *tdevice;
	kproc_t *task;
	void (*proc_cmd)(void *drive, void *iop);
};

struct qs_devq *devq_init(uint32_t bus_id, uint32_t target_id, struct tdevice *tdevice, const char *name, void (*proc_cmd) (void *, void *));
void devq_exit(struct qs_devq *devq);
void devq_free_queue(struct qs_devq *devq);

enum {
	DEVQ_EXIT,
};


static inline void
devq_insert_ccb(struct qs_devq *devq, struct qsio_hdr *ccb_h)
{
	unsigned long flags;

	flags = 0;
	chan_lock(devq->devq_wait);
	STAILQ_INSERT_TAIL(&devq->pending_queue, ccb_h, c_list);
	atomic_inc(&devq->pending_cmds);
	chan_wakeup_unlocked(devq->devq_wait);
	chan_unlock(devq->devq_wait);
}
#endif

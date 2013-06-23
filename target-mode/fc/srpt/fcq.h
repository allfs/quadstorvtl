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

#ifndef QUADSTOR_FCQ_H_
#define QUADSTOR_FCQ_H_

#include <linuxdefs.h>
#include <exportdefs.h>
#include <missingdefs.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_transport_fc.h>

#define FC_TM_SUCCESS		0
#define FC_TM_FAILED		1

#include "sedefs.h"
#include "ib_srpt.h"

struct fcbridge {
	struct srpt_device *ha;
	struct fcq *fcq;
	TAILQ_ENTRY(fcbridge) b_list;
	__u32 id;
};

enum {
	SRPT_CMD_TYPE_CTIO,
	SRPT_CMD_TYPE_NOTIFY,
};

struct fcq {
	unsigned long flags;
	spinlock_t fcq_lock;
	wait_queue_head_t fcq_wait;
	STAILQ_HEAD(, se_cmd) pending_queue;
	struct task_struct *task;
};

static inline void
fcq_insert_cmd(struct fcbridge *fcbridge, struct se_cmd *se_cmd)
{
	struct fcq *fcq = fcbridge->fcq;
	unsigned long flags = 0;

	spin_lock_irqsave(&fcq->fcq_lock, flags);
	STAILQ_INSERT_TAIL(&fcq->pending_queue, se_cmd, q_list);
	spin_unlock_irqrestore(&fcq->fcq_lock, flags);
	wake_up(&fcq->fcq_wait);
}

struct fcq * fcq_init(struct fcbridge *fcbridge);
void fcq_exit(struct fcq *fcq);
int fcbridge_proc_cmd(void *bridge, void *iop);
int fcbridge_task_mgmt(struct fcbridge *fcbridge, struct qsio_immed_notify *notify);
void __ctio_free_data(struct qsio_scsiio *ctio);
void fcbridge_route_cmd_post(struct qsio_scsiio *ctio);

#endif

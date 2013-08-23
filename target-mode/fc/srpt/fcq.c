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

#include <linuxdefs.h>
#include <exportdefs.h>
#include <missingdefs.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport_fc.h>
#include "fcq.h"
#include "qla_sc.h"

static int
cmd_r_prt(struct srpt_device *vha)
{
	return (SRPT_RPORT_START);
}

static inline struct se_cmd *
get_next_cmd(struct fcq *fcq)
{
	struct se_cmd *cmd;
	unsigned long flags = 0;

	spin_lock_irqsave(&fcq->fcq_lock, flags);
	cmd = STAILQ_FIRST(&fcq->pending_queue);
	if (cmd)
		STAILQ_REMOVE_HEAD(&fcq->pending_queue, q_list); 
	spin_unlock_irqrestore(&fcq->fcq_lock, flags);
	return cmd;
}

static inline uint8_t *
srpt_cmd_cdb(struct se_cmd *cmd)
{
	return (cmd->t_task_cdb);
} 

extern struct qs_interface_cbs icbs;
extern sx_t itf_lock;
extern atomic_t alloced_cmds;

static inline void 
process_ctio(struct fcbridge *fcbridge, struct se_cmd *cmd)
{
	struct qsio_scsiio *ctio;
	struct qpriv *priv;
	uint32_t target_lun;

	if (cmd->ccb)
	{
		struct qsio_scsiio *ctio = (struct qsio_scsiio *)(cmd->ccb);
		struct tdevice *device = ctio->ccb_h.tdevice;

		if (device) {
			(*icbs.device_queue_ctio_direct) ((struct qsio_hdr *)ctio);
			return;
		}
		/* ctio completed (data recevied) */
		fcbridge_proc_cmd(fcbridge, cmd->ccb);
		return;
	}

	target_lun = cmd->unpacked_lun;
	sx_xlock(&itf_lock);
	if (atomic_read(&icbs.itf_enabled) && target_lun)
	{
		atomic_inc(&alloced_cmds);
		ctio = (*icbs.ctio_new)(Q_WAITOK);
	}
	else
	{
		ctio = __local_ctio_new(M_WAITOK);
		cmd->local_pool = 1;
	}
	sx_xunlock(&itf_lock);

	ctio->i_prt[0] = wwn_to_u64(cmd->sess->ch->i_port_id);
	ctio->i_prt[1] = wwn_to_u64(&cmd->sess->ch->i_port_id[8]);
	ctio->t_prt[0] = wwn_to_u64(cmd->sess->ch->t_port_id);
	ctio->t_prt[1] = wwn_to_u64(&cmd->sess->ch->t_port_id[8]);
	ctio->r_prt = cmd_r_prt(cmd->sess->vha);
	ctio->init_int = TARGET_INT_FC;
	memcpy(ctio->cdb, srpt_cmd_cdb(cmd), 16);
	ctio->ccb_h.flags = QSIO_DIR_OUT;
	ctio->ccb_h.flags |= QSIO_TYPE_CTIO;
	ctio->ccb_h.queue_fn = qla_end_ccb;
	ctio->ccb_h.target_lun = target_lun;
	ctio->task_attr = cmd->task_attr;
	ctio->task_tag = cmd->tag;

	priv = &ctio->ccb_h.priv.qpriv;
	priv->qcmd = cmd;
	priv->fcbridge = fcbridge;
	cmd->ccb = (struct qsio_hdr *)ctio;
	fcbridge_proc_cmd(fcbridge, ctio);
}

static void
process_inot(struct fcbridge *fcbridge, struct se_cmd *cmd)
{
	struct qsio_immed_notify *notify;
	struct qpriv *priv;

	DEBUG_BUG_ON(cmd->ccb);
	notify = kzalloc(sizeof(struct qsio_immed_notify), GFP_KERNEL|__GFP_NOFAIL);

	notify->i_prt[0] = wwn_to_u64(cmd->sess->ch->i_port_id);
	notify->i_prt[1] = wwn_to_u64(&cmd->sess->ch->i_port_id[8]);
	notify->t_prt[0] = wwn_to_u64(cmd->sess->ch->t_port_id);
	notify->t_prt[1] = wwn_to_u64(&cmd->sess->ch->t_port_id[8]);
	notify->r_prt = cmd_r_prt(cmd->sess->vha);
	notify->init_int = TARGET_INT_FC;
	notify->fn = cmd->notify_fn;
	notify->ccb_h.target_lun = cmd->unpacked_lun;
	notify->ccb_h.flags = QSIO_TYPE_NOTIFY;
	notify->ccb_h.queue_fn = qla_end_ccb;
	notify->task_tag = cmd->tag;

	priv = &notify->ccb_h.priv.qpriv;
	priv->qcmd = cmd;
	priv->fcbridge = fcbridge;
	cmd->ccb = (struct qsio_hdr *)notify;
	fcbridge_task_mgmt(fcbridge, notify);
}

static inline void 
process_cmd(struct fcbridge *fcbridge, struct se_cmd *cmd)
{
	if (cmd->type == SRPT_CMD_TYPE_CTIO)
		process_ctio(fcbridge, cmd);
	else
		process_inot(fcbridge, cmd);
}

/* process_queue returns only after draining the queue */
static inline void
process_queue(struct fcbridge *fcbridge)
{
	struct se_cmd *cmd;

	while ((cmd = get_next_cmd(fcbridge->fcq)) != NULL)
	{
		/* process the commands.  */
		process_cmd(fcbridge, cmd);
	}
}

extern mempool_t *q2t_pool;

static int
fcq_thread(void *data)
{
	struct fcbridge *fcbridge = data;
	struct fcq *fcq = fcbridge->fcq;

	set_user_nice(current, -20);

	__set_current_state(TASK_RUNNING);

	for (;;)
	{
		wait_event_interruptible(fcq->fcq_wait, !STAILQ_EMPTY(&fcq->pending_queue) || kthread_should_stop());

		if (unlikely(kthread_should_stop()))
		{
			break;
		}

		process_queue(fcbridge);
	}
	return 0;
}


void
fcq_exit(struct fcq *fcq)
{
	int err;

	err = kthread_stop(fcq->task);

	if (err)
	{
		DEBUG_WARN_NEW("Shutting down fcq thread failed\n");
		return;
	}	

	kfree(fcq);
}

struct fcq *
fcq_init(struct fcbridge *fcbridge)
{
	struct fcq *fcq;
	struct task_struct *task;

	fcq = kzalloc(sizeof(struct fcq), GFP_KERNEL);
	if (unlikely(!fcq))
	{
		return NULL;
	}

	init_waitqueue_head(&fcq->fcq_wait);
	STAILQ_INIT(&fcq->pending_queue);
	spin_lock_init(&fcq->fcq_lock);
	fcbridge->fcq = fcq;
	task = kthread_run(fcq_thread, fcbridge, "fcq");

	if (IS_ERR(task))
	{
		fcbridge->fcq = NULL;
		kfree(fcq);
		return NULL;
	}
	fcq->task = task;
	return fcq;
}

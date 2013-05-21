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

#include "fcq.h"
#include "qla_sc.h"

static struct tgtcmd *
get_next_cmd(struct fcq *fcq)
{
	struct tgtcmd *cmd;

	mtx_lock(&fcq->fcq_lock);
	cmd = STAILQ_FIRST(&fcq->pending_queue);
	if (cmd)
		STAILQ_REMOVE_HEAD(&fcq->pending_queue, q_list);
	mtx_unlock(&fcq->fcq_lock);
	return cmd;
}

static uint8_t *
q2t_cdb(struct tgtcmd *cmd)
{
	return cmd->cdb;
}

extern struct qs_interface_cbs icbs;
extern sx_t itf_lock;
extern atomic_t alloced_cmds;

static void 
process_ctio(struct fcbridge *fcbridge, struct tgtcmd *cmd)
{
	struct qsio_scsiio *ctio;
	struct qpriv *priv;

	if (cmd->ccb)
	{
		if (likely(cmd->ccb->flags & QSIO_TYPE_CTIO))
		{
			struct qsio_scsiio *ctio = (struct qsio_scsiio *)(cmd->ccb);
			struct tdevice *device = ctio->ccb_h.tdevice;

			if (device)
			{
				(*icbs.device_queue_ctio_direct) ((struct qsio_hdr *)ctio);
				return;
			}
		}
		/* ctio completed (data recevied) */
		fcbridge_proc_cmd(fcbridge, cmd->ccb);
		return;
	}

	sx_xlock(&itf_lock);
	if (atomic_read(&icbs.itf_enabled) && cmd->target_lun)
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

	ctio->i_prt[0] = cmd->i_prt;
	ctio->t_prt[0] = cmd->t_prt;
	ctio->r_prt = cmd->r_prt;
#if 0
	memcpy(&ctio->init_id, cmd->wwn, WWN_SIZE);
#endif
	ctio->init_int = TARGET_INT_FC;
	memcpy(ctio->cdb, q2t_cdb(cmd), 16);
	ctio->ccb_h.flags = QSIO_DIR_OUT;
	ctio->ccb_h.flags |= QSIO_TYPE_CTIO;
	ctio->ccb_h.queue_fn = qla_end_ccb;
	ctio->ccb_h.target_lun = cmd->target_lun;
	ctio->task_attr = cmd->tag_action;
	ctio->task_tag = cmd->tag_id; 

	priv = &ctio->ccb_h.priv.qpriv;
	priv->qcmd = cmd;
	priv->fcbridge = fcbridge;
	cmd->ccb = (struct qsio_hdr *)ctio;
	fcbridge_proc_cmd(fcbridge, ctio);
}

static void
process_inot(struct fcbridge *fcbridge, struct tgtcmd *cmd)
{
	struct qsio_immed_notify *notify;
	struct qpriv *priv;

	notify = zalloc(sizeof(struct qsio_immed_notify), M_QISP, M_WAITOK|__GFP_NOFAIL);

	notify->i_prt[0] = cmd->i_prt;
	notify->t_prt[0] = cmd->t_prt;
	notify->r_prt = cmd->r_prt;
	notify->task_tag = cmd->seq_id; 
#if 0
	memcpy(&notify->init_id, cmd->wwn, WWN_SIZE);
#endif
	notify->init_int = TARGET_INT_FC;
	notify->fn = cmd->nt_arg;
	notify->ccb_h.target_lun = cmd->target_lun;
	notify->ccb_h.flags = QSIO_TYPE_NOTIFY;
	notify->ccb_h.queue_fn = qla_end_ccb;

	priv = &notify->ccb_h.priv.qpriv;
	priv->qcmd = cmd;
	priv->fcbridge = fcbridge;
	DEBUG_BUG_ON(cmd->ccb);
	cmd->ccb = (struct qsio_hdr *)notify;
	fcbridge_task_mgmt(fcbridge, notify);
}

static void 
process_cmd(struct fcbridge *fcbridge, struct tgtcmd *cmd)
{
	if (unlikely(cmd->entry_type == RQSTYPE_NOTIFY))
		process_inot(fcbridge, cmd);
	else
		process_ctio(fcbridge, cmd);
}

/* process_queue returns only after draining the queue */
static void
process_queue(struct fcbridge *fcbridge, struct fcq *fcq)
{
	struct tgtcmd *cmd;

	while ((cmd = get_next_cmd(fcq)) != NULL)
	{
		/* process the commands.  */
		process_cmd(fcbridge, cmd);
	}
}

static int
kernel_thread_check(int *flags, int bit)
{
	if (test_bit(bit, flags))
		return 1;
	else
		return 0;
}

static inline int 
kernel_thread_stop(kproc_t *task, int *flags, wait_chan_t *chan, int bit)
{
	mtx_lock(&chan->chan_lock);
	set_bit(bit, flags);
	cv_broadcast(&chan->chan_cond);
	msleep(task, &chan->chan_lock, 0, "texit", 0);
	mtx_unlock(&chan->chan_lock);
	return 0;
}

static void 
fcq_thread(void *data)
{
	struct fcq *fcq = data;
	struct fcbridge *fcbridge = fcq->fcbridge;

	__sched_prio(curthread, PINOD);

	for (;;)
	{
		wait_on_chan_interruptible(fcq->fcq_wait, !STAILQ_EMPTY(&fcq->pending_queue) || kernel_thread_check(&fcq->flags, FCQ_SHUTDOWN));

		process_queue(fcbridge, fcq);
		if (unlikely(kernel_thread_check(&fcq->flags, FCQ_SHUTDOWN)))
		{
			break;
		}

	}
	free(fcq, M_QISP);
	kproc_exit(0);
}


void
fcq_exit(struct fcq *fcq)
{
	int err;

	err = kernel_thread_stop(fcq->task, &fcq->flags, &fcq->fcq_wait, FCQ_SHUTDOWN);

	if (err)
	{
		DEBUG_WARN_NEW("Shutting down fcq thread failed\n");
		return;
	}	
}

struct fcq *
fcq_init(struct fcbridge *fcbridge)
{
	struct fcq *fcq;
	int retval;

	fcq = zalloc(sizeof(struct fcq), M_QISP, M_WAITOK);
	if (unlikely(!fcq))
	{
		return NULL;
	}

	wait_chan_init(&fcq->fcq_wait, "fcq wait");
	STAILQ_INIT(&fcq->pending_queue);
	mtx_lock_initt(&fcq->fcq_lock, "fcq");
	fcbridge->fcq = fcq;
	fcq->fcbridge = fcbridge;
	retval = kernel_thread_create(fcq_thread, fcq, fcq->task, "fcq");

	if (retval != 0)
	{
		fcbridge->fcq = NULL;
		free(fcq, M_QISP);
		return NULL;
	}
	return fcq;
}

#ifndef QUADSTOR_FCQ_H_
#define QUADSTOR_FCQ_H_

#include "qla_def.h"
#include "qla_target.h"

struct fcbridge {
	struct scsi_qla_host *ha;
	struct fcq *fcq;
	TAILQ_ENTRY(fcbridge) b_list;
	__u32 id;
};

struct fcq {
	unsigned long flags;
	spinlock_t fcq_lock;
	wait_queue_head_t fcq_wait;
	STAILQ_HEAD(, qla_cmd_hdr) pending_queue;
	struct task_struct *task;
};

static inline void
fcq_insert_cmd(struct fcbridge *fcbridge, struct qla_cmd_hdr *cmd)
{
	struct fcq *fcq = fcbridge->fcq;
	unsigned long flags = 0;

	spin_lock_irqsave(&fcq->fcq_lock, flags);
	STAILQ_INSERT_TAIL(&fcq->pending_queue, cmd, q_list);
	spin_unlock_irqrestore(&fcq->fcq_lock, flags);
	wake_up(&fcq->fcq_wait);
}

struct fcq * fcq_init(struct fcbridge *fcbridge);
void fcq_exit(struct fcq *fcq);
int fcbridge_proc_cmd(void *bridge, void *iop);
int fcbridge_task_mgmt(struct fcbridge *fcbridge, struct qsio_immed_notify *notify);
void __ctio_free_data(struct qsio_scsiio *ctio);
void fcbridge_route_cmd_post(struct qsio_scsiio *ctio);
int fcbridge_i_prt_valid(struct fcbridge *fcbridge, uint64_t i_prt[]);

#endif

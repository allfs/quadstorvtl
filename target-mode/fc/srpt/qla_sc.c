/*
 * Copyright (C) Shivaram Narasimha Murthy
 * All Rights Reserved
 */

#include <linuxdefs.h>
#include <exportdefs.h>
#include <missingdefs.h>
#include <scsi/scsi_cmnd.h>
#include "ib_srpt.h"
#include "ib_sc.h"
#include "qla_sc.h"
#include "fcq.h"

void
ctio_sglist_map(struct qsio_scsiio *ctio, srpt_cmd_t *cmd)
{
	struct scatterlist *sglist;
	struct pgdata **pglist;
	int i;
	int dxfer_len = ctio->dxfer_len;

	sglist = kmalloc(ctio->pglist_cnt * sizeof(struct scatterlist), GFP_KERNEL|__GFP_NOFAIL);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	sg_init_table(sglist, ctio->pglist_cnt);
#endif

	pglist = (struct pgdata **)ctio->data_ptr;
	for (i = 0; i < ctio->pglist_cnt; i++)
	{
		struct pgdata *pgtmp = pglist[i];
		struct scatterlist *sgtmp = &sglist[i];
		int min_len;

		min_len = min_t(int, dxfer_len, pgtmp->pg_len); 
		dxfer_len -= min_len;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
		sg_set_page(sgtmp, pgtmp->page, min_len, pgtmp->pg_offset);
#else
		sgtmp->page = pgtmp->page;
		sgtmp->offset = pgtmp->pg_offset;
		sgtmp->length = min_len;
#endif
	}

	cmd->cmd.t_data_sg = sglist;
	cmd->cmd.t_data_nents = ctio->pglist_cnt;
}

static void
ctio_buffer_map(struct qsio_scsiio *ctio, srpt_cmd_t *cmd)
{
	struct scatterlist *sgtmp;

	sgtmp = kmalloc(sizeof(struct scatterlist), GFP_KERNEL|__GFP_NOFAIL);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	sg_init_table(sgtmp, 1);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	sg_set_page(sgtmp, virt_to_page(ctio->data_ptr), ctio->dxfer_len, page_offset(ctio->data_ptr));
#else
	sgtmp->page = virt_to_page(ctio->data_ptr);
	sgtmp->offset = page_offset(ctio->data_ptr);
	sgtmp->length = ctio->dxfer_len;
#endif
	cmd->cmd.t_data_sg = sgtmp;
	cmd->cmd.t_data_nents = 1;
}

extern struct qs_interface_cbs icbs;

int tcm_to_srp_tsk_mgmt_status(const int tcm_mgmt_status)
{
	if (tcm_mgmt_status == FC_TM_SUCCESS)
		return SRP_TSK_MGMT_SUCCESS;
	else
		return SRP_TSK_MGMT_FAILED; 
}

static void
cmd_sg_free(srpt_cmd_t *cmd)
{
	if (cmd->cmd.t_data_sg) {
		kfree(cmd->cmd.t_data_sg);
		cmd->cmd.t_data_sg = NULL;
		cmd->cmd.t_data_nents = 0;
	}
}

static void
se_cmd_free_ctio(srpt_cmd_t *cmd)
{
	cmd_sg_free(cmd);
	DEBUG_BUG_ON(!atomic_read(&cmd->cmd.sess->cmds));
	atomic_dec(&cmd->cmd.sess->cmds);
}

static void
se_cmd_free_notify(srpt_cmd_t *cmd)
{
	if (cmd->cmd.ccb) {
		kfree(cmd->cmd.ccb);
		cmd->cmd.ccb = NULL;
	}
	if (cmd->cmd.se_tmr_req) {
		kfree(cmd->cmd.se_tmr_req);
		cmd->cmd.se_tmr_req = NULL;
	}
	atomic_dec(&cmd->cmd.sess->cmds);
}

static int
qla_end_notify(struct qsio_immed_notify *notify)
{
	struct se_cmd *se_cmd = notify_cmd(notify);
	srpt_cmd_t *cmd;
	struct se_tmr_req *se_tmr;
	int retval;

	cmd = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	se_tmr = kzalloc(sizeof(*se_tmr), GFP_KERNEL | __GFP_NOFAIL);
	se_tmr->function = notify->fn;
	se_cmd->se_tmr_req = se_tmr;
	if (notify->notify_status == FC_TM_SUCCESS)
		se_tmr->response = SRP_TSK_MGMT_SUCCESS;
	else
		se_tmr->response = SRP_TSK_MGMT_FAILED;

	retval = srpt_queue_response(se_cmd);
again:
	if (unlikely(retval != 0)) {
		if (retval == -EAGAIN) {
			msleep(1);
			goto again;
		}
		se_cmd_free_notify(cmd);
		srpt_release_cmd(se_cmd);
	}
	return 0;
}

static int
qla_end_ctio(struct qsio_scsiio *ctio)
{
	int retval;
	struct ccb_list ctio_list;
	struct se_cmd *se_cmd = ctio_cmd(ctio);
	srpt_cmd_t *cmd;

	cmd = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	STAILQ_INIT(&ctio_list);
	if ((ctio->ccb_h.flags & QSIO_SEND_STATUS) && ctio->ccb_h.target_lun && ctio->ccb_h.tdevice) {
		(*icbs.device_remove_ctio)(ctio, &ctio_list);
	}

	if ((ctio->ccb_h.flags & QSIO_CTIO_ABORTED)) {
		if ((ctio->ccb_h.flags & QSIO_SEND_ABORT_STATUS)) {
			__ctio_free_data(ctio);
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASC, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASCQ);
		}
		else {
			__ctio_free_all(ctio, se_cmd->local_pool);
			se_cmd_free_ctio(cmd);
			srpt_release_cmd(se_cmd);
			goto send;
		}
	}

	DEBUG_INFO("cmd %x flags %x\n", ctio->cdb[0], ctio->ccb_h.flags);
	if (ctio->pglist_cnt)
		ctio_sglist_map(ctio, cmd);
	else if (ctio->dxfer_len)
		ctio_buffer_map(ctio, cmd);
	else {
		cmd_sg_free(cmd);
	}

	if (se_cmd->data_dir == DMA_FROM_DEVICE) {
		if (se_cmd->orig_length > ctio->dxfer_len) {
			se_cmd->residual_count = se_cmd->orig_length - ctio->dxfer_len;
			se_cmd->se_cmd_flags |= SCF_UNDERFLOW_BIT;
		}
		else if (se_cmd->orig_length < ctio->dxfer_len) {
			se_cmd->residual_count = ctio->dxfer_len - se_cmd->orig_length;
			se_cmd->se_cmd_flags |= SCF_OVERFLOW_BIT;
		}
	}

	if (ctio->sense_len) {
		memcpy(cmd->sense_data, ctio->sense_data, ctio->sense_len);
		cmd->cmd.scsi_sense_length = ctio->sense_len;
	}


	se_cmd->data_length = ctio->dxfer_len;
	se_cmd->data_direction = ctio_direction(ctio);
	se_cmd->scsi_status = ctio->scsi_status;
	DEBUG_INFO("ctio %p cmd %x target_lun %llu data_dir %s orig_length %u dxfer_len %d pglist_cnt %d sense_len %d residual count %u scsi_status %d local pool %d\n", se_cmd, ctio->cdb[0], (unsigned long long)ctio->ccb_h.target_lun, se_data_dir(se_cmd->data_direction), se_cmd->orig_length, ctio->dxfer_len, ctio->pglist_cnt, ctio->sense_len, se_cmd->residual_count, ctio->scsi_status, se_cmd->local_pool);

again:
	if (ctio->dxfer_len && se_cmd->data_direction == DMA_TO_DEVICE &&
	    cmd->state != SRPT_STATE_DATA_IN) {
		retval = srpt_write_pending(se_cmd);
	}
	else if (ctio->dxfer_len)
		retval = srpt_queue_response(se_cmd);
	else
		retval = srpt_queue_status(se_cmd);

	if (unlikely(retval != 0)) {
		if (retval == -EAGAIN) {
			msleep(1);
			goto again;
		}
		__ctio_free_all(ctio, se_cmd->local_pool);
		se_cmd_free_ctio(cmd);
		srpt_release_cmd(se_cmd);
	}
send:
	if (!STAILQ_EMPTY(&ctio_list))
		(*icbs.device_queue_ctio_list)(&ctio_list);
	return 0;
}

void
qla_end_ccb(void *ccb_void)
{
	struct qsio_hdr *ccb_h = ccb_void;

	if (ccb_h->flags & QSIO_TYPE_CTIO)
	{
		qla_end_ctio((struct qsio_scsiio *)(ccb_h));
	}
	else if (ccb_h->flags & QSIO_TYPE_NOTIFY)
	{
		qla_end_notify((struct qsio_immed_notify *)(ccb_h));
	}
	else
	{
		DEBUG_BUG_ON(1);
	}
}

void 
target_execute_cmd(struct se_cmd *se_cmd)
{
	srpt_cmd_t *cmd;
	struct qsio_scsiio *ctio;

	cmd = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	ctio = (struct qsio_scsiio *)cmd->cmd.ccb;
	cmd_sg_free(cmd);
	if (ctio->ccb_h.flags & QSIO_DIR_OUT && cmd->rdma_aborted != true) {
		ctio->ccb_h.flags &= ~QSIO_DIR_OUT;
		__ctio_queue_cmd(ctio);
	}
}

static void 
ib_sc_free_ctio(srpt_cmd_t *cmd)
{
	struct se_cmd *se_cmd = &cmd->cmd;
	struct qsio_scsiio *ctio = (struct qsio_scsiio *)cmd->cmd.ccb;

	if (!ctio)
		goto free_cmd;

	if (ctio->ccb_h.flags & QSIO_DIR_OUT && !se_cmd->local_pool) {
		struct ccb_list ctio_list;

		STAILQ_INIT(&ctio_list);
		(*icbs.device_remove_ctio)(ctio, &ctio_list);
		(*icbs.device_queue_ctio_list)(&ctio_list);
	}

free_cmd:
	se_cmd_free_ctio(cmd);
	__ctio_free_all(ctio, se_cmd->local_pool);
}

static void
ib_sc_free_notify(srpt_cmd_t *cmd)
{
	se_cmd_free_notify(cmd);
}

struct se_session *
transport_init_session(struct srpt_rdma_ch *ch)
{
	struct se_session *sess;

	sess = kzalloc(sizeof(*sess), GFP_KERNEL);
	if (unlikely(!sess)) {
		return NULL;
	}
	sess->ch = ch;
	sess->vha = ch->sport->sdev;
	return sess;
}

void
transport_deregister_session(struct se_session *sess)
{
	uint64_t i_prt[2], t_prt[2];

	i_prt[0] = wwn_to_u64(sess->ch->i_port_id);
	i_prt[1] = wwn_to_u64(&sess->ch->i_port_id[8]);
	t_prt[0] = wwn_to_u64(sess->ch->t_port_id);
	t_prt[1] = wwn_to_u64(&sess->ch->t_port_id[8]);
	fcbridge_free_initiator(i_prt, t_prt);
	kfree(sess);
}

int
target_submit_tmr(struct se_cmd *se_cmd, struct se_session *sess, uint32_t unpacked_lun, unsigned char tmr_func, unsigned int tag)
{
	srpt_cmd_t *cmd;

	cmd = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	se_cmd->unpacked_lun = unpacked_lun;
	se_cmd->tag = tag;
	se_cmd->type = SRPT_CMD_TYPE_NOTIFY;
	se_cmd->notify_fn = tmr_func;
	se_cmd->sess = sess;
	fcq_insert_cmd(sess->vha->fcbridge, se_cmd);
	return 0;
}

void transport_generic_free_cmd(struct se_cmd *se_cmd)
{
	srpt_cmd_t *cmd;

	cmd = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	if (se_cmd->type == SRPT_CMD_TYPE_CTIO)
		ib_sc_free_ctio(cmd);
	else
		ib_sc_free_notify(cmd);
	srpt_release_cmd(se_cmd);
}

void
target_cmd_send_failed(struct se_cmd *se_cmd)
{
	transport_generic_free_cmd(se_cmd);
}

void
target_cmd_recv_ctio_failed(struct se_cmd *se_cmd)
{
	srpt_cmd_t *cmd;
	struct qsio_scsiio *ctio = (struct qsio_scsiio *)se_cmd->ccb;
	struct ccb_list ctio_list;

	cmd = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	STAILQ_INIT(&ctio_list);
	(*icbs.device_remove_ctio)(ctio, &ctio_list);

	__ctio_free_all(ctio, se_cmd->local_pool);
	se_cmd_free_ctio(cmd);
	srpt_release_cmd(se_cmd);

	if (!STAILQ_EMPTY(&ctio_list))
		(*icbs.device_queue_ctio_list)(&ctio_list);
}

void
target_cmd_recv_failed(struct se_cmd *se_cmd)
{
	if (se_cmd->type == SRPT_CMD_TYPE_CTIO)
		target_cmd_recv_ctio_failed(se_cmd);
	else
		transport_generic_free_cmd(se_cmd);
}

int
target_put_sess_cmd(struct se_session *se_sess, struct se_cmd *se_cmd)
{
	return 0;
}

int
target_submit_cmd(struct se_cmd *se_cmd, struct se_session *sess, unsigned char *cdb, unsigned char *sense_data, uint32_t unpacked_lun, uint32_t data_length, int fcp_task_attr, int data_dir, unsigned int tag)
{
	srpt_cmd_t *cmd;

	cmd = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	se_cmd->unpacked_lun = unpacked_lun;
	se_cmd->type = SRPT_CMD_TYPE_CTIO;
	se_cmd->orig_length = data_length;
	se_cmd->t_task_cdb = se_cmd->__t_task_cdb;
	se_cmd->task_attr = fcp_task_attr;
	se_cmd->data_dir = data_dir;
	se_cmd->sess = sess;
	se_cmd->tag = tag;
	atomic_inc(&sess->cmds);
	memcpy(se_cmd->t_task_cdb, cdb, scsi_command_size(cdb));
	fcq_insert_cmd(sess->vha->fcbridge, se_cmd);
	return 0;
}

void target_wait_for_sess_cmds(struct se_session *se_sess, int wait_for_tasks)
{
	while (atomic_read(&se_sess->cmds))
		msleep(10);
}

int
ib_sc_fail_ctio(struct se_cmd *se_cmd, uint8_t asc)
{
	srpt_cmd_t *cmd;
	struct qsio_scsiio *ctio;
	int retval;

	cmd = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	ctio = __local_ctio_new(M_WAITOK);
	se_cmd->local_pool = 1;

	switch (asc) {
	case INVALID_FIELD_IN_CDB_ASC:
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);
		break;
	}
	memcpy(cmd->sense_data, ctio->sense_data, ctio->sense_len);
	cmd->cmd.scsi_sense_length = ctio->sense_len;
	se_cmd->scsi_status = ctio->scsi_status;
	retval = srpt_queue_status(se_cmd);
again:
	if (unlikely(retval != 0)) {
		if (retval == -EAGAIN) {
			msleep(1);
			goto again;
		}
		__ctio_free_all(ctio, se_cmd->local_pool);
		se_cmd_free_ctio(cmd);
		srpt_release_cmd(se_cmd);
	}
	return 0;
}

int
ib_sc_fail_notify(struct se_cmd *se_cmd, int function)
{
	srpt_cmd_t *cmd;
	struct se_tmr_req *se_tmr;
	int retval;

	cmd = container_of(se_cmd, struct srpt_send_ioctx, cmd);
	se_tmr = kzalloc(sizeof(*se_tmr), GFP_KERNEL | __GFP_NOFAIL);
	se_tmr->function = function;
	se_cmd->se_tmr_req = se_tmr;
	se_tmr->response = SRP_TSK_MGMT_FAILED;
	retval = srpt_queue_response(se_cmd);
again:
	if (unlikely(retval != 0)) {
		if (retval == -EAGAIN) {
			msleep(1);
			goto again;
		}
		se_cmd_free_notify(cmd);
		srpt_release_cmd(se_cmd);
	}
	return retval;
}

int
fcbridge_i_prt_valid(struct fcbridge *fcbridge, uint64_t i_prt[])
{
	return 1;
}

void
fcbridge_get_tport(struct fcbridge *fcbridge, uint64_t wwpn[])
{
	struct srpt_device *sdev = fcbridge->ha;
	struct srpt_port *sport;

	sport = &sdev->port[0];
	wwpn[0] = wwn_to_u64(sport->gid.raw);
	wwpn[1] = wwn_to_u64(&sport->gid.raw[8]);
}

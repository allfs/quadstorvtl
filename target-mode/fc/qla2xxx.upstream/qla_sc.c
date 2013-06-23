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
#include "qla_target.h"
#include "qla_sc.h"
#include "fcq.h"

void
ctio_sglist_map(struct qsio_scsiio *ctio, struct qla_tgt_cmd *cmd)
{
	struct scatterlist *sglist;
	struct pgdata **pglist;
	int i;
	int dxfer_len = ctio->dxfer_len;

	DEBUG_BUG_ON(cmd->sg);
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

	cmd->sg = cmd->se_cmd.t_data_sg = sglist;
	cmd->sg_cnt = cmd->se_cmd.t_data_nents = ctio->pglist_cnt;
}

static void
ctio_buffer_map(struct qsio_scsiio *ctio, struct qla_tgt_cmd *cmd)
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
	cmd->sg = cmd->se_cmd.t_data_sg = sgtmp;
	cmd->sg_cnt = cmd->se_cmd.t_data_nents = 1;
}

extern struct qs_interface_cbs icbs;
static int
qla_end_notify(struct qsio_immed_notify *notify)
{
	struct qla_tgt_cmd *cmd = notify_cmd(notify);
	struct se_cmd *se_cmd = &cmd->se_cmd;
	struct qla_tgt_mgmt_cmd *mcmd = container_of(se_cmd,
				struct qla_tgt_mgmt_cmd, se_cmd);

	se_cmd->tmr_function = notify->fn;
	mcmd->fc_tm_rsp = notify->notify_status;
	mcmd->flags = QLA24XX_MGMT_SEND_NACK;
	qlt_xmit_tm_rsp(mcmd);

	return 0;
}

static void
cmd_sg_free(struct qla_tgt_cmd *cmd)
{
	if (cmd->sg) {
		kfree(cmd->sg);
		cmd->sg = cmd->se_cmd.t_data_sg = NULL;
		cmd->sg_cnt = cmd->se_cmd.t_data_nents = 0;
	}
}

static inline char *
se_data_dir(int dir)
{
	switch(dir) {
	case DMA_NONE:
		return "dma_none";
	case DMA_FROM_DEVICE:
		return "dma_from_device";
	case DMA_TO_DEVICE:
		return "dma_to_device";
	default:
		break;
	}
	return "unknown";
}

static int
qla_end_ctio(struct qsio_scsiio *ctio)
{
	int retval;
	struct ccb_list ctio_list;
	struct qla_tgt_cmd *cmd = ctio_cmd(ctio);
	struct se_cmd *se_cmd = &cmd->se_cmd;

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
			__ctio_free_all(ctio, cmd->local_pool);
			qlt_free_cmd(cmd);
			goto send;
		}
	}

	DEBUG_INFO("cmd %x flags %x\n", ctio->cdb[0], ctio->ccb_h.flags);
	if (ctio->pglist_cnt)
		ctio_sglist_map(ctio, cmd);
	else if (ctio->dxfer_len)
		ctio_buffer_map(ctio, cmd);
	else {
		DEBUG_BUG_ON(cmd->sg);
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

	if (ctio->sense_len)
		memcpy(cmd->sense_buffer, ctio->sense_data, ctio->sense_len);

	DEBUG_INFO("cmd %x target_lun %llu data_dir %s orig_length %u dxfer_len %d pglist_cnt %d sense_len %d residual count %u scsi_status %d\n", ctio->cdb[0], (unsigned long long)ctio->ccb_h.target_lun, se_data_dir(se_cmd->data_dir), se_cmd->orig_length, ctio->dxfer_len, ctio->pglist_cnt, ctio->sense_len, se_cmd->residual_count, ctio->scsi_status);

	cmd->bufflen = se_cmd->data_length = ctio->dxfer_len;
	cmd->dma_data_direction = ctio_direction(ctio);
	cmd->se_cmd.scsi_status = ctio->scsi_status;

again:
	if (ctio->dxfer_len && cmd->dma_data_direction == DMA_TO_DEVICE &&
	    cmd->state != QLA_TGT_STATE_DATA_IN) {
		retval = qlt_rdy_to_xfer(cmd);
	}
	else {
		retval = qlt_xmit_response(cmd, QLA_TGT_XMIT_DATA|QLA_TGT_XMIT_STATUS,
					cmd->se_cmd.scsi_status);
	}

	if (unlikely(retval != 0)) {
		if (retval == -EAGAIN) {
			msleep(1);
			goto again;
		}
		__ctio_free_all(ctio, cmd->local_pool);
		qlt_free_cmd(cmd);;
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

static void 
qla_sc_handle_data(struct qla_tgt_cmd *cmd)
{
	struct qsio_scsiio *ctio = (struct qsio_scsiio *)cmd->se_cmd.ccb;

	cmd_sg_free(cmd);
	if (ctio->ccb_h.flags & QSIO_DIR_OUT && cmd->state != QLA_TGT_STATE_ABORTED) {
		ctio->ccb_h.flags &= ~QSIO_DIR_OUT;
		__ctio_queue_cmd(ctio);
	}
}

static void 
qla_sc_free_cmd(struct qla_tgt_cmd *cmd)
{
	struct qsio_scsiio *ctio = (struct qsio_scsiio *)cmd->se_cmd.ccb;

	if (!ctio)
		goto free_cmd;

	if (ctio->ccb_h.flags & QSIO_DIR_OUT && !cmd->local_pool) {
		struct ccb_list ctio_list;

		STAILQ_INIT(&ctio_list);
		(*icbs.device_remove_ctio)(ctio, &ctio_list);
		(*icbs.device_queue_ctio_list)(&ctio_list);
	}

	__ctio_free_all(ctio, cmd->local_pool);
free_cmd:
	cmd_sg_free(cmd);
	qlt_free_cmd(cmd);
}

static void
qla_sc_free_mcmd(struct qla_tgt_mgmt_cmd *mcmd)
{
	if (mcmd->se_cmd.ccb)
		kfree(mcmd->se_cmd.ccb);
	qlt_free_mcmd(mcmd);
}

static int
qla_sc_handle_tmr(struct qla_tgt_mgmt_cmd *mcmd, uint32_t lun,
		uint8_t tmr_func, uint32_t tag)
{
	mcmd->unpacked_lun = lun;
	mcmd->tag = tag;
	mcmd->cmd_h.type = QLA_HDR_TYPE_NOTIFY;
	mcmd->se_cmd.notify_fn = tmr_func;
	fcq_insert_cmd(mcmd->sess->tgt->fcbridge, (struct qla_cmd_hdr *)mcmd);
	return 0;
}

static int
qla_sc_handle_cmd(scsi_qla_host_t *vha, struct qla_tgt_cmd *cmd,
		unsigned char *cdb, uint32_t data_length, int fcp_task_attr,
		int data_dir, int bidi)
{
	struct se_cmd *se_cmd = &cmd->se_cmd;

	se_cmd->orig_length = data_length;
	se_cmd->t_task_cdb = se_cmd->__t_task_cdb;
	se_cmd->task_attr = fcp_task_attr;
	se_cmd->data_dir = data_dir;
	memcpy(se_cmd->t_task_cdb, cdb, scsi_command_size(cdb));
	cmd->cmd_h.type = QLA_HDR_TYPE_CTIO;
	fcq_insert_cmd(cmd->sess->tgt->fcbridge, (struct qla_cmd_hdr *)cmd);
	return 0;
}

static void
qla_sc_free_session(struct qla_tgt_sess *sess)
{
	uint64_t i_prt[2], t_prt[2];

	i_prt[0] = wwn_to_u64(sess->port_name);
	i_prt[1] = 0;
	t_prt[0] = wwn_to_u64(sess->vha->port_name);
	t_prt[1] = 0;
	fcbridge_free_initiator(i_prt, t_prt);
	kfree(sess->se_sess);
}

static int
qla_sc_check_initiator_node_acl( scsi_qla_host_t *vha, unsigned char *fc_wwpn,
	void *qla_tgt_sess, uint8_t *s_id, uint16_t loop_id)
{
	struct se_session *se_sess;

        se_sess = kzalloc(sizeof(*se_sess), GFP_KERNEL);
        if (!se_sess)
                return -ENOMEM;

	kref_init(&se_sess->sess_kref);
	se_sess->fabric_sess_ptr = qla_tgt_sess;
	((struct qla_tgt_sess *)qla_tgt_sess)->se_sess = se_sess;

	return 0;
}

static struct qla_tgt_sess *
qla_sc_find_sess_by_s_id(scsi_qla_host_t *vha, const uint8_t *s_id)
{
	struct qla_tgt_sess *sess;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;
	u32 key;

	key = (((unsigned long)s_id[0] << 16) | ((unsigned long)s_id[1] << 8) | (unsigned long)s_id[2]);
	list_for_each_entry(sess, &tgt->sess_list, sess_list_entry) {
		if (sess->s_id.b24 == key)
			return sess;
	}

	return NULL;
}

static struct qla_tgt_sess *
qla_sc_find_sess_by_loop_id(scsi_qla_host_t *vha, const uint16_t loop_id)
{
	struct qla_tgt_sess *sess;
	struct qla_hw_data *ha = vha->hw;
	struct qla_tgt *tgt = ha->tgt.qla_tgt;

	list_for_each_entry(sess, &tgt->sess_list, sess_list_entry) {
		if (sess->loop_id == loop_id)
			return sess;
	}

	return NULL;
}

static void
qla_sc_clear_nacl_from_fcport_map(struct qla_tgt_sess *sess)
{
	return;
}
 
static void qla_sc_release_session(struct kref *kref)
{
	struct se_session *se_sess = container_of(kref,
			struct se_session, sess_kref);

	qlt_unreg_sess(se_sess->fabric_sess_ptr);
}

static void
qla_sc_put_sess(struct qla_tgt_sess *sess)
{
	struct qla_hw_data *ha = sess->vha->hw;
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	kref_put(&sess->se_sess->sess_kref, qla_sc_release_session);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static void
qla_sc_shutdown_sess(struct qla_tgt_sess *sess)
{
	return;
}

static void qla_sc_update_sess(struct qla_tgt_sess *sess, port_id_t s_id,
				    uint16_t loop_id, bool conf_compl_supported)
{
	sess->s_id = s_id;
	sess->loop_id = loop_id;
	sess->conf_compl_supported = conf_compl_supported;
}

int
fcbridge_i_prt_valid(struct fcbridge *fcbridge, uint64_t i_prt[])
{
	scsi_qla_host_t *ha = fcbridge->ha;

	if (i_prt[1] || i_prt[0] != wwn_to_u64(ha->port_name))
		return 1;
	else
		return 0;
}

void
fcbridge_get_tport(struct fcbridge *fcbridge, uint64_t wwpn[])
{
	wwpn[0] = wwn_to_u64(fcbridge->ha->port_name);
	wwpn[1] = 0;
}

struct qla_tgt_func_tmpl qla_sc_template = {
	.handle_cmd		= qla_sc_handle_cmd,
	.handle_data		= qla_sc_handle_data,
	.handle_tmr		= qla_sc_handle_tmr,
	.free_cmd		= qla_sc_free_cmd,
	.free_mcmd		= qla_sc_free_mcmd,
	.free_session		= qla_sc_free_session,
	.check_initiator_node_acl	= qla_sc_check_initiator_node_acl,
	.find_sess_by_s_id	= qla_sc_find_sess_by_s_id,
	.find_sess_by_loop_id	= qla_sc_find_sess_by_loop_id,
	.clear_nacl_from_fcport_map	= qla_sc_clear_nacl_from_fcport_map,
	.update_sess		= qla_sc_update_sess,
	.put_sess		= qla_sc_put_sess,
	.shutdown_sess		= qla_sc_shutdown_sess,
};

/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2011 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

#include "qla_def.h"

#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <net/sock.h>
#include <net/netlink.h>

static struct sock *ql_fc_nl_sock = NULL;
static int ql_fc_nl_event(struct notifier_block *this,
			unsigned long event, void *ptr);

static struct notifier_block ql_fc_nl_notifier = {
	.notifier_call = ql_fc_nl_event,
};

static struct qlfc_aen_log aen_log;

extern int qla2x00_echo_test(scsi_qla_host_t *, struct msg_loopback *,
    uint16_t *);
extern int qla2x00_loopback_test(scsi_qla_host_t *, struct msg_loopback *,
    uint16_t *);

/*
 * local functions
 */
static void ql_fc_nl_rcv_msg(struct sk_buff *skb);
static int ql_fc_proc_nl_rcv_msg(struct sk_buff *skb,
		struct nlmsghdr *nlh, int rcvlen);
static int ql_fc_nl_rsp(uint32_t pid, uint32_t seq, uint32_t type,
		void *hdr, int hdr_len, void *payload, int size);

static int qla84xx_update_fw(struct scsi_qla_host *ha, int rlen,
		struct msg_update_fw *upd_fw)
{
	struct qlfc_fw *qlfw;
	struct verify_chip_entry_84xx *mn;
	dma_addr_t mn_dma;
	int ret = 0;
	uint32_t fw_ver;
	uint16_t options;

	if (rlen < (sizeof(struct msg_update_fw) + upd_fw->len +
		offsetof(struct qla_fc_msg, u))){
		DEBUG16(printk(KERN_ERR "%s(%lu): invalid len\n",
			__func__, ha->host_no));
		return -EINVAL;
	}

	qlfw = &ha->fw_buf;
	if (!upd_fw->offset) {
		if (qlfw->fw_buf || !upd_fw->fw_len ||
			upd_fw->len > upd_fw->fw_len) {
			DEBUG16(printk(KERN_ERR "%s(%lu): invalid offset"
			    " or fw_len\n", __func__, ha->host_no));
			return -EINVAL;
		} else {
			qlfw->fw_buf = dma_alloc_coherent(&ha->pdev->dev,
						upd_fw->fw_len, &qlfw->fw_dma,
						GFP_KERNEL);
			if (qlfw->fw_buf == NULL) {
				DEBUG2(printk(KERN_ERR "%s(%lu): dma alloc "
				    "failed\n", __func__, ha->host_no));
				return (-ENOMEM);
			}
			qlfw->len = upd_fw->fw_len;
		}
		fw_ver = le32_to_cpu(*((uint32_t *)
				((uint32_t *)upd_fw->fw_bytes + 2)));
		if (!fw_ver) {
			DEBUG16(printk(KERN_ERR "%s(%lu): invalid fw revision"
			    " 0x%x\n", __func__, ha->host_no, fw_ver));
			return -EINVAL;
		}
	} else {
		/* make sure we have a buffer allocated */
		if (!qlfw->fw_buf || upd_fw->fw_len != qlfw->len ||
			((upd_fw->offset + upd_fw->len) > upd_fw->fw_len)){
			DEBUG16(printk(KERN_ERR "%s(%lu): invalid size of "
			    "offset=0 expected\n", __func__, ha->host_no));
			return -EINVAL;
		}
	}
	/* Copy the firmware into DMA Buffer */
	memcpy(((uint8_t *)qlfw->fw_buf + upd_fw->offset),
		upd_fw->fw_bytes, upd_fw->len);
	
	if ((upd_fw->offset+upd_fw->len) != qlfw->len)
		return 0;
	
	mn = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (mn == NULL) {
		DEBUG2(printk(KERN_ERR "%s: dma alloc for fw buffer "
		    "failed%lu\n", __func__, ha->host_no));
		return -ENOMEM;
	}

	fw_ver = le32_to_cpu(*((uint32_t *)((uint32_t *)qlfw->fw_buf + 2)));

	/* Create iocb and issue it */
	memset(mn, 0, sizeof(*mn));
	
	mn->entry_type = VERIFY_CHIP_IOCB_TYPE;
	mn->entry_count = 1;

	options = VCO_FORCE_UPDATE | VCO_END_OF_DATA;
	if (upd_fw->diag_fw)
		options |= VCO_DIAG_FW;
	mn->options = cpu_to_le16(options);
 
	mn->fw_ver = cpu_to_le32(fw_ver);
	mn->fw_size = cpu_to_le32(qlfw->len);
	mn->fw_seq_size = cpu_to_le32(qlfw->len);

	mn->dseg_address[0] = cpu_to_le32(LSD(qlfw->fw_dma));
	mn->dseg_address[1] = cpu_to_le32(MSD(qlfw->fw_dma));
	mn->dseg_length = cpu_to_le32(qlfw->len);
	mn->data_seg_cnt = cpu_to_le16(1);
	
	ret = qla2x00_issue_iocb_timeout(ha, mn, mn_dma, 0, 120);

	if (ret != QLA_SUCCESS) {
		DEBUG2(printk(KERN_ERR "%s(%lu): failed\n", __func__,
		    ha->host_no));
	}

	qla_free_nlnk_dmabuf(ha);
	return ret;
}

static int
qla84xx_mgmt_cmd(scsi_qla_host_t *ha, struct qla_fc_msg *cmd, int rlen,
	uint32_t pid, uint32_t seq, uint32_t type)
{
	struct access_chip_84xx *mn;
	dma_addr_t mn_dma, mgmt_dma;
	void *mgmt_b = NULL;
	int ret = 0;
	int rsp_hdr_len, len = 0;
	struct qla84_msg_mgmt *ql84_mgmt;

	ql84_mgmt = &cmd->u.utok.mgmt;
	rsp_hdr_len = offsetof(struct qla_fc_msg, u) + 
			offsetof(struct qla84_msg_mgmt, payload);

	mn = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (mn == NULL) {
		DEBUG2(printk(KERN_ERR "%s: dma alloc for fw buffer "
		    "failed%lu\n", __func__, ha->host_no));
		return (-ENOMEM);
	}

	memset(mn, 0, sizeof (struct access_chip_84xx));

	mn->entry_type = ACCESS_CHIP_IOCB_TYPE;
	mn->entry_count = 1;

	switch (ql84_mgmt->cmd) {
	case QLA84_MGMT_READ_MEM:
		mn->options = cpu_to_le16(ACO_DUMP_MEMORY);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.mem.start_addr);
		break;
	case QLA84_MGMT_WRITE_MEM:
		if (rlen < (sizeof(struct qla84_msg_mgmt) + ql84_mgmt->len +
			offsetof(struct qla_fc_msg, u))){
			ret = -EINVAL;
			goto exit_mgmt0;
		}
		mn->options = cpu_to_le16(ACO_LOAD_MEMORY);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.mem.start_addr);
		break;
	case QLA84_MGMT_CHNG_CONFIG:
		mn->options = cpu_to_le16(ACO_CHANGE_CONFIG_PARAM);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.config.id);
		mn->parameter2 = cpu_to_le32(ql84_mgmt->mgmtp.u.config.param0);
		mn->parameter3 = cpu_to_le32(ql84_mgmt->mgmtp.u.config.param1);
		break;
	case QLA84_MGMT_GET_INFO:
		mn->options = cpu_to_le16(ACO_REQUEST_INFO);
		mn->parameter1 = cpu_to_le32(ql84_mgmt->mgmtp.u.info.type);
		mn->parameter2 = cpu_to_le32(ql84_mgmt->mgmtp.u.info.context);
		break;
	default:
		ret = -EIO;
		goto exit_mgmt0;
	}

	if ((len = ql84_mgmt->len) &&
	    ql84_mgmt->cmd != QLA84_MGMT_CHNG_CONFIG) {
		mgmt_b = dma_alloc_coherent(&ha->pdev->dev, len,
				&mgmt_dma, GFP_KERNEL);
		if (mgmt_b == NULL) {
			DEBUG2(printk(KERN_ERR "%s: dma alloc mgmt_b "
			    "failed%lu\n", __func__, ha->host_no));
			ret = -ENOMEM;
			goto exit_mgmt0;
		}
		mn->total_byte_cnt = cpu_to_le32(ql84_mgmt->len);
		mn->dseg_count = cpu_to_le16(1);
		mn->dseg_address[0] = cpu_to_le32(LSD(mgmt_dma));
		mn->dseg_address[1] = cpu_to_le32(MSD(mgmt_dma));
		mn->dseg_length = cpu_to_le32(len);

		if (ql84_mgmt->cmd == QLA84_MGMT_WRITE_MEM) {
			memcpy(mgmt_b, ql84_mgmt->payload, len);
		}
	}

	ret = qla2x00_issue_iocb(ha, mn, mn_dma, 0);
	cmd->error = ret; 
	if ((ret != QLA_SUCCESS) || (ql84_mgmt->cmd == QLA84_MGMT_WRITE_MEM)
	    ||(ql84_mgmt->cmd == QLA84_MGMT_CHNG_CONFIG)) {
		if (ret != QLA_SUCCESS) 
			DEBUG2(printk(KERN_ERR "%s(%lu): failed\n",
			    __func__, ha->host_no));
		ret = ql_fc_nl_rsp(pid, seq, type, cmd, rsp_hdr_len, NULL, 0);
	} else if ((ql84_mgmt->cmd == QLA84_MGMT_READ_MEM)||
			(ql84_mgmt->cmd == QLA84_MGMT_GET_INFO)) {
		ret = ql_fc_nl_rsp(pid, seq, type, cmd, rsp_hdr_len, mgmt_b,
		    len);
	}

	if (mgmt_b)
		dma_free_coherent(&ha->pdev->dev, len, mgmt_b, mgmt_dma);

exit_mgmt0:
	dma_pool_free(ha->s_dma_pool, mn, mn_dma);
	return ret;
}

static int
qla81xx_mgmt_cmd(scsi_qla_host_t *ha, struct qla_fc_msg *cmd, int rlen,
	uint32_t pid, uint32_t seq, uint32_t type)
{
	dma_addr_t mgmt_dma;
	void *mgmt_b = NULL;
	int ret = 0;
	int rsp_hdr_len;
	struct qla84_msg_mgmt *ql84_mgmt;

	ql84_mgmt = &cmd->u.utok.mgmt;
	rsp_hdr_len = offsetof(struct qla_fc_msg, u) + 
			offsetof(struct qla84_msg_mgmt, payload);

	mgmt_b = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &mgmt_dma);
	if (mgmt_b == NULL) {
		DEBUG2(printk(KERN_ERR "%s: dma alloc mgmt_b failed%lu\n",
			__func__, ha->host_no));
		return -ENOMEM;
	}

	ret = -EIO;
	if (ql84_mgmt->cmd == QLA84_MGMT_GET_INFO) {
		ret = qla81xx_get_xgmac_stats(ha, ql84_mgmt->len, mgmt_dma);
		cmd->error = ret;
		if (ret != QLA_SUCCESS) {
			DEBUG2(printk(KERN_ERR "%s(%lu): failed\n", __func__,
			    ha->host_no));
			ret = ql_fc_nl_rsp(pid, seq, type, cmd, rsp_hdr_len,
			    NULL, 0);
		} else
			ret = ql_fc_nl_rsp(pid, seq, type, cmd, rsp_hdr_len,
			    mgmt_b, ql84_mgmt->len);
	}

	dma_pool_free(ha->s_dma_pool, mgmt_b, mgmt_dma);
	return ret;
}

static void
ql_fc_get_aen(scsi_qla_host_t *ha)
{
	unsigned long flags;

	memset(&aen_log, 0, sizeof (struct qlfc_aen_log));

	spin_lock_irqsave(&ha->hardware_lock, flags);

	memcpy(&aen_log, &ha->aen_log, sizeof(struct qlfc_aen_log));
	ha->aen_log.num_events = 0;

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/* Set the port configuration to enable the
 * internal loopback on ISP81XX
 */
static inline int
qla81xx_set_internal_loopback(scsi_qla_host_t *ha, uint16_t *config,
    uint16_t *new_config)
{
	int ret = 0;
	int rval = 0;

	if (!IS_QLA81XX(ha))
		return 0;
	new_config[0] = config[0] | (ENABLE_INTERNAL_LOOPBACK << 1);
	memcpy(&new_config[1], &config[1], sizeof(uint16_t) * 3);

	ha->notify_dcbx_comp = 1;
	ret = qla81xx_set_port_config(ha, new_config);
	if (ret != QLA_SUCCESS) {
		DEBUG2(printk(KERN_ERR
		    "%s(%lu): Set port config failed\n",
		    __func__, ha->host_no));
		ha->notify_dcbx_comp = 0;
		rval = -EINVAL;
		goto done_set_internal;
	}

	/* Wait for DCBX complete event */
	if (!wait_for_completion_timeout(&ha->dcbx_comp,
	    (20 * HZ))) {
		DEBUG2(qla_printk(KERN_WARNING, ha,
		    "State change notificaition not received.\n"));
	} else
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "State change RECEIVED\n"));
	ha->notify_dcbx_comp = 0;

done_set_internal:
	return rval;
}

/* Set the port configuration to disable the
 * internal loopback on ISP81XX
 */
static inline int
qla81xx_reset_internal_loopback(scsi_qla_host_t *ha, uint16_t *config,
    int wait)
{
	int ret = 0;
	int rval = 0;
	uint16_t new_config[4];

	if (!IS_QLA81XX(ha))
		goto done_reset_internal;

	memset(new_config, 0, sizeof(new_config));
	if ((config[0] & INTERNAL_LOOPBACK_MASK) >> 1 ==
	    ENABLE_INTERNAL_LOOPBACK) {
		new_config[0] = config[0] & ~INTERNAL_LOOPBACK_MASK;
		memcpy(&new_config[1], &config[1], sizeof(uint16_t) * 3);

		ha->notify_dcbx_comp = wait;
		ret = qla81xx_set_port_config(ha, new_config);
		if (ret != QLA_SUCCESS) {
			DEBUG2(printk(KERN_ERR
			    "%s(%lu): Set port config failed\n",
			    __func__, ha->host_no));
			ha->notify_dcbx_comp = 0;
			rval =  -EINVAL;
			goto done_reset_internal;
		}

		/* Wait for DCBX complete event */
		if (wait && !wait_for_completion_timeout(&ha->dcbx_comp,
		    (20 * HZ))) {
			DEBUG2(qla_printk(KERN_WARNING, ha,
			    "State change notificaition not received.\n"));
			ha->notify_dcbx_comp = 0;
			rval =  -EINVAL;
			goto done_reset_internal;
		} else {
			DEBUG2(qla_printk(KERN_INFO, ha,
			    "State change RECEIVED\n"));
		}

		ha->notify_dcbx_comp = 0;
	}
done_reset_internal:
	return rval;
}

static int ql_fc_loopback(struct scsi_qla_host *ha, struct sk_buff *skb,
    struct nlmsghdr *nlh, struct qla_fc_msg *ql_cmd, int rcvlen)
{
	struct qla_loopback *qlloopback = NULL;
	struct msg_loopback *loopback = NULL;
	uint16_t ret_mb[MAILBOX_REGISTER_COUNT];
	uint16_t config[4], new_config[4];
	int ret = 0;
	int rsp_hdr_len = offsetof(struct qla_fc_msg, u) +
	    offsetof(struct msg_loopback, bytes);

	memset(config, 0 , sizeof(config));
	memset(new_config, 0 , sizeof(new_config));
	
	if (ql_cmd->cmd != QLFC_LOOPBACK_CMD)
		goto send_data;

	loopback = &ql_cmd->u.utok.qla_loopback;

	if (rcvlen - sizeof(struct scsi_nl_hdr) <
	    (sizeof(struct msg_loopback) + loopback->len + rsp_hdr_len)) {
		DEBUG16(printk(KERN_ERR "%s(%lu): invalid len\n",
		    __func__, ha->host_no));

		ql_cmd->error = -EINVAL;
		goto cleanup;
	}

	qlloopback = &ha->loopback_buf;
	if (!loopback->offset) {
		if (qlloopback->loopback_buf || !loopback->total_len ||
			loopback->len > loopback->total_len) {
			DEBUG16(printk(KERN_ERR "%s(%lu): invalid offset"
			    " or total_len\n", __func__, ha->host_no));

			ql_cmd->error = -EINVAL;
			goto cleanup;
		} else {
			qlloopback->loopback_buf =
			    dma_alloc_coherent(&ha->pdev->dev,
			    loopback->total_len, &qlloopback->loopback_dma,
			    GFP_KERNEL);

			if (qlloopback->loopback_buf == NULL) {
				DEBUG2(printk(KERN_ERR "%s(%lu): dma alloc "
				    "failed\n", __func__, ha->host_no));

				ql_cmd->error = -ENOMEM;
				goto cleanup;
			}
			qlloopback->len = loopback->total_len;
		}
	} else {
		/* make sure we have a buffer allocated */
		if (!qlloopback->loopback_buf ||
		    loopback->total_len != qlloopback->len ||
		    ((loopback->offset + loopback->len) >
		    loopback->total_len)) {
			DEBUG16(printk(KERN_ERR "%s(%lu): invalid size of "
			    "offset=0 expected\n", __func__, ha->host_no));

			ql_cmd->error = -EINVAL;
			goto cleanup;
		}
	}

	/* Copy the data into DMA Buffer */
	memcpy(((uint8_t *)qlloopback->loopback_buf + loopback->offset),
	    loopback->bytes, loopback->len);

	if ((loopback->offset + loopback->len) != qlloopback->len) {
		ql_cmd->error = 0;
		return ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid,
		    nlh->nlmsg_seq, (uint32_t)nlh->nlmsg_type,
		    ql_cmd, rsp_hdr_len, NULL, 0);
	}

	if ((ha->current_topology == ISP_CFG_F ||
	    (IS_QLA81XX(ha) &&
	    le32_to_cpu(*(uint32_t *)qlloopback->loopback_buf) ==
	    ELS_OPCODE_BYTE && qlloopback->len == MAX_ELS_FRAME_PAYLOAD)) &&
	    loopback->options == EXTERNAL_LOOPBACK) {
		if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
			ql_cmd->error = -EINVAL;
			goto cleanup;
		}

		ret = qla2x00_echo_test(ha, loopback, ret_mb);
		loopback->cmd_sent = INT_DEF_LB_ECHO_CMD;
	} else {
		if (IS_QLA81XX(ha)) {
			memset(config, 0 , sizeof(config));
			memset(new_config, 0 , sizeof(new_config));
			ret = qla81xx_get_port_config(ha, config);
			if (ret != QLA_SUCCESS) {
				(printk(KERN_ERR
				    "%s(%lu): Get port config failed\n",
				    __func__, ha->host_no));
				ql_cmd->error = -EINVAL;
				goto cleanup;
			}
		}

		if (loopback->options != EXTERNAL_LOOPBACK && IS_QLA81XX(ha)) {
			DEBUG2(qla_printk(KERN_INFO, ha,
			    "Internal: current port config = %x\n", config[0]));
			ret = qla81xx_set_internal_loopback(ha, config,
			    new_config);
			if (ret) {
				ql_cmd->error = -EINVAL;
				goto cleanup;
			}
		} else {
			/* For external loopback to work
			 * ensure internal loopback is disabled
			 */
			ret = qla81xx_reset_internal_loopback(ha, config, 1);
			if (ret) {
				ql_cmd->error = -EINVAL;
				goto cleanup;
			}
		}

		ret = qla2x00_loopback_test(ha, loopback, ret_mb);
		loopback->cmd_sent = INT_DEF_LB_LOOPBACK_CMD;

		if (IS_QLA81XX(ha)) {
			if (new_config[0] != 0) {
				/* Revert back to original port config
				 * Also clear internal loopback
				 */
				qla81xx_reset_internal_loopback(ha,
				    new_config, 0);
			}
			if (ret_mb[0] == MBS_COMMAND_ERROR &&
			    ret_mb[1] == QLA_RESET_FC_LB_FAILED) {
				DEBUG2(printk(KERN_ERR "%s(%ld): ABORTing "
				    "ISP\n", __func__, ha->host_no));

				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
				qla2xxx_wake_dpc(ha);
				qla2x00_wait_for_chip_reset(ha);
				/* Also reset the MPI */
				if (qla81xx_restart_mpi_firmware(ha) !=
				    QLA_SUCCESS) {
					qla_printk(KERN_INFO, ha,
					    "MPI reset failed for host%ld.\n",
					    ha->host_no);
				}

				ql_cmd->error = -EINVAL;
				goto cleanup;
			}
		}
	}

	ql_cmd->error = 0;
	if (ret != QLA_SUCCESS) {
		DEBUG2(printk(KERN_ERR "%s(%lu): failed\n", __func__,
		    ha->host_no));
	}

	loopback->comp_stat = ret_mb[0];
	loopback->crc_err_cnt = ret_mb[1];
	loopback->disparity_err_cnt = ret_mb[2];
	loopback->frame_len_err_cnt = ret_mb[3];
	loopback->iter_cnt_last_err = (ret_mb[19] << 16) | ret_mb[18];

	return ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
	    (uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);

send_data:
	if (ql_cmd->cmd != QLFC_LOOPBACK_DATA) {
		DEBUG16(printk(KERN_ERR "%s(%lu): invalid command\n",
		__func__, ha->host_no));

		ql_cmd->error = -EINVAL;
		goto cleanup;
	}

	loopback = &ql_cmd->u.ktou.qla_loopback;
	qlloopback = &ha->loopback_buf;

	if ((loopback->offset + loopback->len) > qlloopback->len) {
		ql_cmd->error = -EINVAL;
		goto cleanup;
	}

	ql_cmd->error = 0;
	ret = ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
	    (uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len,
	    ((uint8_t *)qlloopback->loopback_buf + loopback->offset),
	    loopback->len);

	if ((loopback->offset + loopback->len) == qlloopback->len) {
cleanup:
		if (qlloopback && qlloopback->loopback_buf) {
			dma_free_coherent(&ha->pdev->dev, qlloopback->len,
			    qlloopback->loopback_buf,
			    qlloopback->loopback_dma);
			qlloopback->loopback_buf = NULL;
		}
	}

	if (ql_cmd->error)
		ret = ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
		    (uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
	return ret;
}

static int ql_fc_iidma(struct scsi_qla_host *ha, struct sk_buff *skb,
    struct nlmsghdr *nlh, struct qla_fc_msg *ql_cmd, int rcvlen)
{
	struct qla_port_param *port_param = NULL;
	fc_port_t *fcport = NULL;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int ret = 0;
	int rsp_hdr_len = offsetof(struct qla_fc_msg, u) +
	    sizeof(struct qla_port_param);

	if ((rcvlen - sizeof(struct scsi_nl_hdr)) <
	    (sizeof(struct qla_port_param))) {
		DEBUG16(printk(KERN_ERR "%s(%lu): invalid len\n",
		    __func__, ha->host_no));
		ql_cmd->error = -EINVAL;
		goto cleanup;
	}

	if (!IS_IIDMA_CAPABLE(ha)) {
		DEBUG16(printk(KERN_ERR "%s(%lu): iiDMA not supported\n",
		    __func__, ha->host_no));
		ql_cmd->error = -EINVAL;
		goto cleanup;
	}

	port_param = &ql_cmd->u.port_param;

	if (port_param->fc_scsi_addr.dest_type != EXT_DEF_TYPE_WWPN) {
		DEBUG16(printk(KERN_ERR "%s(%ld): Invalid destination type\n",
		    __func__, ha->host_no));
		ql_cmd->error = -EINVAL;
		goto cleanup;
	}

	fcport = qla2x00_find_port(ha, port_param->fc_scsi_addr.dest_addr.wwpn);
	if (!fcport) {
		DEBUG16(printk(KERN_ERR "%s(%ld): Failed to find port\n",
		    __func__, ha->host_no));
		ql_cmd->error = -EINVAL;
		goto cleanup;
	}

	if (fcport->loop_id == FC_NO_LOOP_ID) {
		DEBUG16(printk(KERN_ERR "%s(%ld): Invalid port loop id, "
		    "loop_id = 0x%x\n",
		    __func__, ha->host_no, fcport->loop_id));
		ql_cmd->error = -EINVAL;
		goto cleanup;
	}

	if (port_param->mode)
		ret = qla2x00_set_idma_speed(ha, fcport->loop_id,
		    port_param->speed, mb);
	else
		ret = qla2x00_get_idma_speed(ha, fcport->loop_id,
		    &port_param->speed, mb);

	if (ret != QLA_SUCCESS) {
		DEBUG16(printk(KERN_ERR "scsi(%ld): iIDMA cmd failed for "
		    "%02x%02x%02x%02x%02x%02x%02x%02x -- %04x %x %04x %04x.\n",
		    ha->host_no, fcport->port_name[0], fcport->port_name[1],
		    fcport->port_name[2], fcport->port_name[3],
		    fcport->port_name[4], fcport->port_name[5],
		    fcport->port_name[6], fcport->port_name[7], ret,
		    fcport->fp_speed, mb[0], mb[1]));
		ql_cmd->error = -EINVAL;
		goto cleanup;
	}

	ql_cmd->error = 0;
cleanup:
	return ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
	    (uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
}

int
qla24xx_fcp_prio_cfg_valid(struct qla_fcp_prio_cfg *pri_cfg, uint8_t flag)
{
	int i, ret, num_valid;
	uint8_t *bcode;
	struct qla_fcp_prio_entry *pri_entry;
	uint32_t *bcode_val_ptr, bcode_val;

	ret = 1;
	num_valid = 0;
	bcode = (uint8_t *)pri_cfg;
	bcode_val_ptr = (uint32_t *)pri_cfg;
	bcode_val = (uint32_t)(*bcode_val_ptr);

	if (bcode_val == 0xFFFFFFFF) {
		/* No FCP Priority config data in flash */
		DEBUG2(printk(KERN_INFO
			"%s: No FCP Priority config data.\n",
			__func__));
		return 0;
	}

	if (bcode[0x0] != 'H' || bcode[0x1] != 'Q' || bcode[0x2] != 'O' ||
	    bcode[0x3] != 'S') {
		/* Invalid FCP Priority data header */
		DEBUG2(printk(KERN_INFO
			"%s: Invalid FCP Priority data header. bcode=0x%x\n",
			__func__, bcode_val));
		return 0;
	}

	if (flag != 1)
		return ret;

	pri_entry = &pri_cfg->entry[0];
	for (i = 0; i < pri_cfg->num_entries; i++) {
		if (pri_entry->flags & (FCP_PRIO_ENTRY_VALID |
					FCP_PRIO_ENTRY_TAG_VALID))
			num_valid++;
		pri_entry++;
	}

	if (pri_cfg->num_entries && num_valid == 0) {
		/* No valid FCP priority data entries */
		DEBUG2(printk(KERN_INFO
			"%s: No valid FCP Priority data entries.\n",
			__func__));
		ret = 0;
	} else {
		/* Valid FCP Priority data entries */
		DEBUG2(printk(KERN_INFO
			"%s: Valid FCP Priority data. num entries=%d\n",
			__func__, num_valid));
	}

	return ret;
}

static int
qla24xx_proc_fcp_prio_cfg_cmd(scsi_qla_host_t *ha, struct sk_buff *skb,
    struct nlmsghdr *nlh, struct qla_fc_msg *ql_cmd, int rcvlen)
{
	int ret = 0;
	uint32_t len;
	struct qla_fcp_prio_param *param;
	int rsp_hdr_len = offsetof(struct qla_fc_msg, u) +
	    offsetof(struct qla_fcp_prio_param, fcp_prio_cfg);
	uint8_t *payload = NULL;

	if (!(IS_QLA24XX_TYPE(ha) || IS_QLA25XX(ha))) {
		ql_cmd->error = -EINVAL;
		len = 0;
		goto exit_fcp_prio_cfg;
	}

	param = &ql_cmd->u.fcp_prio_param;
	len = param->fcp_prio_cfg_size;

	/* Only set config is allowed if config memory is not allocated */
	if (!ha->fcp_prio_cfg && (param->oper != QLFC_FCP_PRIO_SET_CONFIG)) {
		ret = -EINVAL;
		goto exit_fcp_prio_cfg;
	}

	switch (param->oper) {
	case QLFC_FCP_PRIO_DISABLE:
		if (ha->flags.fcp_prio_enabled) {
			ha->flags.fcp_prio_enabled = 0;
			ha->fcp_prio_cfg->attributes &=
			    ~FCP_PRIO_ATTR_ENABLE;
			qla24xx_update_all_fcp_prio(ha);
		}
		break;

	case QLFC_FCP_PRIO_ENABLE:
		if (!ha->flags.fcp_prio_enabled) {
			if (ha->fcp_prio_cfg) {
				ha->flags.fcp_prio_enabled = 1;
				ha->fcp_prio_cfg->attributes |=
				    FCP_PRIO_ATTR_ENABLE;
				qla24xx_update_all_fcp_prio(ha);
			} else {
				ret = -EINVAL;
				goto exit_fcp_prio_cfg;
			}
		}
		break;

	case QLFC_FCP_PRIO_GET_CONFIG:
		if (!len || len > FCP_PRIO_CFG_SIZE) {
			ret = -EINVAL;
			goto exit_fcp_prio_cfg;
		}

		payload = (uint8_t *)ha->fcp_prio_cfg;
		break;

	case QLFC_FCP_PRIO_SET_CONFIG:
		if (!len || len > FCP_PRIO_CFG_SIZE) {
			ret = -EINVAL;
			len = 0;
			goto exit_fcp_prio_cfg;
		}

		/* validate fcp priority data */
		if (!qla24xx_fcp_prio_cfg_valid(
		    (struct qla_fcp_prio_cfg *)param->fcp_prio_cfg, 1)) {
			ret = -EINVAL;
			len = 0;
			goto exit_fcp_prio_cfg;
		}

		if (!ha->fcp_prio_cfg) {
			ha->fcp_prio_cfg = vmalloc(FCP_PRIO_CFG_SIZE);
			if (!ha->fcp_prio_cfg) {
				qla_printk(KERN_WARNING, ha,
				    "Unable to allocate memory for fcp prio "
				    "config data (%x).\n", FCP_PRIO_CFG_SIZE);
				ret = -ENOMEM;
				len = 0;
				goto exit_fcp_prio_cfg;
			}
		}

		memset(ha->fcp_prio_cfg, 0, FCP_PRIO_CFG_SIZE);
		memcpy(ha->fcp_prio_cfg, param->fcp_prio_cfg, len);

		ha->flags.fcp_prio_enabled = 0;
		if (ha->fcp_prio_cfg->attributes & FCP_PRIO_ATTR_ENABLE)
			ha->flags.fcp_prio_enabled = 1;
		qla24xx_update_all_fcp_prio(ha);
		len = 0;
		break;

	default:
		ret = -EINVAL;
		len = 0;
	}

exit_fcp_prio_cfg:
	ql_cmd->error = ret;
	return ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
	    (uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, payload, len);
}

static int
qla82xx_diag_mode_cmd(scsi_qla_host_t *ha, struct sk_buff *skb,
    struct nlmsghdr *nlh, struct qla_fc_msg *ql_cmd, int rcvlen)
{
	int ret = 0;
	uint32_t new_state;
	uint32_t drv_state;
	unsigned long reset_timeout;
	uint32_t diag_mode_cmd;

	int rsp_hdr_len = offsetof(struct qla_fc_msg, u);

	diag_mode_cmd = ql_cmd->u.diag_mode;

	switch (diag_mode_cmd) {
	case QLFC_SET_DIAG_MODE:
		DEBUG(qla_printk(KERN_INFO, ha,
				"Set QUISCENT mode\n"));
		qla82xx_idc_lock(ha);
		ha->flags.quiesce_owner = 1;
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
				 QLA82XX_DEV_NEED_QUIESCENT);
		qla82xx_idc_unlock(ha);
		new_state = qla82xx_wait_for_state_change(ha,
				QLA82XX_DEV_NEED_QUIESCENT);
		if (new_state == QLA82XX_DEV_READY)
			ret = -1;
			break;

	case QLFC_RESET_DIAG_MODE:
		DEBUG(qla_printk(KERN_INFO, ha,
				"Reset QUISCENT mode\n"));
		qla82xx_idc_lock(ha);
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
				QLA82XX_DEV_READY);
		qla82xx_idc_unlock(ha);

		qla2x00_perform_loop_resync(ha);

		/* Wait for 30 seconds for quiescent reset from other
		 * functions*/
		reset_timeout = jiffies + (30 * HZ);
		do {
			msleep(1000);
			qla82xx_idc_lock(ha);
			drv_state = qla82xx_rd_32(ha,
					QLA82XX_CRB_DRV_STATE);
			drv_state &= ~(QLA82XX_DRVST_QSNT_RDY << \
						(ha->portnum * 4));
			qla82xx_idc_unlock(ha);

			if (time_after_eq(jiffies, reset_timeout)) {
				qla_printk(KERN_INFO, ha,
					"TIMEOUT at reset quiescent\n");
				qla_printk(KERN_INFO, ha,
					"DRV_STATE: %d\n", drv_state);
				break;
			}
		} while (drv_state);

		qla82xx_idc_lock(ha);
		qla82xx_clear_qsnt_ready(ha);
		ha->flags.quiesce_owner = 0;
		qla82xx_idc_unlock(ha);
		break;

	default:
		DEBUG(printk(KERN_INFO"%s(%ld): inst=%ld \
				Invalid sub command.\n",
				__func__, ha->host_no, ha->instance));
		ret = -1;
		break;
	}

	ql_cmd->error = ret;
	return ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
	    (uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
}

static int
qla2x00_update_fru_versions(struct scsi_qla_host *ha, struct sk_buff *skb,
    struct nlmsghdr *nlh, struct qla_fc_msg *ql_cmd, int rcvlen)
{
	int rsp_hdr_len = offsetof(struct qla_fc_msg, u);
	dma_addr_t sfp_dma;
	void *sfp = NULL;
	int ret;
	struct qla_image_version_list *list = &ql_cmd->u.fru_img;
	struct qla_image_version *image = list->version;
	uint32_t count = list->count;
	uint32_t datlen = sizeof(*list) + sizeof(*image) * count;

	if (rcvlen - sizeof(struct scsi_nl_hdr) < datlen) {
		DEBUG(printk(KERN_ERR "%s(%lu): invalid len\n",
		    __func__, ha->host_no));
		ql_cmd->error = EXT_STATUS_INVALID_PARAM;
		goto done;
	}

	sfp = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &sfp_dma);
	if (!sfp) {
		DEBUG(printk(KERN_ERR "%s(%lu): failed alloc\n",
		    __func__, ha->host_no));
		ql_cmd->error = EXT_STATUS_NO_MEMORY;
		goto done;
	}

	for ( ; count--; image++) {
		memcpy(sfp, &image->field_info, sizeof(image->field_info));
		ret = qla2x00_write_sfp(ha, sfp_dma, sfp,
		    image->field_address.device, image->field_address.offset,
		    sizeof(image->field_info), image->field_address.option);
		if (ret) {
			DEBUG16(printk(KERN_ERR "%s(%lu): failed write %x\n",
			    __func__, ha->host_no, ret));
			ql_cmd->error = EXT_STATUS_MAILBOX;
			goto done;
		}
	}

	ql_cmd->error = EXT_STATUS_OK;

done:
	if (sfp)
		dma_pool_free(ha->s_dma_pool, sfp, sfp_dma);

	return ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
	    (uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
}

static int
qla2x00_read_fru_status(struct scsi_qla_host *ha, struct sk_buff *skb,
    struct nlmsghdr *nlh, struct qla_fc_msg *ql_cmd, int rcvlen)
{
	int rsp_hdr_len = offsetof(struct qla_fc_msg, u) +
	    sizeof(ql_cmd->u.stat_reg);
	dma_addr_t sfp_dma;
	uint8_t *sfp = NULL;
	int ret;
	struct qla_status_reg *sr = &ql_cmd->u.stat_reg;

	if (rcvlen - sizeof(struct scsi_nl_hdr) < sizeof(*sr)) {
		DEBUG(printk(KERN_ERR "%s(%lu): invalid len\n",
		    __func__, ha->host_no));
		ql_cmd->error = EXT_STATUS_INVALID_PARAM;
		goto done;
	}

	sfp = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &sfp_dma);
	if (!sfp) {
		DEBUG(printk(KERN_ERR "%s(%lu): failed alloc\n",
		    __func__, ha->host_no));
		ql_cmd->error = EXT_STATUS_NO_MEMORY;
		goto done;
	}

	ret = qla2x00_read_sfp(ha, sfp_dma, sfp,
	    sr->field_address.device, sr->field_address.offset,
	    sizeof(sr->status_reg), sr->field_address.option);
	if (ret) {
		DEBUG(printk(KERN_ERR "%s(%lu): failed read %x\n",
		    __func__, ha->host_no, ret));
		ql_cmd->error = EXT_STATUS_MAILBOX;
		goto done;
	}

	memcpy(&sr->status_reg, sfp, sizeof(sr->status_reg));

	ql_cmd->error = EXT_STATUS_OK;

done:
	if (sfp)
		dma_pool_free(ha->s_dma_pool, sfp, sfp_dma);

	return ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
	    (uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
}

static int
qla2x00_write_fru_status(struct scsi_qla_host *ha, struct sk_buff *skb,
    struct nlmsghdr *nlh, struct qla_fc_msg *ql_cmd, int rcvlen)
{
	int rsp_hdr_len = offsetof(struct qla_fc_msg, u);
	dma_addr_t sfp_dma;
	uint8_t *sfp = NULL;
	int ret;
	struct qla_status_reg *sr = &ql_cmd->u.stat_reg;

	if (rcvlen - sizeof(struct scsi_nl_hdr) < sizeof(*sr)) {
		DEBUG(printk(KERN_ERR "%s(%lu): invalid len\n",
		    __func__, ha->host_no));
		ql_cmd->error = EXT_STATUS_INVALID_PARAM;
		goto done;
	}

	sfp = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &sfp_dma);
	if (!sfp) {
		DEBUG(printk(KERN_ERR "%s(%lu): failed alloc\n",
		    __func__, ha->host_no));
		ql_cmd->error = EXT_STATUS_NO_MEMORY;
		goto done;
	}

	memcpy(sfp, &sr->status_reg, sizeof(sr->status_reg));

	ret = qla2x00_write_sfp(ha, sfp_dma, sfp,
	    sr->field_address.device, sr->field_address.offset,
	    sizeof(sr->status_reg), sr->field_address.option);
	if (ret) {
		DEBUG(printk(KERN_ERR "%s(%lu): failed write %x\n",
		    __func__, ha->host_no, ret));
		ql_cmd->error = EXT_STATUS_MAILBOX;
		goto done;
	}

	ql_cmd->error = EXT_STATUS_OK;

done:
	if (sfp)
		dma_pool_free(ha->s_dma_pool, sfp, sfp_dma);

	return ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
	    (uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
}

/*
 * Netlink Interface Related Functions
 */

void
ql_fc_nl_rcv(struct sock *sk, int len)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sk->sk_receive_queue))) {
		ql_fc_nl_rcv_msg(skb);
		kfree_skb(skb);
	}
}

static void
ql_fc_nl_rcv_msg(struct sk_buff *skb)
{
	struct nlmsghdr *nlh;
	struct scsi_nl_hdr *snlh;
	uint32_t rlen;
	int err;

	while (skb->len >= NLMSG_SPACE(0)) {
		err = 0;

		nlh = (struct nlmsghdr *) skb->data;

		if ((nlh->nlmsg_len < (sizeof(*nlh) + sizeof(*snlh))) ||
		    (skb->len < nlh->nlmsg_len)) {
			DEBUG16(printk(KERN_WARNING "%s: discarding partial "
			    "skb\n", __func__));
			break;
		}

		rlen = NLMSG_ALIGN(nlh->nlmsg_len);
		if (rlen > skb->len) {
			DEBUG16(printk(KERN_WARNING "%s: rlen > skb->len\n",
				 __func__));
			rlen = skb->len;
		}

		if (nlh->nlmsg_type != FC_TRANSPORT_MSG) {
			DEBUG16(printk(KERN_WARNING "%s: Not FC_TRANSPORT_MSG\n"
			    , __func__));
			err = -EBADMSG;
			goto next_msg;
		}

		snlh = NLMSG_DATA(nlh);
		if ((snlh->version != SCSI_NL_VERSION) ||
		    (snlh->magic != SCSI_NL_MAGIC)) {
			DEBUG16(printk(KERN_WARNING "%s: Bad Version or Magic"
			    " number\n", __func__));
			err = -EPROTOTYPE;
			goto next_msg;
		}
		err = ql_fc_proc_nl_rcv_msg(skb, nlh, rlen);
next_msg:
		if (err)
			netlink_ack(skb, nlh, err);
		skb_pull(skb, rlen);
	}
}

static int
ql_fc_proc_nl_rcv_msg(struct sk_buff *skb, struct nlmsghdr *nlh, int rcvlen)
{
	struct scsi_nl_hdr *snlh;
	struct qla_fc_msg  *ql_cmd;
	struct Scsi_Host *shost;
	struct scsi_qla_host *ha;
	int err = 0;
	int rsp_hdr_len;
	
	snlh = NLMSG_DATA(nlh);

	/* Only vendor specific commands are supported */
	if (!(snlh->msgtype & FC_NL_VNDR_SPECIFIC))
		return -EBADMSG;
	
	ql_cmd = (struct qla_fc_msg *)((char *)snlh + sizeof (struct scsi_nl_hdr));

	if (ql_cmd->magic != QL_FC_NL_MAGIC)
		return -EBADMSG;

	shost = scsi_host_lookup(ql_cmd->host_no);
	if (!shost) {
		DEBUG16(printk(KERN_ERR "%s: could not find host no %u\n",
		    __func__, ql_cmd->host_no));
		err = -ENODEV;
		goto exit_proc_nl_rcv_msg;
	}

	ha = (struct scsi_qla_host *)shost->hostdata;

	if (!ha) {
		DEBUG16(printk(KERN_ERR "%s: found invalid host.\n", __func__));
		err = -ENODEV;
		goto exit_proc_nl_rcv_msg;
	}

	rsp_hdr_len = offsetof(struct qla_fc_msg, u);

	if (qla2x00_reset_active(ha)) {
		DEBUG16(printk(KERN_ERR "%s: ISP abort active/needed -- "
		    "cmd=%d\n", __func__, ql_cmd->cmd));
		ql_cmd->error = -EBUSY;
		return ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
			(uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL,
			0);
	}

	switch (ql_cmd->cmd) {
	case QLFC_GET_AEN:
		rsp_hdr_len = offsetof(struct qla_fc_msg, u);
		ql_cmd->error = 0;
		ql_fc_get_aen(ha);
		err =  ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
			(uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len,
			&aen_log, sizeof(struct qlfc_aen_log));
		goto exit_proc_nl_rcv_msg;

	case QLFC_LOOPBACK_CMD:
	case QLFC_LOOPBACK_DATA:
		err = ql_fc_loopback(ha, skb, nlh, ql_cmd, rcvlen);
		goto exit_proc_nl_rcv_msg;

	case QLFC_IIDMA:
		err = ql_fc_iidma(ha, skb, nlh, ql_cmd, rcvlen);
		goto exit_proc_nl_rcv_msg;

	case QLFC_FCP_PRIO_CFG_CMD:
		err = qla24xx_proc_fcp_prio_cfg_cmd(ha, skb, nlh, ql_cmd,
		    rcvlen);
		goto exit_proc_nl_rcv_msg;

	case QLFC_DIAG_MODE:
		err = qla82xx_diag_mode_cmd(ha, skb, nlh, ql_cmd, rcvlen);
		goto exit_proc_nl_rcv_msg;

	case QLFC_SET_FRU_VPD:
		err = qla2x00_update_fru_versions(ha, skb, nlh, ql_cmd, rcvlen);
		goto exit_proc_nl_rcv_msg;

	case QLFC_READ_FRU_STATUS:
		err = qla2x00_read_fru_status(ha, skb, nlh, ql_cmd, rcvlen);
		goto exit_proc_nl_rcv_msg;

	case QLFC_WRITE_FRU_STATUS:
		err = qla2x00_write_fru_status(ha, skb, nlh, ql_cmd, rcvlen);
		goto exit_proc_nl_rcv_msg;
	}

	/* Use existing 84xx interface to get MPI XGMAC statistics for
	 * 81xx via FC interface
	 */
	if ((!IS_QLA8XXX_TYPE(ha) && !IS_QLA84XX(ha)) ||
	    (IS_QLA8XXX_TYPE(ha) && (ql_cmd->cmd != QLA84_MGMT_CMD))) {
		DEBUG16(printk(KERN_ERR "%s: invalid host ha = %p"
		    "dtype = 0x%x\n", __func__, ha, (ha ? DT_MASK(ha): ~0)));
		err = -ENODEV;
		goto exit_proc_nl_rcv_msg;
	}

	switch (ql_cmd->cmd) {

	case QLA84_RESET:
	
		rsp_hdr_len = offsetof(struct qla_fc_msg, u);
		err = qla84xx_reset(ha, ql_cmd->u.utok.qla84_reset.diag_fw);
		ql_cmd->error = err;

		err = ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
			(uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
		break;

	case QLA84_UPDATE_FW:
		rsp_hdr_len = offsetof(struct qla_fc_msg, u);
		err = qla84xx_update_fw(ha, (rcvlen - sizeof(struct scsi_nl_hdr)),
				&ql_cmd->u.utok.qla84_update_fw);
		ql_cmd->error = err;

		err = ql_fc_nl_rsp(NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
			(uint32_t)nlh->nlmsg_type, ql_cmd, rsp_hdr_len, NULL, 0);
		break;

	case QLA84_MGMT_CMD:
		if (IS_QLA84XX(ha))
			err = qla84xx_mgmt_cmd(ha, ql_cmd,
				(rcvlen - sizeof(struct scsi_nl_hdr)),
				NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
				(uint32_t)nlh->nlmsg_type);
		else if (IS_QLA8XXX_TYPE(ha))
			err = qla81xx_mgmt_cmd(ha, ql_cmd,
				(rcvlen - sizeof(struct scsi_nl_hdr)),
				NETLINK_CREDS(skb)->pid, nlh->nlmsg_seq,
				(uint32_t)nlh->nlmsg_type);
		break;

	default:
		err = -EBADMSG;
	}

exit_proc_nl_rcv_msg:
	if (shost)
		scsi_host_put(shost);
	return err;
}

static int
ql_fc_nl_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	DEBUG16(printk(KERN_WARNING "%s: event 0x%lx ptr = %p\n",
	    __func__, event, ptr));
	return NOTIFY_DONE;
}

static int
ql_fc_nl_rsp(uint32_t pid, uint32_t seq, uint32_t type, void *hdr, int hdr_len,
	void *payload, int size)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	int rc;
	int len = NLMSG_SPACE(size + hdr_len);
	
	skb = alloc_skb(len, GFP_ATOMIC);
	if (!skb) {
		DEBUG2(printk(KERN_ERR "%s: Could not alloc skb\n", __func__));
		return -ENOMEM;
	}
	nlh = __nlmsg_put(skb, pid, seq, type, (len - sizeof(*nlh)), 0);
	nlh->nlmsg_flags = 0;
	memcpy(NLMSG_DATA(nlh), hdr, hdr_len);
	
	if (payload)
		memcpy((void *)((char *)(NLMSG_DATA(nlh)) + hdr_len), payload, size);
	
	rc = netlink_unicast(ql_fc_nl_sock, skb, pid, MSG_DONTWAIT);
	if (rc < 0) {
		DEBUG16(printk(KERN_ERR "%s: netlink_unicast failed\n",
		    __func__));
		return rc;
	}
	return 0;
}

void qla_free_nlnk_dmabuf(scsi_qla_host_t *ha)
{
	struct qlfc_fw *qlfw;

	qlfw = &ha->fw_buf;

	if (qlfw->fw_buf) {
		dma_free_coherent(&ha->pdev->dev, qlfw->len, qlfw->fw_buf,
			qlfw->fw_dma);
		memset(qlfw, 0, sizeof(struct qlfc_fw));
	}
}

int
ql_nl_register(void)
{
	int error = 0;

	error = netlink_register_notifier(&ql_fc_nl_notifier);
	if (!error) {
		ql_fc_nl_sock = netlink_kernel_create(NETLINK_FCTRANSPORT,
					QL_FC_NL_GROUP_CNT, ql_fc_nl_rcv,
					THIS_MODULE);
		if (!ql_fc_nl_sock) {
			netlink_unregister_notifier(&ql_fc_nl_notifier);
			error = -ENODEV;
		}
	}
	return (error);
}

void
ql_nl_unregister()
{
	if (ql_fc_nl_sock) {
		sock_release(ql_fc_nl_sock->sk_socket);
		netlink_unregister_notifier(&ql_fc_nl_notifier);
	}
}

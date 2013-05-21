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

static void
ctio_sglist_map(struct qsio_scsiio *ctio, struct tgtcmd *tcmd)
{
	sglist_t *sglist;
	struct pgdata **pglist;
	int i;
	int dxfer_len = ctio->dxfer_len;

	sglist = malloc(ctio->pglist_cnt * sizeof(sglist_t), M_QISP, M_WAITOK);
	pglist = (struct pgdata **)ctio->data_ptr;
	for (i = 0; i < ctio->pglist_cnt; i++)
	{
		struct pgdata *pgtmp = pglist[i];
		int min_len;

		sglist_t *sgtmp = &sglist[i];

		sgtmp->iov_base = (caddr_t)pgdata_page_address(pgtmp) + pgtmp->pg_offset;
		min_len = min_t(int, dxfer_len, pgtmp->pg_len); 
		DEBUG_BUG_ON(!min_len);
		sgtmp->iov_len = min_len;
		dxfer_len -= min_len;
	}
	tcmd->sglist = sglist;
}

extern struct sx fcbridges_lock;
extern struct qs_interface_cbs icbs;

void 
qla_end_ctio_ccb(struct qsio_scsiio *ctio)
{
	struct fcbridge *fcbridge;
	struct tgtcmd *cmd = ctio_cmd(ctio);

 	fcbridge = ctio_fcbridge(ctio);
	if (ctio->ccb_h.flags & QSIO_DIR_OUT && !cmd->aborted) {
		DEBUG_INFO("received data for cmd %x\n", ctio->cdb[0]);
		if (cmd->dmap) {
			isp_common_dmateardownt(fcbridge->ha, ctio);
			cmd->dmap = NULL;
		}

		if (cmd->sglist)
		{
			free(cmd->sglist, M_QISP);
			cmd->sglist = NULL;
		}

		if (unlikely(ctio->scsi_status != 0))
		{
			__ctio_free_all(ctio, cmd->local_pool);
			return;
		}
		ctio->ccb_h.flags &= ~QSIO_DIR_OUT;
		__ctio_queue_cmd(ctio);
		return;
	}
	else if (ctio->ccb_h.flags & QSIO_DIR_OUT && cmd->aborted && !cmd->local_pool) {
		struct ccb_list ctio_list;

		STAILQ_INIT(&ctio_list);
		(*icbs.device_remove_ctio)(ctio, &ctio_list);
		(*icbs.device_queue_ctio_list)(&ctio_list);
	}

	if (cmd->dmap) {
		isp_common_dmateardownt(fcbridge->ha, ctio);
	}

	if (cmd->sglist) {
		free(cmd->sglist, M_QISP);
	}

	slab_cache_free(fcbridge->ha->tptr->tgt_cache, cmd);
	__ctio_free_all(ctio, cmd->local_pool);
}

static void
tgtcmd_free(tstate_t *tptr, struct tgtcmd *tcmd)
{
	if (tcmd->sglist)
	{
		free(tcmd->sglist, M_QISP);
	}
	slab_cache_free(tptr->tgt_cache, tcmd);
}

static void 
qla_end_notify(struct qsio_immed_notify *notify)
{
	ispsoftc_t *isp = notify_host(notify);
	struct tgtcmd *cmd = notify_cmd(notify);
	inot_private_data_t *ntp = &cmd->u.itp;

	if (IS_24XX(isp)) {
		in_fcentry_24xx_t *inot = (in_fcentry_24xx_t *)&ntp->rd.nt;
		if (notify->notify_status)
			inot->in_status = FC_TM_FAILED;
	}
	else {
		in_fcentry_t *inp = (in_fcentry_t *)&ntp->rd.nt;
		inp->in_status = notify->notify_status ? FC_TM_FAILED : 0;
	}
	mtx_lock(&isp->isp_lock);
	isp_notify_ack(isp, &ntp->rd.nt);
	mtx_unlock(&isp->isp_lock);
	free(notify, M_QISP);
	tgtcmd_free(isp->tptr, cmd);
}

static void 
qla_end_ctio(struct qsio_scsiio *ctio)
{
	struct fcbridge *fcbridge;
	ispsoftc_t *isp;
	struct ccb_list ctio_list;

	STAILQ_INIT(&ctio_list);
	if ((ctio->ccb_h.flags & QSIO_SEND_STATUS) && ctio->ccb_h.target_lun && ctio->ccb_h.tdevice) {
		(*icbs.device_remove_ctio)(ctio, &ctio_list);
	}

	fcbridge = ctio_fcbridge(ctio);
	isp = fcbridge->ha;

	if ((ctio->ccb_h.flags & QSIO_CTIO_ABORTED)) {
		if ((ctio->ccb_h.flags & QSIO_SEND_ABORT_STATUS)) {
			__ctio_free_data(ctio);
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASC, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASCQ);
		}
		else {
			slab_cache_free(isp->tptr->tgt_cache, ctio_cmd(ctio));
			__ctio_free_all(ctio, ctio_cmd(ctio)->local_pool);
			goto send;
		}
	}

	DEBUG_INFO("cmd %x flags %x\n", ctio->cdb[0], ctio->ccb_h.flags);
	if (ctio->pglist_cnt)
	{
		ctio_sglist_map(ctio, ctio_cmd(ctio));
	}

again:
	mtx_lock(&fcbridge->ha->isp_lock);
	isp_target_start_ctio(isp, ctio);
	mtx_unlock(&fcbridge->ha->isp_lock);
	if (unlikely(ctio->ccb_h.flags & QSIO_HBA_ERROR)) {
		slab_cache_free(isp->tptr->tgt_cache, ctio_cmd(ctio));
		__ctio_free_all(ctio, ctio_cmd(ctio)->local_pool);
		goto send;
	}
	else if (ctio->ccb_h.flags & QSIO_HBA_REQUEUE) {
		uio_yield();
		goto again;
	}
send:
	if (!STAILQ_EMPTY(&ctio_list))
		(*icbs.device_queue_ctio_list)(&ctio_list);
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

int
fcbridge_i_prt_valid(struct fcbridge *fcbridge, uint64_t i_prt[])
{
	ispsoftc_t *isp = fcbridge->ha;
	int i;

	if (i_prt[1])
		return 1;

	for (i = 0; i < isp->isp_nchan; i++) { 
		if (i_prt[0] == FCPARAM(isp, i)->isp_wwpn)
			return 0;
	}
	return 1;
}

void
fcbridge_get_tport(struct fcbridge *fcbridge, uint64_t wwpn[])
{
	wwpn[0] = FCPARAM(fcbridge->ha, 0)->isp_wwpn;
	wwpn[1] = 0; 
}

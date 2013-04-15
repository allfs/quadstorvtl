#include "isp_freebsd.h"
#include "qla_sc.h"

void
isp_complete_ctio(struct qsio_scsiio *cso)
{
	qla_end_ctio_ccb(cso);
}

void
isp_common_dmateardownt(ispsoftc_t *isp, struct qsio_scsiio *csio)
{
	struct tgtcmd *tcmd = ctio_cmd(csio);

	if (!tcmd->dmap)
		return;
	if (csio->ccb_h.flags & QSIO_DIR_OUT) {
		bus_dmamap_sync(isp->isp_osinfo.dmat, tcmd->dmap, BUS_DMASYNC_POSTREAD);
	} else {
		bus_dmamap_sync(isp->isp_osinfo.dmat, tcmd->dmap, BUS_DMASYNC_POSTWRITE);
	}
	bus_dmamap_unload(isp->isp_osinfo.dmat, tcmd->dmap);
	tcmd->dmap = NULL;
}

void
isp_handle_platform_atio7(ispsoftc_t *isp, at7_entry_t *aep)
{
	int cdbxlen;
	uint16_t lun, chan, nphdl = NIL_HANDLE;
	uint32_t did, sid;
	uint64_t wwn = INI_NONE;
	fcportdb_t *lp;
	tstate_t *tptr;
	struct tgtcmd *atiop;
	atio_private_data_t *atp = NULL;

	did = (aep->at_hdr.d_id[0] << 16) | (aep->at_hdr.d_id[1] << 8) | aep->at_hdr.d_id[2];
	sid = (aep->at_hdr.s_id[0] << 16) | (aep->at_hdr.s_id[1] << 8) | aep->at_hdr.s_id[2];
	lun = (aep->at_cmnd.fcp_cmnd_lun[0] << 8) | aep->at_cmnd.fcp_cmnd_lun[1];

	/*
	 * Find the N-port handle, and Virtual Port Index for this command.
	 *
	 * If we can't, we're somewhat in trouble because we can't actually respond w/o that information.
	 * We also, as a matter of course, need to know the WWN of the initiator too.
	 */
	if (ISP_CAP_MULTI_ID(isp)) {
		/*
		 * Find the right channel based upon D_ID
		 */
		isp_find_chan_by_did(isp, did, &chan);

		if (chan == ISP_NOCHAN) {
			NANOTIME_T now;

			/*
			 * If we don't recognizer our own D_DID, terminate the exchange, unless we're within 2 seconds of startup
			 * It's a bit tricky here as we need to stash this command *somewhere*.
			 */
			GET_NANOTIME(&now);
			if (NANOTIME_SUB(&isp->isp_init_time, &now) > 2000000000ULL) {
				isp_prt(isp, ISP_LOGWARN, "%s: [RX_ID 0x%x] D_ID %x not found on any channel- dropping", __func__, aep->at_rxid, did);
				isp_endcmd(isp, aep, NIL_HANDLE, ISP_NOCHAN, ECMD_TERMINATE, 0);
				return;
			}
			isp_prt(isp, ISP_LOGWARN, "%s: [RX_ID 0x%x] D_ID %x not found on any channel- deferring", __func__, aep->at_rxid, did);
			goto noresrc;
		}
		isp_prt(isp, ISP_LOGTDEBUG0, "%s: [RX_ID 0x%x] D_ID 0x%06x found on Chan %d for S_ID 0x%06x", __func__, aep->at_rxid, did, chan, sid);
	} else {
		chan = 0;
	}

	/*
	 * Get the tstate pointer
	 */
	tptr = isp->tptr;
	if (!tptr) {
		DEBUG_WARN_NEW("tptr not set\n");
		isp_endcmd(isp, aep, nphdl, chan, SCSI_BUSY, 0);
		return;
	}

	/*
	 * Find the PDB entry for this initiator
	 */
	if (isp_find_pdb_by_sid(isp, chan, sid, &lp) == 0) {
		/*
		 * If we're not in the port database terminate the exchange.
		 */
		isp_prt(isp, ISP_LOGTINFO, "%s: [RX_ID 0x%x] D_ID 0x%06x found on Chan %d for S_ID 0x%06x wasn't in PDB already",
		    __func__, aep->at_rxid, did, chan, sid);
		isp_endcmd(isp, aep, NIL_HANDLE, chan, ECMD_TERMINATE, 0);
		return;
	}
	nphdl = lp->handle;
	wwn = lp->port_wwn;

	/*
	 * If the f/w is out of resources, just send a BUSY status back.
	 */
	if (aep->at_rxid == AT7_NORESRC_RXID) {
		isp_endcmd(isp, aep, nphdl, chan, SCSI_BUSY, 0);
		return;
	}

	/*
	 * If we're out of resources, just send a BUSY status back.
	 */
	atiop = slab_cache_alloc(tptr->tgt_cache, M_NOWAIT|M_ZERO, sizeof(*atiop));
	if (atiop == NULL) {
		isp_prt(isp, ISP_LOGTDEBUG0, "[0x%x] out of atios", aep->at_rxid);
		goto noresrc;
	}
	atiop->i_prt = wwn;
	atiop->t_prt = FCPARAM(isp, chan)->isp_wwpn;
	atiop->r_prt = FC_RPORT_START + device_get_unit(isp->isp_dev) + chan;

	atp = &atiop->u.atp;
	atp->tag = aep->at_rxid;
	atp->state = ATPD_STATE_ATIO;
	atiop->init_id = nphdl;
	atiop->target_lun = lun;
	atiop->chan = chan;
	cdbxlen = aep->at_cmnd.fcp_cmnd_alen_datadir >> FCP_CMND_ADDTL_CDBLEN_SHIFT;
	if (cdbxlen) {
		isp_prt(isp, ISP_LOGWARN, "additional CDBLEN ignored");
	}
	cdbxlen = sizeof (aep->at_cmnd.cdb_dl.sf.fcp_cmnd_cdb);
	ISP_MEMCPY(atiop->cdb, aep->at_cmnd.cdb_dl.sf.fcp_cmnd_cdb, cdbxlen);
	atiop->tag_id = atp->tag;
	switch (aep->at_cmnd.fcp_cmnd_task_attribute & FCP_CMND_TASK_ATTR_MASK) {
	case FCP_CMND_TASK_ATTR_SIMPLE:
	case FCP_CMND_TASK_ATTR_UNTAGGED:
		atiop->tag_action = MSG_SIMPLE_Q_TAG;
		break;
	case FCP_CMND_TASK_ATTR_HEAD:
		atiop->tag_action = MSG_HEAD_OF_Q_TAG;
		break;
	case FCP_CMND_TASK_ATTR_ORDERED:
		atiop->tag_action = MSG_ORDERED_Q_TAG;
		break;
	default:
		/* FALLTHROUGH */
	case FCP_CMND_TASK_ATTR_ACA:
		atiop->tag_action = MSG_ORDERED_Q_TAG;
		break;
	}
	atp->orig_datalen = aep->at_cmnd.cdb_dl.sf.fcp_cmnd_dl;
	atp->bytes_xfered = 0;
	atp->last_xframt = 0;
	atp->lun = lun;
	atp->nphdl = nphdl;
	atp->portid = sid;
	atp->oxid = aep->at_hdr.ox_id;
	atp->cdb0 = atiop->cdb[0];
	atp->tattr = aep->at_cmnd.fcp_cmnd_task_attribute & FCP_CMND_TASK_ATTR_MASK;
	atp->state = ATPD_STATE_CAM;
	fcq_insert_cmd(tptr->fcbridge, atiop);
	return;
noresrc:
	isp_endcmd(isp, aep, nphdl, chan, SCSI_STATUS_BUSY, 0);
}

void
isp_handle_platform_ctio(ispsoftc_t *isp, void *arg)
{
	struct qsio_scsiio *cso;
	struct tgtcmd *tcmd;
	int sentstatus = 0, ok, notify_cam = 0, resid = 0;
	tstate_t *tptr = NULL;
	atio_private_data_t *atp = NULL;
	uint32_t tval = 0, handle;

	/*
	 * CTIO handles are 16 bits.
	 * CTIO2 and CTIO7 are 32 bits.
	 */

	handle = ((ct2_entry_t *)arg)->ct_syshandle;
	cso = isp_find_xs_tgt(isp, handle);
	if (cso == NULL) {
		isp_print_bytes(isp, "null ccb in isp_handle_platform_ctio", QENTRY_LEN, arg);
		return;
	}
	tcmd = ctio_cmd(cso);
	isp_destroy_tgt_handle(isp, handle);
	tptr = isp->tptr;
	KASSERT((tptr != NULL), ("cannot get state pointer"));
	if (isp->isp_nactive) {
		isp->isp_nactive++;
	}

	if (IS_24XX(isp)) {
		ct7_entry_t *ct = arg;

		atp = &tcmd->u.atp;
		sentstatus = ct->ct_flags & CT7_SENDSTATUS;
		ok = (ct->ct_nphdl == CT7_OK);
#if 0
		if (ok && sentstatus && (ccb->ccb_h.flags & CAM_SEND_SENSE)) {
			ccb->ccb_h.status |= CAM_SENT_SENSE;
		}
#endif
		notify_cam = ct->ct_header.rqs_seqno & 0x1;
		if ((ct->ct_flags & CT7_DATAMASK) != CT7_NO_DATA) {
			resid = ct->ct_resid;
			atp->bytes_xfered += (atp->last_xframt - resid);
			atp->last_xframt = 0;
		}
		if (ct->ct_nphdl == CT_HBA_RESET) {
			ok = 0;
			notify_cam = 1;
			sentstatus = 1;
			cso->ccb_h.flags |= QSIO_HBA_ERROR;
		} else if (!ok) {
			cso->ccb_h.flags |= QSIO_HBA_ERROR;
		}
		tval = atp->tag;
		atp->state = ATPD_STATE_PDON; /* XXX: should really come after isp_complete_ctio */
	}

	/*
	 * We're here either because intermediate data transfers are done
	 * and/or the final status CTIO (which may have joined with a
	 * Data Transfer) is done.
	 *
	 * In any case, for this platform, the upper layers figure out
	 * what to do next, so all we do here is collect status and
	 * pass information along. Any DMA handles have already been
	 * freed.
	 */
	if (notify_cam == 0) {
		isp_prt(isp, ISP_LOGTDEBUG0, "  INTER CTIO[0x%x] done", tval);
		return;
	}
	isp_prt(isp, ISP_LOGTDEBUG0, "%s CTIO[0x%x] done", (sentstatus)? "  FINAL " : "MIDTERM ", tval);

	isp_complete_ctio(cso);
}

void
isp_handle_platform_target_tmf(ispsoftc_t *isp, isp_notify_t *notify)
{
	tstate_t *tptr;
	fcportdb_t *lp;
	struct tgtcmd *inot = NULL;
	inot_private_data_t *ntp = NULL;
	lun_id_t lun;

	isp_prt(isp, ISP_LOGTDEBUG0, "%s: code 0x%x sid  0x%x tagval 0x%016llx chan %d lun 0x%x", __func__, notify->nt_ncode,
	    notify->nt_sid, (unsigned long long) notify->nt_tagval, notify->nt_channel, notify->nt_lun);
	/*
	 * NB: This assignment is necessary because of tricky type conversion.
	 * XXX: This is tricky and I need to check this. If the lun isn't known
	 * XXX: for the task management function, it does not of necessity follow
	 * XXX: that it should go up stream to the wildcard listener.
	 */
	if (notify->nt_lun == LUN_ANY) {
		lun = CAM_LUN_WILDCARD;
	} else {
		lun = notify->nt_lun;
	}

	tptr = isp->tptr;
	if (!tptr) {
		DEBUG_WARN_NEW("tptr not set\n");
		goto bad;
	}

	inot = slab_cache_alloc(tptr->tgt_cache, M_NOWAIT|M_ZERO, sizeof(*inot));
	if (inot == NULL) {
		isp_prt(isp, ISP_LOGWARN, "%s: out of immediate notify structures for chan %d lun 0x%x", __func__, notify->nt_channel, lun);
		goto bad;
	}
	inot->entry_type = RQSTYPE_NOTIFY;
	inot->i_prt = notify->nt_wwn;
	inot->t_prt = FCPARAM(isp, notify->nt_channel)->isp_wwpn;
	inot->r_prt = FC_RPORT_START + device_get_unit(isp->isp_dev) + notify->nt_channel;

	if (isp_find_pdb_by_sid(isp, notify->nt_channel, notify->nt_sid, &lp) == 0) {
		inot->init_id = CAM_TARGET_WILDCARD;
	} else {
		inot->init_id = lp->handle;
	}
	inot->seq_id = notify->nt_tagval;
	inot->tag_id = notify->nt_tagval >> 32;

	switch (notify->nt_ncode) {
	case NT_ABORT_TASK:
		inot->nt_arg = MSG_ABORT_TASK;
		break;
	case NT_ABORT_TASK_SET:
		inot->nt_arg = MSG_ABORT_TASK_SET;
		break;
	case NT_CLEAR_ACA:
		inot->nt_arg = MSG_CLEAR_ACA;
		break;
	case NT_CLEAR_TASK_SET:
		inot->nt_arg = MSG_CLEAR_TASK_SET;
		break;
	case NT_LUN_RESET:
		inot->nt_arg = MSG_LOGICAL_UNIT_RESET;
		break;
	case NT_TARGET_RESET:
		inot->nt_arg = MSG_TARGET_RESET;
		break;
	default:
		isp_prt(isp, ISP_LOGWARN, "%s: unknown TMF code 0x%x for chan %d lun 0x%x", __func__, notify->nt_ncode, notify->nt_channel, lun);
		goto bad;
	}

	ntp = &inot->u.itp;
	ISP_MEMCPY(&ntp->rd.nt, notify, sizeof (isp_notify_t));
	if (notify->nt_lreserved) {
		ISP_MEMCPY(&ntp->rd.data, notify->nt_lreserved, QENTRY_LEN);
		ntp->rd.nt.nt_lreserved = &ntp->rd.data;
	}
	ntp->rd.seq_id = notify->nt_tagval;
	ntp->rd.tag_id = notify->nt_tagval >> 32;
	fcq_insert_cmd(tptr->fcbridge, inot);
	return;
bad:
	if (notify->nt_need_ack && notify->nt_lreserved) {
		if (((isphdr_t *)notify->nt_lreserved)->rqs_entry_type == RQSTYPE_ABTS_RCVD) {
			(void) isp_acknak_abts(isp, notify->nt_lreserved, ENOMEM);
		} else {
			(void) isp_notify_ack(isp, notify->nt_lreserved);
		}
	}
	if (inot)
		slab_cache_free(tptr->tgt_cache, inot);
}

void
isp_target_start_ctio(ispsoftc_t *isp, struct qsio_scsiio *cso)
{
	void *qe;
	tstate_t *tptr;
	struct tgtcmd *tcmd;
	atio_private_data_t *atp;
	uint32_t dmaresult, handle;
	uint8_t local[QENTRY_LEN];

	cso->ccb_h.flags &= ~QSIO_HBA_REQUEUE;
	tptr = isp->tptr;
	if (!tptr) {
		DEBUG_WARN_NEW("tptr not set\n");
		cso->ccb_h.flags |= QSIO_HBA_ERROR;
		return;
	}

	tcmd = ctio_cmd(cso);
	atp = &tcmd->u.atp;

	qe = isp_getrqentry(isp);
	if (qe == NULL) {
		cso->ccb_h.flags |= QSIO_HBA_REQUEUE;
		goto out;
	}
	memset(local, 0, QENTRY_LEN);

	/*
	 * We're either moving data or completing a command here.
	 */
	if (IS_24XX(isp)) {
		ct7_entry_t *cto = (ct7_entry_t *) local;

		cto->ct_header.rqs_entry_type = RQSTYPE_CTIO7;
		cto->ct_header.rqs_entry_count = 1;
		cto->ct_header.rqs_seqno = 1;
		cto->ct_nphdl = atp->nphdl;
		cto->ct_rxid = atp->tag;
		cto->ct_iid_lo = atp->portid;
		cto->ct_iid_hi = atp->portid >> 16;
		cto->ct_oxid = atp->oxid;
		cto->ct_vpidx = ISP_GET_VPIDX(isp, tcmd->chan);
		cto->ct_scsi_status = cso->scsi_status;
		cto->ct_timeout = 120;
		cto->ct_flags = atp->tattr << CT7_TASK_ATTR_SHIFT;
		if (cso->ccb_h.flags & QSIO_SEND_STATUS) {
			cto->ct_flags |= CT7_SENDSTATUS;
		}
		if (cso->dxfer_len == 0) {
			cto->ct_flags |= CT7_FLAG_MODE1 | CT7_NO_DATA;
			if (cso->scsi_status == SCSI_STATUS_CHECK_COND) {
				int m = min(cso->sense_len, sizeof (struct scsi_sense_data));
				cto->rsp.m1.ct_resplen = cto->ct_senselen = min(m, MAXRESPLEN_24XX);
				memcpy(cto->rsp.m1.ct_resp, &cso->sense_data, cto->ct_senselen);
				cto->ct_scsi_status |= (FCP_SNSLEN_VALID << 8);
			}
		} else {
			cto->ct_flags |= CT7_FLAG_MODE0;
			if (cso->ccb_h.flags & QSIO_DIR_IN) {
				cto->ct_flags |= CT7_DATA_IN;
			} else {
				cto->ct_flags |= CT7_DATA_OUT;
			}
			cto->rsp.m0.reloff = atp->bytes_xfered;
			/*
			 * Don't overrun the limits placed on us
			 */
			if (atp->bytes_xfered + cso->dxfer_len > atp->orig_datalen) {
				cso->dxfer_len = atp->orig_datalen - atp->bytes_xfered;
			}
			atp->last_xframt = cso->dxfer_len;
			cto->rsp.m0.ct_xfrlen = cso->dxfer_len;
		}
		if (cto->ct_flags & CT7_SENDSTATUS) {
			cto->ct_resid = atp->orig_datalen - (atp->bytes_xfered + cso->dxfer_len);
			if (cto->ct_resid < 0) {
				cto->ct_scsi_status |= (FCP_RESID_OVERFLOW << 8);
			} else if (cto->ct_resid > 0) {
				cto->ct_scsi_status |= (FCP_RESID_UNDERFLOW << 8);
			}
			atp->state = ATPD_STATE_LAST_CTIO;
		} else {
			cto->ct_resid = 0;
			atp->state = ATPD_STATE_CTIO;
		}
	}

	if (isp_allocate_xs_tgt(isp, cso, &handle)) {
		cso->ccb_h.flags |= QSIO_HBA_REQUEUE;
		goto out;
	}


	/*
	 * Call the dma setup routines for this entry (and any subsequent
	 * CTIOs) if there's data to move, and then tell the f/w it's got
	 * new things to play with. As with isp_start's usage of DMA setup,
	 * any swizzling is done in the machine dependent layer. Because
	 * of this, we put the request onto the queue area first in native
	 * format.
	 */

	if (IS_24XX(isp)) {
		ct7_entry_t *cto = (ct7_entry_t *) local;
		cto->ct_syshandle = handle;
	}

	dmaresult = isp_pci_dmasetupt(isp, cso, (ispreq_t *) local);
	if (dmaresult == CMD_QUEUED) {
		isp->isp_nactive++;
		return;
	}
	if (dmaresult == CMD_EAGAIN) {
		cso->ccb_h.flags |= QSIO_HBA_REQUEUE;
	} else {
		cso->ccb_h.flags |= QSIO_HBA_ERROR;
	}
	isp_destroy_tgt_handle(isp, handle);
out:
	return;
}

void
tptr_free(ispsoftc_t *isp)
{
	tstate_t *tptr = isp->tptr;
	if (tptr)  {
		if (tptr->fcbridge)
			fcbridge_exit(tptr->fcbridge);
		if (tptr->tgt_cache)
			slab_cache_destroy(tptr->tgt_cache);
		free(tptr, M_DEVBUF);
		isp->tptr = NULL;
	}
}

int
tptr_alloc(ispsoftc_t *isp)
{
	tstate_t *tptr;

	tptr = malloc(sizeof(tstate_t), M_DEVBUF, M_NOWAIT | M_ZERO);
	if (!tptr) {
		return ENOMEM;
	}

	tptr->tgt_cache = slab_cache_create("tstate_scache", sizeof(struct tgtcmd), NULL, NULL, NULL, NULL, 0, 0);
	if (!tptr->tgt_cache) {
		free(tptr, M_DEVBUF);
		return ENOMEM;
	}

	tptr->fcbridge = fcbridge_new(isp, 0);
	if (!tptr->fcbridge) {
		slab_cache_destroy(tptr->tgt_cache);
		free(tptr, M_DEVBUF);
		return ENOMEM;
	}
	isp->tptr = tptr;
	return 0;
}


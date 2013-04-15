#include "ldev_bsd.h"
#include "exportdefs.h"
#include "missingdefs.h"
#include "devq.h"

struct qs_interface_cbs icbs;
static int ldev_new_device_cb(struct tdevice *newdevice);
static int ldev_remove_device_cb(struct tdevice *removedevice, int tid, void *hpriv);
static void ldev_disable_device_cb(struct tdevice *removedevice, int, void *);

struct qs_interface_cbs icbs = {
	.new_device = ldev_new_device_cb,
	.remove_device = ldev_remove_device_cb,
	.disable_device = ldev_disable_device_cb,
	.interface = TARGET_INT_LOCAL,
};


MALLOC_DEFINE(M_LDEV, "ldev", "QUADStor ldev allocs");


struct cdev *ldevdev;
static struct cdevsw ldevdev_csw = {
	.d_version = D_VERSION,
};

static void
ldev_poll(struct cam_sim *sim)
{

}

static void 
copy_out_request_buffer2(uint8_t *dataptr, int dxfer_len, struct ccb_scsiio *csio)
{
	int i;
	uint32_t offset = 0;
	struct iovec *iov;

	if (!dxfer_len)
		return;

	if (!csio->sglist_cnt) {
		memcpy(csio->data_ptr, dataptr, dxfer_len);
		return;
	}

	iov = &((struct iovec *)csio->data_ptr)[0];
	for (i = 0; i < csio->sglist_cnt; i++)
	{ 
		uint8_t *out_addr;
		int min;

		min = min_t(int, (dxfer_len - offset), iov->iov_len);
		out_addr = iov->iov_base;
		memcpy(out_addr, dataptr+offset, min); 
		offset += min; 
		iov++;
	}

	DEBUG_BUG_ON(offset != dxfer_len);
	return;
}

static void 
copy_out_request_buffer(struct pgdata **pglist, int pglist_cnt, struct ccb_scsiio *csio, uint32_t dxfer_len)
{
	int i;
	int offset = 0;
	int sgoffset;
	int pgoffset;
	int min_len;
	int j;

	if (!csio->sglist_cnt) {
		for (i = 0; i < pglist_cnt; i++) { 
			struct pgdata *pgtmp = pglist[i];

			min_len = min_t(int, (dxfer_len - offset), pgtmp->pg_len);
			memcpy(csio->data_ptr+offset, page_address(pgtmp->page) + pgtmp->pg_offset, min_len); 
			offset += min_len;
			if (offset == dxfer_len)
				break;
		}
		return;
	}

	i = 0;
	j = 0;
	sgoffset = 0;
	pgoffset = 0;

	while (i < pglist_cnt)
	{
		struct pgdata *pgtmp = pglist[i];
		uint8_t *out_addr;
		int out_avail;

		min_len = min_t(int, (dxfer_len - offset), pgtmp->pg_len); 

		DEBUG_BUG_ON(pgoffset > pgtmp->pg_len);
		out_avail = min_len - pgoffset;

		while (j < csio->sglist_cnt) {
			struct iovec *iov = &((struct iovec *)csio->data_ptr)[j];
			uint8_t *in_addr;
			int min;
			int in_avail;

			DEBUG_BUG_ON(sgoffset > iov->iov_len);
			in_avail = iov->iov_len - sgoffset;
			min = min_t(int, in_avail, out_avail);

			out_addr = page_address(pgtmp->page)+ pgtmp->pg_offset + pgoffset;

			in_addr = (uint8_t *)iov->iov_base+sgoffset;
			memcpy(in_addr, out_addr, min);

			sgoffset += min;
			pgoffset += min;
			out_avail -= min;
			offset += min;

			if (sgoffset == iov->iov_len) {
				sgoffset = 0;
				j++;
			}

 			if (offset == dxfer_len)
				return;

			if (pgoffset == pgtmp->pg_len) {
				pgoffset = 0;
				i++;
				break;
			}

			if (j == csio->sglist_cnt)
				return;
		}
	}
	return;
}

static void 
ldev_send_ccb(void *ccb_void)
{
	struct qsio_scsiio *ctio = (struct qsio_scsiio *)ccb_void;
	struct vhba_priv *vhba_priv = &ctio->ccb_h.priv.vpriv;
	struct ldev_bsd *ldev = vhba_priv->ldev;
	union ccb *ccb;
	struct ccb_scsiio *csio;
	struct ccb_list ctio_list;

	STAILQ_INIT(&ctio_list);
	(*icbs.device_remove_ctio)(ctio, &ctio_list);
	DEBUG_BUG_ON(!atomic_read(&ldev->pending_cmds));
	atomic_dec(&ldev->pending_cmds);

	ccb = vhba_priv->ccb;
	csio = &ccb->csio;

	if (ctio->scsi_status == SCSI_STATUS_CHECK_COND) {
		csio->sense_len = ctio->sense_len;
		memcpy(&csio->sense_data, ctio->sense_data, ctio->sense_len);
		csio->scsi_status = ctio->scsi_status;
		csio->resid = csio->dxfer_len - ctio->dxfer_len;
	}

	if (ctio->pglist_cnt > 0) {
		copy_out_request_buffer((struct pgdata **)ctio->data_ptr, ctio->pglist_cnt, csio, ctio->dxfer_len);
	}
	else if (ctio->dxfer_len > 0) {
		copy_out_request_buffer2(ctio->data_ptr, ctio->dxfer_len, csio);
	}

	csio->ccb_h.status = CAM_REQ_CMP;
	mtx_lock(&ldev->cam_mtx);
	xpt_done(ccb);
	mtx_unlock(&ldev->cam_mtx);
	(*icbs.ctio_free_all)(ctio);
	(*icbs.device_queue_ctio_list)(&ctio_list);
}

static void 
copy_in_request_buffer2(uint8_t *dataptr, int dxfer_len, struct ccb_scsiio *csio)
{
	int i;
	int offset;
	struct iovec *iov;

	if (!dxfer_len)
		return;

	if (csio->sglist_cnt == 0) {
		memcpy(dataptr, csio->data_ptr, dxfer_len);
		return;
	}

	offset = 0;
	iov = &((struct iovec *)csio->data_ptr)[0];
	for (i = 0; i < csio->sglist_cnt; i++) { 
		int min;

		min = min_t(int, dxfer_len - offset, iov->iov_len);
		memcpy(dataptr+offset, iov->iov_base, min); 
		offset += min;
		iov++;
	}
	return;
}

static void 
copy_in_request_buffer(struct pgdata **pglist, int pglist_cnt, struct ccb_scsiio *csio, int dxfer_len)
{
	int i;
	uint32_t offset = 0;
	uint32_t sgoffset;
	uint32_t pgoffset;
	int j;
	int min_len;

	if (!dxfer_len)
		return;

	if (csio->sglist_cnt == 0) {
		for (i = 0; i < pglist_cnt; i++) { 
			struct pgdata *pgtmp = pglist[i];
			min_len = min_t(int, pgtmp->pg_len, (dxfer_len - offset));
			memcpy(page_address(pgtmp->page), csio->data_ptr+offset, min_len); 
			offset += min_len;
		}
		return;
	}

	sgoffset = 0;
	pgoffset = 0;
	j = 0;
	i = 0;
	while (i < pglist_cnt) {
		struct pgdata *pgtmp = pglist[i];
		uint8_t *out_addr;
		int out_avail;

		out_avail = pgtmp->pg_len - pgoffset;

		while (j < csio->sglist_cnt) {
			struct iovec *iov = &((struct iovec *)csio->data_ptr)[j];
			uint8_t *in_addr;
			int min;
			int in_avail;

			in_avail = iov->iov_len - sgoffset;
			min = min_t(int, in_avail, out_avail);

			out_addr = page_address(pgtmp->page)+pgoffset;

			in_addr = (uint8_t *)iov->iov_base+sgoffset;
			memcpy(out_addr, in_addr, min);

			sgoffset += min;
			pgoffset += min;
			out_avail -= min;

			if (sgoffset == iov->iov_len) {
				sgoffset = 0;
				j++;
			}

			if (pgoffset == pgtmp->pg_len) {
				pgoffset = 0;
				i++;
				break;
			}

			if (j == csio->sglist_cnt)
				return;
		}
	}

	return;
}

static int
allocate_write_request(struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, struct ccb_scsiio *csio, int dxfer_len, struct tdevice *device)
{
	int retval;

	if (!num_blocks)
		return 0;

	retval = (*icbs.device_allocate_buffers)(ctio, block_size, num_blocks, Q_NOWAIT);
	if (unlikely(retval != 0)) {
		DEBUG_WARN_NEW("Cannot allocate buffers for %u blocks\n", num_blocks);
		return -1;
	}
	ctio->dxfer_len = dxfer_len;
	copy_in_request_buffer((struct pgdata **)ctio->data_ptr, ctio->pglist_cnt, csio, ctio->dxfer_len);
	return 0;
}

void
ldev_proc_cmd(struct qsio_scsiio *ctio)
{
	struct vhba_priv *vhba_priv = &ctio->ccb_h.priv.vpriv;
	union ccb *ccb = vhba_priv->ccb;
	struct ccb_scsiio *csio = &ccb->csio;
	struct ldev_bsd *ldev = vhba_priv->ldev;
	struct tdevice *device = ldev->device;

	switch(ctio->cdb[0]) {
	case WRITE_6:
	{
		int retval;
		uint32_t num_blocks = 0, block_size = 0;
		int dxfer_len;

		retval = (*icbs.ctio_write_length)(ctio, device, &block_size, &num_blocks, &dxfer_len);
		if (unlikely(retval != 0)) {
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMAND_PHASE_ERROR_ASC, COMMAND_PHASE_ERROR_ASCQ);
			goto err;
		}

		retval = allocate_write_request(ctio, block_size, num_blocks, csio, dxfer_len, device);
		if (retval != 0) {
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMAND_PHASE_ERROR_ASC, COMMAND_PHASE_ERROR_ASCQ);
			goto err;
		}
		break;
	}
	case MODE_SELECT:
	case MODE_SELECT_10:
	case SEND_DIAGNOSTIC:
	case PERSISTENT_RESERVE_OUT:
	{
		int retval;

		retval = (*icbs.device_allocate_cmd_buffers)(ctio, Q_NOWAIT);
		if (retval != 0) {
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMAND_PHASE_ERROR_ASC, COMMAND_PHASE_ERROR_ASCQ);
			goto err;
		}
		copy_in_request_buffer2(ctio->data_ptr, ctio->dxfer_len, csio);
		break;
	}
	default:
		break;
	}

	atomic_inc(&ldev->pending_cmds);
	(*icbs.device_queue_ctio)(device, ctio);
	return;
err:
	csio->sense_len = ctio->sense_len;
	memcpy(&csio->sense_data, ctio->sense_data, ctio->sense_len);
	csio->scsi_status = SCSI_STATUS_CHECK_COND;
	csio->ccb_h.status = CAM_REQ_CMP;
	mtx_lock(&ldev->cam_mtx);
	xpt_done(ccb);
	mtx_unlock(&ldev->cam_mtx);
	(*icbs.ctio_free_all)(ctio);
}

static struct qsio_scsiio *
construct_ctio(struct ldev_bsd *ldev, struct ccb_scsiio *csio)
{
	struct qsio_scsiio *ctio;
	struct vhba_priv *vhba_priv;
	int min_len;

	ctio = (*icbs.ctio_new)(Q_NOWAIT);
	if (unlikely(!ctio)) {
		DEBUG_WARN_NEW("Memory allocation failure\n");
		return NULL;
	}

	ctio->i_prt = LDEV_HOST_ID; /* Our ID */ 
	ctio->t_prt = 0;
	ctio->r_prt = LDEV_RPORT_START; 
	min_len = min_t(int, 16, csio->cdb_len);
	memcpy(ctio->cdb, csio->ccb_h.flags & CAM_CDB_POINTER ? csio->cdb_io.cdb_ptr : csio->cdb_io.cdb_bytes, min_len); 
	ctio->ccb_h.flags = QSIO_DIR_OUT;
	ctio->ccb_h.queue_fn = ldev_send_ccb;
	ctio->ccb_h.tdevice = ldev->device;
	ctio->task_attr = csio->tag_action;
	ctio->task_tag = csio->tag_id;

	vhba_priv = &ctio->ccb_h.priv.vpriv;
	vhba_priv->ldev = ldev;
	vhba_priv->ccb = (union ccb *)(csio);
	return ctio;
}

static void
xpt_scsi_io(struct ldev_bsd *ldev, struct cam_sim *sim, union ccb *ccb)
{
	struct ccb_scsiio *csio = &ccb->csio;
	struct qsio_scsiio *ctio;

	if (atomic_read(&ldev->disabled)) {
		csio->ccb_h.status = CAM_REQ_ABORTED;
		xpt_done(ccb);
		return;
	}

	ctio = construct_ctio(ldev, &ccb->csio);
	if (unlikely(!ctio)) {
		csio->ccb_h.status = CAM_REQ_ABORTED;
		xpt_done(ccb);
		return;
	}
	devq_insert_ccb(ldev->devq, (struct qsio_hdr *)ctio);
}

static void
xpt_transfer_settings(struct ldev_bsd *ldev, struct cam_sim *sim, union ccb *ccb)
{
	struct ccb_trans_settings_scsi *scsi;
	struct ccb_trans_settings *cts;

	cts = &ccb->cts;
	scsi = &cts->proto_specific.scsi;

	cts->protocol = PROTO_SCSI;
	cts->protocol_version = SCSI_REV_SPC3;
	cts->transport = XPORT_SPI;
	cts->transport_version = 3;

	scsi->valid = CTS_SCSI_VALID_TQ;
	ccb->ccb_h.status = CAM_REQ_CMP;
}

static void
xpt_inq(struct ldev_bsd *ldev, struct cam_sim *sim, union ccb *ccb)
{
	struct ccb_pathinq *cpi = &ccb->cpi;

	cpi->version_num = 1;
	cpi->target_sprt = 0;
	cpi->max_target = 1;
	cpi->max_lun = 1;
	cpi->bus_id = cam_sim_bus(sim);
	cpi->initiator_id = 7;
	cpi->hba_inquiry = PI_SDTR_ABLE | PI_TAG_ABLE | PI_WIDE_32;
	strcpy(cpi->sim_vid, "FreeBSD");
	strcpy(cpi->hba_vid, "QUADStor");
	cpi->bus_id = cam_sim_bus(sim);
	cpi->base_transfer_speed = 4000000;
	strncpy(cpi->dev_name, cam_sim_name(sim), DEV_IDLEN);
	cpi->unit_number = cam_sim_unit(sim);
	cpi->protocol = PROTO_SCSI;
	cpi->protocol_version = SCSI_REV_SPC3;
	cpi->transport = XPORT_SPI;
	cpi->transport_version = 3;
	cpi->ccb_h.status = CAM_REQ_CMP;
}
 
static void
ldev_action(struct cam_sim *sim, union ccb *ccb)
{
	struct ccb_hdr *ccb_h = &ccb->ccb_h;
	struct ldev_bsd *ldev = (struct ldev_bsd *)cam_sim_softc(sim);

	switch (ccb_h->func_code) {
		case XPT_PATH_INQ:
			xpt_inq(ldev, sim, ccb);
			break;
		case XPT_RESET_BUS:
		{
			struct ccb_pathinq *cpi = &ccb->cpi;
			cpi->ccb_h.status = CAM_REQ_CMP;
			break;
		}
		case XPT_SCSI_IO:
			if (ccb_h->target_id || ccb_h->target_lun) {
				ccb->ccb_h.status = CAM_DEV_NOT_THERE;
				break;
			}
			xpt_scsi_io(ldev, sim, ccb);
			return;
		case XPT_GET_TRAN_SETTINGS:
			xpt_transfer_settings(ldev, sim, ccb);
			break;
		default:
			ccb_h->status = CAM_REQ_INVALID;
			break;
	}
	xpt_done(ccb);
}

#define MAX_SIM_COMMANDS	256

static int
ldev_new_device_cb(struct tdevice *newdevice)
{
	struct ldev_bsd *ldev;
	struct cam_sim *sim = NULL;
	struct cam_devq *devq = NULL;
	struct cam_path *path = NULL;
	uint32_t tid;

	ldev = zalloc(sizeof(struct ldev_bsd), M_LDEV, M_WAITOK);
	spin_lock_initt(&ldev->cam_mtx, "ldev cam mtx");
	ldev->device = newdevice;

	tid = (*icbs.device_tid)(newdevice);
	ldev->devq = devq_init(tid, "ldev");
	if (!ldev->devq)
	{
		free(ldev, M_LDEV);
		return -1;
	}

	devq = cam_simq_alloc(MAX_SIM_COMMANDS);
	if (unlikely(!devq)) {
		DEBUG_WARN_NEW("CAM SIMQ alloc failed for %d transactions\n", 256);
		devq_exit(ldev->devq);
		free(ldev, M_LDEV);
		return -1;
	}

	sim = cam_sim_alloc(ldev_action, ldev_poll, "ldev", ldev, 0, &ldev->cam_mtx, MAX_SIM_COMMANDS, 0, devq);
	if (unlikely(!sim)) {
		DEBUG_WARN_NEW("CAM SIM Alloc failed\n");
		cam_simq_free(devq);
		devq_exit(ldev->devq);
		free(ldev, M_LDEV);
		return -1;
	}

	spin_lock(&ldev->cam_mtx);
	if (xpt_bus_register(sim, NULL, 0) != CAM_SUCCESS) {
		DEBUG_WARN_NEW("Failed to register bus\n");
		goto err;
	}

	if (xpt_create_path(&path, xpt_periph, cam_sim_path(sim), CAM_TARGET_WILDCARD, CAM_LUN_WILDCARD) != CAM_REQ_CMP) {
		xpt_bus_deregister(cam_sim_path(sim));
		DEBUG_WARN_NEW("Failed to create xpt path\n");
		goto err;
	}

	spin_unlock(&ldev->cam_mtx);
	ldev->sim = sim;
	ldev->path = path;
	(*icbs.device_set_hpriv)(newdevice, ldev);
	return 0;
err:
	cam_sim_free(sim, 1);
	spin_unlock(&ldev->cam_mtx);
	devq_exit(ldev->devq);
	free(ldev, M_LDEV);
	return -1;
}

static void
ldev_disable_device_cb(struct tdevice *removedevice, int tid, void *hpriv)
{
	struct ldev_bsd *ldev = hpriv;

	if (unlikely(!ldev))
		return;

	atomic_set(&ldev->disabled, 1);
}

static int
ldev_remove_device_cb(struct tdevice *removedevice, int tid, void *hpriv)
{
	struct ldev_bsd *ldev = hpriv;

	if (!ldev)
		return 0;

	atomic_set(&ldev->disabled, 1);
	while (atomic_read(&ldev->pending_cmds) > 0)
		pause("ldev_remove", 1000);

	spin_lock(&ldev->cam_mtx);
	xpt_async(AC_LOST_DEVICE, ldev->path, NULL);
	xpt_free_path(ldev->path);

	xpt_bus_deregister(cam_sim_path(ldev->sim));
	cam_sim_free(ldev->sim, 1);
	spin_unlock(&ldev->cam_mtx);
	devq_exit(ldev->devq);
	free(ldev, M_LDEV);
	return 0;
}

static void
ldev_exit(void)
{
	device_unregister_interface(&icbs);
	destroy_dev(ldevdev);
}

static int
ldev_init(void)
{
	ldevdev = make_dev(&ldevdev_csw, 0, UID_ROOT, GID_WHEEL, 0550, "ldevdev");
	device_register_interface(&icbs);
	return 0;
}

static int
event_handler(struct module *module, int event, void *arg) {
	int retval = 0;
	switch (event) {
		case MOD_LOAD:
			retval = ldev_init();
			break;
		case MOD_UNLOAD:
			ldev_exit();
			break;
		default:
			retval = EOPNOTSUPP;
			break;
	}
        return retval;
}

static moduledata_t ldevmod_info = {
    "ldevmod",    /* module name */
     event_handler,  /* event handler */
     NULL            /* extra data */
};

DECLARE_MODULE(ldevmod, ldevmod_info, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_DEPEND(ldevmod, tldev, 1, 1, 2);

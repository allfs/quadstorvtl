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

#include "qla_sc.h"
#include "fcq.h"
#include <commondefs.h>

/* Commands alloced for queueing */
atomic_t alloced_cmds;
TAILQ_HEAD(,fcbridge) fcbridge_list = TAILQ_HEAD_INITIALIZER(fcbridge_list);

struct device_info {
	struct tdevice *device;
	atomic_t refs;
	int disabled;
};

struct device_info *device_list[MAX_DINFO_DEVICES];

#ifdef FREEBSD 
MALLOC_DEFINE(M_QISP, "QISP", "QUADStor ISP Targ");
static struct mtx qs_device_lock;
static struct sx itf_lock;
MTX_SYSINIT(qs_device_lock, &qs_device_lock, "fcdevice lock", MTX_DEF);
SX_SYSINIT(itf_lock, &itf_lock, "fcitf lock");
wait_chan_t alloced_cmds_wait;

static void
fccommon_sysinit(void)
{
	wait_chan_init(&alloced_cmds_wait, "alloced cmds wait");
}

SYSINIT(alloced_cmds_sysinit, SI_SUB_LOCK, SI_ORDER_MIDDLE, fccommon_sysinit, NULL);
#else
DEFINE_SPINLOCK(qs_device_lock);
DEFINE_MUTEX(itf_lock);
DECLARE_WAIT_QUEUE_HEAD(alloced_cmds_wait);
#undef free
#define free(ptr,type)	kfree(ptr)
#endif

static int fcbridge_new_device_cb(struct tdevice *device);
static int fcbridge_remove_device_cb(struct tdevice *device, int tid, void *hpriv);
static void fcbridge_disable_device_cb(struct tdevice *device, int tid, void *hpriv);
static void fcbridge_attach_interface(void);
static void fcbridge_detach_interface(void);
struct qs_interface_cbs icbs = {
	.new_device = fcbridge_new_device_cb,
	.remove_device = fcbridge_remove_device_cb,
	.disable_device = fcbridge_disable_device_cb,
	.detach_interface = fcbridge_detach_interface,
	.ctio_exec = fcbridge_route_cmd_post,
	.interface = TARGET_INT_FC,
};

static int
initiator_valid(uint64_t i_prt[])
{
	struct fcbridge *fcbridge;
	int valid = 1, retval;

	mtx_lock(&qs_device_lock);
	TAILQ_FOREACH(fcbridge, &fcbridge_list, b_list) {
		retval = fcbridge_i_prt_valid(fcbridge, i_prt);
		if (!retval) {
			valid = 0;
			break;
		}
	}
	mtx_unlock(&qs_device_lock);
	return valid;
}

static void
dinfo_get(struct device_info *dinfo)
{
	atomic_inc(&dinfo->refs);
}

static void
dinfo_put(struct device_info *dinfo)
{
	DEBUG_BUG_ON(!atomic_read(&dinfo->refs));
	atomic_dec(&dinfo->refs);
}

static struct device_info *
fcbridge_locate_dinfo(int bus)
{
	struct device_info *dinfo, *ret = NULL;

	if (unlikely(bus >= MAX_DINFO_DEVICES))
		return NULL;

	mtx_lock(&qs_device_lock);
	dinfo = device_list[bus];
	if (dinfo && !dinfo->disabled) {
		dinfo_get(dinfo);
		ret = dinfo;
	}
	mtx_unlock(&qs_device_lock);
	return ret;
}

static void 
fcbridge_disable_device_cb(struct tdevice *device, int tid, void *hpriv)
{
	struct device_info *dinfo;
	uint32_t base_id;

	base_id = (*icbs.device_tid)(device) - 1;
	mtx_lock(&qs_device_lock);
	dinfo = device_list[base_id];
	if (!dinfo) {
		mtx_unlock(&qs_device_lock);
		return;
	}
	dinfo->disabled = 1;
	mtx_unlock(&qs_device_lock);
}

static int
fcbridge_remove_device_cb(struct tdevice *device, int tid, void *hpriv)
{
	struct device_info *dinfo;
	uint32_t base_id;

	base_id = (*icbs.device_tid)(device) - 1;
	mtx_lock(&qs_device_lock);
	dinfo = device_list[base_id];
	if (!dinfo) {
		mtx_unlock(&qs_device_lock);
		return -1;
	}
	device_list[base_id] = NULL;
	dinfo->disabled = 1;
	mtx_unlock(&qs_device_lock);
	while (atomic_read(&dinfo->refs) > 1)
		pause("psg", 10);
	free(dinfo, M_QISP);
	return 0;
}

static int
fcbridge_new_device_cb(struct tdevice *device)
{
	struct device_info *dinfo, *prev;
	uint32_t base_id;

	base_id = (*icbs.device_tid)(device) - 1;

	dinfo = zalloc(sizeof(struct device_info), M_QISP, M_WAITOK);
	dinfo->device = device;
	atomic_set(&dinfo->refs, 1);
	DEBUG_BUG_ON(device_list[base_id]);
	mtx_lock(&qs_device_lock);
	prev = device_list[base_id];
	if (prev && prev->device == device) {
		mtx_unlock(&qs_device_lock);
		free(dinfo, M_QISP);
		return 0;
	}
	else if (prev) {
		prev->disabled = 1;
		mtx_unlock(&qs_device_lock);
		free(dinfo, M_QISP);
		return -1;
	}
	device_list[base_id] = dinfo;
	mtx_unlock(&qs_device_lock);
	return 0;
}

static inline void
fcbridge_init_unit_identifier(struct fcbridge *fcbridge, struct logical_unit_identifier *unit_identifier)
{
	uint64_t wwpn[2];

	fcbridge_get_tport(fcbridge, wwpn);
	unit_identifier->code_set = 0x02; /*logical unit idenifier */
	unit_identifier->identifier_type = UNIT_IDENTIFIER_T10_VENDOR_ID;
	memset(unit_identifier->vendor_id, ' ', 8);
	memcpy(unit_identifier->vendor_id, VENDOR_ID_QUADSTOR, strlen(VENDOR_ID_QUADSTOR));
	memset(unit_identifier->product_id, ' ', 16);
	memcpy(unit_identifier->product_id, PRODUCT_ID_QUADSTOR_FCBRIDGE, strlen(PRODUCT_ID_QUADSTOR_FCBRIDGE));
	unit_identifier->identifier_length = offsetof(struct logical_unit_identifier, serial_number) - offsetof(struct logical_unit_identifier, vendor_id);
	sprintf(unit_identifier->serial_number, "%08llX%08llX", (unsigned long long)wwpn[1], (unsigned long long)wwpn[0]);
	unit_identifier->identifier_length += strlen(unit_identifier->serial_number);
}

static void
fcbridge_init_inquiry_data(struct fcbridge *fcbridge, struct inquiry_data *inquiry)
{
	memset(inquiry, 0, sizeof(struct inquiry_data));
	inquiry->device_type = T_PROCESSOR;
	inquiry->version = ANSI_VERSION_SCSI3_SPC3;
	inquiry->response_data = RESPONSE_DATA;
	inquiry->additional_length = STANDARD_INQUIRY_LEN_SPC3 - 5;
	memset(&inquiry->vendor_id, ' ', 8);
	memcpy(&inquiry->vendor_id, VENDOR_ID_QUADSTOR, strlen(VENDOR_ID_QUADSTOR));
	memset(&inquiry->product_id, ' ', 16);
	memcpy(&inquiry->product_id, PRODUCT_ID_QUADSTOR_FCBRIDGE, strlen(PRODUCT_ID_QUADSTOR_FCBRIDGE));
	memcpy(&inquiry->revision_level, PRODUCT_REVISION_QUADSTOR, strlen(PRODUCT_REVISION_QUADSTOR));
}

static void
fcbridge_free(struct fcbridge *fcbridge)
{
	fcq_exit(fcbridge->fcq);
	free(fcbridge, M_QISP);
}

void
fcbridge_exit(struct fcbridge *fcbridge)
{
	mtx_lock(&qs_device_lock);
	TAILQ_REMOVE(&fcbridge_list, fcbridge, b_list);
	mtx_unlock(&qs_device_lock);
	fcbridge_free(fcbridge);
}

struct fcbridge *
fcbridge_new(void *ha, uint32_t id)
{
	struct fcbridge *fcbridge;

	fcbridge = (struct fcbridge *)zalloc(sizeof(struct fcbridge), M_QISP, M_NOWAIT);
	if (unlikely(!fcbridge))
	{
		DEBUG_WARN_NEW("Cannot allocate for a new fcbridge\n");
		return NULL;
	}

	fcbridge->ha = ha;
	fcbridge->id = id;
	fcq_init(fcbridge);
	if (unlikely(!fcbridge->fcq))
	{
		DEBUG_WARN_NEW("Cannot init cmd queue\n");
		free(fcbridge, M_QISP);
		return NULL;
	}

	mtx_lock(&qs_device_lock);
	TAILQ_INSERT_TAIL(&fcbridge_list, fcbridge, b_list);
	mtx_unlock(&qs_device_lock);
	return fcbridge;
}

void
fcbridge_route_cmd_post(struct qsio_scsiio *ctio)
{
	struct tdevice *device = ctio->ccb_h.tdevice;
	uint32_t block_size, num_blocks;
	int retval, dxfer_len;

	switch (ctio->cdb[0]) {
	case WRITE_6:
		if (!(ctio->ccb_h.flags & QSIO_DIR_OUT))
			break;

		num_blocks = 0;
		retval = (*icbs.ctio_write_length)(ctio, device, &block_size, &num_blocks, &dxfer_len);
		if (unlikely(retval != 0)) {
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMAND_PHASE_ERROR_ASC, COMMAND_PHASE_ERROR_ASCQ);
			(*icbs.device_send_ccb)(ctio);
			return;
		}

		if (!num_blocks)
			break;

		retval = (*icbs.device_allocate_buffers)(ctio, block_size, num_blocks, Q_NOWAIT);
		if (unlikely(retval != 0)) {
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMAND_PHASE_ERROR_ASC, COMMAND_PHASE_ERROR_ASCQ);
			(*icbs.device_send_ccb)(ctio);
			return;
		}
		ctio->dxfer_len = dxfer_len;

		(*ctio->ccb_h.queue_fn)(ctio);
		return;
	case MODE_SELECT_6:
	case MODE_SELECT_10:
	case WRITE_ATTRIBUTE:
	case SEND_DIAGNOSTIC:
	case PERSISTENT_RESERVE_OUT:
		if (!(ctio->ccb_h.flags & QSIO_DIR_OUT))
			break;

		retval = (*icbs.device_allocate_cmd_buffers)(ctio, Q_NOWAIT);
		if (unlikely(retval != 0)) {
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMAND_PHASE_ERROR_ASC, COMMAND_PHASE_ERROR_ASCQ);
			(*icbs.device_send_ccb)(ctio);
			return;
		}

		if (!ctio->dxfer_len)
			break;

		(*ctio->ccb_h.queue_fn)(ctio);
		return;
	default:
		break;
	}

	(*icbs.device_queue_ctio_direct)((struct qsio_hdr *)ctio);
	return;
}

static inline void 
__device_send_ccb(struct qsio_scsiio *ctio)
{
	struct qsio_hdr *ccb_h;
	/* Basically call the *_send_ccb function for the ccb */

	ccb_h = &ctio->ccb_h;
	ctio->ccb_h.flags = QSIO_DIR_IN | QSIO_SEND_STATUS | QSIO_TYPE_CTIO;
	if (ctio->dxfer_len)
		ctio->ccb_h.flags |= QSIO_DATA_DIR_IN;
 
	(*ccb_h->queue_fn)(ctio);
}

static int
fcbridge_route_cmd(struct fcbridge *fcbridge, struct qsio_scsiio *ctio)
{
	uint64_t lun = ctio->ccb_h.target_lun;
	struct tdevice *device;
	struct device_info *dinfo;
	uint16_t bus = (*icbs.bus_from_lun)(lun);
	int exec;

	dinfo = fcbridge_locate_dinfo(bus);
	if (unlikely(!dinfo))
	{
		__ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, LOGICAL_UNIT_NOT_SUPPORTED_ASC, LOGICAL_UNIT_NOT_SUPPORTED_ASCQ);
		(*icbs.device_send_ccb)(ctio);
		return 0;
	}
	device = dinfo->device;

	ctio->ccb_h.tdevice = device;

	exec = (*icbs.device_istate_queue_ctio)(device, ctio);
	if (exec < 0) {
		dinfo_put(dinfo);
		__ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, LOGICAL_UNIT_NOT_SUPPORTED_ASC, LOGICAL_UNIT_NOT_SUPPORTED_ASCQ);
		__device_send_ccb(ctio);
		return 0;
	}

	if (exec)
		fcbridge_route_cmd_post(ctio);
	dinfo_put(dinfo);
	return 0;
}

static int
fcbridge_device_identification(struct fcbridge *fcbridge, uint8_t *buffer, int length)
{
	struct device_identification_page *page = (struct device_identification_page *)buffer;
	struct logical_unit_identifier unit_identifier, *ptr_identifier;
	uint32_t page_length = 0;
	int done = 0;
	uint8_t idlength;

	if (unlikely(length < sizeof(struct vital_product_page)))
	{
		return -1;
	}

	page->device_type = T_PROCESSOR;
	page->page_code = DEVICE_IDENTIFICATION_PAGE;

	done += sizeof(struct device_identification_page);

	fcbridge_init_unit_identifier(fcbridge, &unit_identifier);

	idlength = unit_identifier.identifier_length + sizeof(struct device_identifier);
	if (done + idlength > length)
	{
		goto out;
	}

	ptr_identifier = (struct logical_unit_identifier *)(buffer+done);
	memcpy(ptr_identifier, &unit_identifier, sizeof(struct logical_unit_identifier)); 
	page_length += idlength;
	done += idlength;

out:
	page->page_length = page_length;
	return done;
}

static int
fcbridge_serial_number(struct fcbridge *fcbridge, uint8_t *buffer, int length)
{
	struct serial_number_page *page = (struct serial_number_page *) buffer;
	uint8_t serial_number[32];
	uint64_t wwpn[2];
	int min_len;

	if (unlikely(length < sizeof(struct vital_product_page)))
	{
		return -1;
	}

	fcbridge_get_tport(fcbridge, wwpn);
	sprintf(serial_number, "%08llX%08llX", (unsigned long long)wwpn[1], (unsigned long long)wwpn[0]);

	memset(page, 0, sizeof(struct vital_product_page));
	page->device_type = T_PROCESSOR; /* peripheral qualifier */
	page->page_code = UNIT_SERIAL_NUMBER_PAGE;
	page->page_length =  strlen(serial_number);

	min_len = min_t(int, strlen(serial_number), (length - sizeof(struct vital_product_page)));
	if (min_len) {
		memcpy(page->serial_number, serial_number, min_len);
	}

	return (min_len + sizeof(struct vital_product_page));
}

static int 
fcbridge_copy_vital_product_page_info(struct fcbridge *fcbridge, uint8_t *buffer, int allocation_length)
{
	struct vital_product_page tmp;
	struct vital_product_page *page = (struct vital_product_page *)buffer;
	int min_len;
	int i;
	int offset;
	struct evpd_page_info evpd_info;

	evpd_info.num_pages = 0x03; /* Five pages supported */
	evpd_info.page_code[0] = VITAL_PRODUCT_DATA_PAGE;
	evpd_info.page_code[1] = DEVICE_IDENTIFICATION_PAGE;
	evpd_info.page_code[2] = UNIT_SERIAL_NUMBER_PAGE;

	memset(&tmp, 0, sizeof(struct vital_product_page));
	tmp.device_type = T_DIRECT;
	tmp.page_code = 0x00;
	tmp.page_length = evpd_info.num_pages;

	min_len = min_t(int, allocation_length, sizeof(tmp));
	memcpy(page, &tmp, min_len);
	if (min_len < sizeof(tmp))
		return min_len;

	offset = min_len;
	for (i = 0; i < evpd_info.num_pages; i++) {
		if (offset == allocation_length)
			break;
		page->page_type[i] = evpd_info.page_code[i];
		offset++;
	}

	return offset;
}

static int
fcbridge_evpd_inquiry_data(struct fcbridge *fcbridge, struct qsio_scsiio *ctio, uint8_t page_code, int allocation_length)
{
	int retval;

	ctio->data_ptr = malloc(allocation_length, M_DEVBUF, M_NOWAIT);
	if (unlikely(!ctio->data_ptr)) {
		DEBUG_WARN_NEW("Cannot allocate for %d\n", allocation_length);
		return -1;
	}
	bzero(ctio->data_ptr, allocation_length);

	switch (page_code)
	{
		case UNIT_SERIAL_NUMBER_PAGE:
			retval = fcbridge_serial_number(fcbridge, ctio->data_ptr, allocation_length);
			if (unlikely(retval < 0))
			{
				goto err;
			}

			ctio->dxfer_len = retval;
			break;
		case DEVICE_IDENTIFICATION_PAGE:
			retval = fcbridge_device_identification(fcbridge, ctio->data_ptr, allocation_length);
			if (unlikely(retval < 0))
			{
				goto err;
			}

			ctio->dxfer_len = retval;
			break;
		case VITAL_PRODUCT_DATA_PAGE:
			retval = fcbridge_copy_vital_product_page_info(fcbridge, ctio->data_ptr, allocation_length);
			if (unlikely(retval < 0))
			{
				goto err;
			}

			ctio->dxfer_len = retval;
			break;
		default:
			DEBUG_WARN_NEW("Unhandled page code for EVPD inquiry %x\n", page_code);
			goto err;
	}
	return 0;
err:
	__ctio_free_data(ctio);
	ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);
	return 0;
}

static int
fcbridge_standard_inquiry_data(struct fcbridge *fcbridge, struct qsio_scsiio *ctio, int allocation_length)
{
	struct inquiry_data inquiry;
	int min_len;

	fcbridge_init_inquiry_data(fcbridge, &inquiry);

	min_len = min_t(int, allocation_length, sizeof(inquiry));
	ctio->data_ptr = malloc(min_len, M_DEVBUF, M_NOWAIT);
	if (unlikely(!ctio->data_ptr)) {
		DEBUG_WARN_NEW("Cannot allocate for %d\n", allocation_length);
		return -1;
	}
	ctio->dxfer_len = min_len;

	memset(ctio->data_ptr, 0, min_len);
	memcpy(ctio->data_ptr, &inquiry, min_len);
	return 0;
}

static int
fcbridge_cmd_test_unit_ready(struct fcbridge *fcbridge, struct qsio_scsiio *ctio)
{
	return 0;
}

static void
fcbridge_check_interface(void)
{
	int i;
	struct device_info *dinfo;
	struct tdevice *device;

	sx_xlock(&itf_lock);
	if (atomic_read(&icbs.itf_enabled)) {
		sx_xunlock(&itf_lock);
		return;
	}

	fcbridge_attach_interface();

	if (!atomic_read(&icbs.itf_enabled)) {
		sx_xunlock(&itf_lock);
		return;
	}

	for (i = 0; i < MAX_DINFO_DEVICES; i++) {
		dinfo = device_list[i];

		if (dinfo)
			continue;

		device = (*icbs.get_device)(i);
		if (!device)
			continue;
		fcbridge_new_device_cb(device);
	}
	sx_xunlock(&itf_lock);
}

static int
fcbridge_cmd_report_luns(struct fcbridge *fcbridge, struct qsio_scsiio *ctio)
{
	uint32_t length;
	int i;
	uint8_t *ptr;
	struct device_info *dinfo;
	uint8_t *cdb = ctio->cdb;
	uint32_t allocation_length;
	int done = 0;
	int avail = 0;
	int retval;

	allocation_length = be32toh(*((uint32_t *)(&cdb[6])));
	if (allocation_length < 16) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	length = ((MAX_DINFO_DEVICES + 1) * 8) + 16; /* 8 bytes for the header, 8 for lun 0 */
	if (length > allocation_length)
	{
		length = allocation_length;
	}

	ctio->data_ptr = malloc(length, M_DEVBUF, M_NOWAIT);
	if (unlikely(!ctio->data_ptr)) {
		DEBUG_WARN_NEW("Cannot allocate for size %d\n", length);
		return -1;
	}

	ptr = (uint8_t *)(ctio->data_ptr);
	memset(ptr, 0, 16);
	done += 16;
	ptr += 16;
	avail = done;

	sx_xlock(&itf_lock);
	if (!atomic_read(&icbs.itf_enabled))
		goto skip_luns;

	for (i = 0; i < MAX_DINFO_DEVICES; i++) {
		dinfo = fcbridge_locate_dinfo(i);

		if (!dinfo)
			continue;

		retval = (*icbs.fc_initiator_check)(ctio->i_prt, dinfo->device);
		if (retval != 0) {
			dinfo_put(dinfo);
			continue;
		}
 
		if ((done + 8) <= length) {
			(*icbs.write_lun)(dinfo->device, ptr);
			ptr += 8;
			done += 8;
		}
		dinfo_put(dinfo);
		avail += 8;
	}
skip_luns:
	sx_xunlock(&itf_lock);
	ptr = (uint8_t *)(ctio->data_ptr);
	*((uint32_t *)ptr) = htobe32((avail - 8));
	ctio->dxfer_len = done;
	return 0;
}

static int
fcbridge_cmd_inquiry(struct fcbridge *fcbridge, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t evpd, page_code;
	uint16_t allocation_length;

	evpd = (cdb[1]) & 0x1;

	page_code = cdb[2];
	allocation_length = be16toh(*(uint16_t *)(&cdb[3]));

	if (!evpd)
		return fcbridge_standard_inquiry_data(fcbridge, ctio, allocation_length);
	else
		return fcbridge_evpd_inquiry_data(fcbridge, ctio, page_code, allocation_length);
}

static void
fcbridge_target_task_abort(struct fcbridge *fcbridge, struct qsio_immed_notify *notify)
{
	uint64_t lun = notify->ccb_h.target_lun;
	struct tdevice *device;
	struct device_info *dinfo;
	uint16_t bus = (*icbs.bus_from_lun)(lun);
	int task_found;

	DEBUG_INFO("task abort for lun %llu tag %x\n", (unsigned long long)lun, notify->task_tag);
	if (!lun || lun == 0xffff) {
		task_found = (*icbs.device_istate_abort_task)(NULL, notify->i_prt, notify->t_prt, TARGET_INT_FC, notify->task_tag);
		DEBUG_INFO("task found %d\n", task_found);
		if (!task_found)
			notify->notify_status = FC_TM_FAILED;
		return;
	}

	dinfo = fcbridge_locate_dinfo(bus);
	if (unlikely(!dinfo)) {
		notify->notify_status = FC_TM_FAILED;
		return;
	}

	device = dinfo->device;

	task_found = (*icbs.device_istate_abort_task)(device, notify->i_prt, notify->t_prt, TARGET_INT_FC, notify->task_tag);
	dinfo_put(dinfo);

	if (!task_found)
		notify->notify_status = FC_TM_FAILED;
	DEBUG_INFO("task found %d dinfo %p\n", task_found, dinfo);
}

static void
fcbridge_target_task_set_abort(struct fcbridge *fcbridge, struct qsio_immed_notify *notify)
{
	uint64_t lun = notify->ccb_h.target_lun;
	struct tdevice *device;
	struct device_info *dinfo;
	uint16_t bus = (*icbs.bus_from_lun)(lun);

	DEBUG_INFO("task set abort for lun %llu\n", (unsigned long long)lun);
	if (!lun) {
		(*icbs.device_istate_abort_task_set)(NULL, notify->i_prt, notify->t_prt, TARGET_INT_FC);
		return;
	}

	dinfo = fcbridge_locate_dinfo(bus);
	if (unlikely(!dinfo)) {
		notify->notify_status = FC_TM_FAILED;
		return;
	}

	device = dinfo->device;

	(*icbs.device_istate_abort_task_set)(device, notify->i_prt, notify->t_prt, TARGET_INT_FC);
	dinfo_put(dinfo);
	return;
}

static void 
fcbridge_target_reset(struct fcbridge *fcbridge, struct qsio_immed_notify *notify)
{
	uint64_t lun = notify->ccb_h.target_lun;
	struct tdevice *device;
	struct device_info *dinfo;
	uint16_t bus = (*icbs.bus_from_lun)(lun);

	dinfo = fcbridge_locate_dinfo(bus);
	if (unlikely(!dinfo)) {
		notify->notify_status = FC_TM_FAILED;
		return;
	}

	device = dinfo->device;
	(*icbs.device_target_reset)(device, notify->i_prt, notify->t_prt, TARGET_INT_FC);
	dinfo_put(dinfo);
}

int
fcbridge_task_mgmt(struct fcbridge *fcbridge, struct qsio_immed_notify *notify)
{
	sx_xlock(&itf_lock);

	if (!atomic_read(&icbs.itf_enabled))
		goto skip;

	DEBUG_INFO("notify fn %x lun %u\n", notify->fn, notify->ccb_h.target_lun);
	switch (notify->fn)
	{
		case LOGICAL_UNIT_RESET:
		case TARGET_RESET:
			if (!notify->ccb_h.target_lun)
				break;
			fcbridge_target_reset(fcbridge, notify);
			break;
		case ABORT_TASK_SET:
			fcbridge_target_task_set_abort(fcbridge, notify);
			break;
		case ABORT_TASK:
			fcbridge_target_task_abort(fcbridge, notify);
			break;
		case CLEAR_TASK_SET:
			notify->notify_status = FC_TM_FAILED;
			break;
		default:
			break;
	}

skip:
	notify->ccb_h.flags = QSIO_SEND_STATUS | QSIO_TYPE_NOTIFY;
	(*notify->ccb_h.queue_fn)(notify);
	sx_xunlock(&itf_lock);
	return 0;
}

int
fcbridge_proc_cmd(void *bridge, void *iop)
{
	struct fcbridge *fcbridge = (struct fcbridge *)(bridge);
	struct qsio_scsiio *ctio = (struct qsio_scsiio *)(iop);
	uint64_t lun = (ctio->ccb_h.target_lun);
	uint8_t *cdb;
	int retval;

	cdb = ctio->cdb;

	switch (cdb[0]) {
	case INQUIRY:
	case TEST_UNIT_READY:
	case REPORT_LUNS:
		retval = initiator_valid(ctio->i_prt);
		if (!retval) {
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, LOGICAL_UNIT_NOT_SUPPORTED_ASC, LOGICAL_UNIT_NOT_SUPPORTED_ASCQ);
			__device_send_ccb(ctio);
			return 0;
		}
	default:
		break;
	}

	if (lun) {
		if (atomic_read(&icbs.itf_enabled) && !(ctio_cmd(ctio)->local_pool))
		{
			return fcbridge_route_cmd(fcbridge, ctio);
		}
		else
		{
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, LOGICAL_UNIT_NOT_SUPPORTED_ASC, LOGICAL_UNIT_NOT_SUPPORTED_ASCQ);
			__device_send_ccb(ctio);
			return 0;
		}
	}

	retval = 0;

	if (!atomic_read(&icbs.itf_enabled))
		fcbridge_check_interface();

	switch (cdb[0]) {
	case INQUIRY:
		retval = fcbridge_cmd_inquiry(fcbridge, ctio);
		break;
	case TEST_UNIT_READY:
		retval = fcbridge_cmd_test_unit_ready(fcbridge, ctio);
		break;
	case REPORT_LUNS:
		retval = fcbridge_cmd_report_luns(fcbridge, ctio);
		break;
	default:
		__ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_COMMAND_OPERATION_CODE_ASC, INVALID_COMMAND_OPERATION_CODE_ASCQ);
		break;
	}

	if (unlikely(retval != 0)) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_HARDWARE_ERROR, 0, INTERNAL_TARGET_FAILURE_ASC, INTERNAL_TARGET_FAILURE_ASCQ);
	}

	__device_send_ccb(ctio);
	return 0;
}

#ifdef FREEBSD

static int
linker_detach(linker_file_t lf, void *arg)
{
	void (*device_unregister_interface)(struct qs_interface_cbs *);

	if (strcmp(lf->filename, "vtlcore.ko"))
		return 0;

	device_unregister_interface = (void *)linker_file_lookup_symbol(lf, "device_unregister_interface", 0);
	if (!device_unregister_interface) {
		DEBUG_WARN_NEW("failed to get device_unregister_interface symbol\n");
		return 1;
	}

	(*device_unregister_interface)(&icbs);
	return 1;
}

void
fcbridge_detach_interface(void)
{
	sx_xlock(&itf_lock);
	if (!atomic_read(&icbs.itf_enabled)) {
		sx_xunlock(&itf_lock);
		return;
	}

	atomic_set(&icbs.itf_enabled, 0);

	while (atomic_read(&alloced_cmds)) {
		sx_xunlock(&itf_lock);
		wait_on_chan(alloced_cmds_wait, !atomic_read(&alloced_cmds));
		sx_xlock(&itf_lock);
	}

	linker_file_foreach(linker_detach, NULL);
	sx_xunlock(&itf_lock);
}

static int
linker_attach(linker_file_t lf, void *arg)
{
	int (*device_register_interface)(struct qs_interface_cbs *);
	int retval;

	if (strcmp(lf->filename, "vtlcore.ko"))
		return 0;

	device_register_interface = (void *)linker_file_lookup_symbol(lf, "device_register_interface", 0);
	if (!device_register_interface) {
		DEBUG_WARN_NEW("device_register_interface symbol missing from core mod\n");
		return 1;
	}

	retval = (*device_register_interface)(&icbs);
	if (retval == 0)
		atomic_set(&icbs.itf_enabled, 1);
	return 1;
}

void
fcbridge_attach_interface(void)
{
	if (atomic_read(&icbs.itf_enabled))
		return;

	linker_file_foreach(linker_attach, NULL);
}
#else

void
fcbridge_detach_interface(void)
{
	void (*unregister_interface)(struct qs_interface_cbs *);

	sx_xlock(&itf_lock);
	if (!atomic_read(&icbs.itf_enabled)) {
		sx_xunlock(&itf_lock);
		return;
	}

	atomic_set(&icbs.itf_enabled, 0);
	while(atomic_read(&alloced_cmds)) {
		sx_xunlock(&itf_lock);
		wait_on_chan(alloced_cmds_wait, !atomic_read(&alloced_cmds));
		sx_xlock(&itf_lock);
	}

	unregister_interface = (void *)symbol_get(device_unregister_interface);
	if (!unregister_interface) {
		DEBUG_WARN_NEW("failed to get device_unregister_interface symbol\n");
		sx_xunlock(&itf_lock);
		return;
	}

	(*unregister_interface)(&icbs);
	symbol_put_addr(unregister_interface);
	sx_xunlock(&itf_lock);
	module_put(THIS_MODULE);
}

void
fcbridge_attach_interface(void)
{
	int retval;
	int (*register_interface)(struct qs_interface_cbs *);

	if (atomic_read(&icbs.itf_enabled))
		return;

	register_interface = (void *)symbol_get(device_register_interface);
	if (!register_interface) {
		return;
	}

	if (!try_module_get(THIS_MODULE)) {
		DEBUG_WARN_NEW("Failed to increment our reference\n");
		symbol_put_addr(register_interface);
		return;
	}

	retval = (*register_interface)(&icbs);
	symbol_put_addr(register_interface);
	if (retval == 0) {
		atomic_set(&icbs.itf_enabled, 1);
	}
	else {
		module_put(THIS_MODULE);
	}
}
#endif

int
__ctio_queue_cmd(struct qsio_scsiio *ctio)
{
	struct fcbridge *fcbridge;
	tgtcmd_t *cmd = ctio_cmd(ctio);

	fcbridge = ctio_fcbridge(ctio);
	DEBUG_BUG_ON(!fcbridge);
	fcq_insert_cmd(fcbridge, (void *)cmd);
	return 0;
}

struct qsio_scsiio *
__local_ctio_new(allocflags_t flags)
{
	struct qsio_scsiio *ctio;

	ctio = zalloc(sizeof(struct qsio_scsiio), M_QISP, flags);
	return ctio;
}

void
__local_ctio_free_all(struct qsio_scsiio *ctio)
{
	DEBUG_BUG_ON(ctio->pglist_cnt);
	if (ctio->dxfer_len)
	{
		free(ctio->data_ptr, M_QISP);
	}
	ctio_free_sense(ctio);
	free(ctio, M_QISP);
}

void
__ctio_free_data(struct qsio_scsiio *ctio)
{
	tgtcmd_t *cmd = ctio_cmd(ctio);

	if (!cmd->local_pool)
	{
		(*icbs.ctio_free_data)(ctio);
	}
	else
	{
		if (ctio->data_ptr) {
			free(ctio->data_ptr, M_DEVBUF);
			ctio->data_ptr = NULL;
			ctio->dxfer_len = 0;
		}
	}
}

void
__ctio_free_all(struct qsio_scsiio *ctio, int local_pool)
{
	if (!local_pool)
	{
		(*icbs.ctio_free_all)(ctio);
		DEBUG_BUG_ON(!atomic_read(&alloced_cmds));
		if (atomic_dec_and_test(&alloced_cmds))
			chan_wakeup(&alloced_cmds_wait);
	}
	else
	{
		__local_ctio_free_all(ctio);
	}
}

void fcbridge_free_initiator(uint64_t i_prt[], uint64_t t_prt[])
{
#if 0 /* Not required */
	if (atomic_read(&icbs.itf_enabled))
		(*icbs.device_free_initiator)(i_prt, t_prt, TARGET_INT_FC, NULL);
#endif
}

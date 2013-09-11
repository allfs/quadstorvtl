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

#include "coredefs.h"
#include "tdrive.h"
#include "mchanger.h"
#include "sense.h"
#include "blk_map.h"
#include "vendor.h"
#include "vdevdefs.h"

static void
tdrive_wait_for_write_queue(struct tdrive *tdrive)
{
	while (atomic_read(&tdrive->write_devq->pending_cmds))
		pause("psg", 100);
}

void
tdrive_empty_write_queue(struct tdrive *tdrive)
{
	struct tape *tape;
	struct tape_partition *partition;

	tape = tdrive->tape;
	if (unlikely(!tape))
		return;

	tdrive_wait_for_write_queue(tdrive);
	partition = tape->cur_partition;
	if (atomic_test_bit(PARTITION_DIR_WRITE, &partition->flags))
		tape_flush_buffers(tdrive->tape);
}

static void
tdrive_init_mode_block_descriptor(struct tdrive *tdrive)
{
	struct mode_parameter_block_descriptor *descriptor = &tdrive->block_descriptor;

	bzero(descriptor, sizeof(*descriptor));
	descriptor->density_code = DEFAULT_DENSITY_CODE;
	descriptor->block_length[0] = (DEFAULT_BLOCK_LENGTH >> 16) & 0xFF;
	descriptor->block_length[1] = (DEFAULT_BLOCK_LENGTH >> 8) & 0xFF;
	descriptor->block_length[2] = (DEFAULT_BLOCK_LENGTH) & 0xFF;
}

static void
tdrive_init_device_configuration_page(struct tdrive *tdrive)
{
	struct device_configuration_page *page = &tdrive->configuration_page;

	bzero(page, sizeof(*page));
	page->page_code = DEVICE_CONFIGURATION_PAGE;
	page->page_length = 0xE;
	page->rew = 0x40; /* Block identifiers supported */
	page->select_data_compression_algorithm = 1;
}

static void
tdrive_init_device_configuration_ext_page(struct tdrive *tdrive)
{
	struct device_configuration_ext_page *page = &tdrive->configuration_ext_page;

	bzero(page, sizeof(*page));
	page->page_code = DEVICE_CONFIGURATION_PAGE;
	page->sub_page_code = DEVICE_CONFIGURATION_EXTENSION_PAGE;
	page->page_length = htobe16(0x1C);
	page->write_mode = 0x2; /* Short Erase Mode */
}


static void
tdrive_init_data_compression_page(struct tdrive *tdrive, struct vdeviceinfo *deviceinfo)
{
	struct data_compression_page *page = &tdrive->compression_page;

	bzero(page, sizeof(*page));
	page->page_code = DATA_COMPRESSION_PAGE;
	page->page_length = sizeof(struct data_compression_page) - offsetof(struct data_compression_page, dcc);
	page->dcc |= 0x40; /* Data compression capable now */
	if (deviceinfo->enable_compression)
		page->dcc |= 0x80; /* Data compression enable by default*/
	page->red |= 0x80; /* Data decompression enabled all the times */
}

static int 
get_max_additional_partitions(struct tape *tape)
{
	switch (tape->make) {
	case VOL_TYPE_LTO_5:
		return 1;
	case VOL_TYPE_LTO_6:
		return 3;
	default:
		return 0;
	}
}

static uint64_t
partition_size_from_units(uint16_t part_size, uint8_t units)
{
	uint64_t partition_mult = 10;
	int i;

	for (i = 1; i < units; i++) {
		partition_mult *= 10;
	}
	return part_size * partition_mult;
}

static uint16_t
partition_size_to_units(uint64_t part_size, uint8_t units)
{
	uint64_t partition_div = 10;
	int i;

	if (!units)
		units = 9;

	for (i = 1; i < units; i++) {
		partition_div *= 10;
	}
	debug_check((part_size / partition_div) > 0xFFFFULL);
	return (uint16_t)(part_size / partition_div);
}

static void
tdrive_init_medium_partition_page(struct tdrive *tdrive)
{
	struct medium_partition_page *page = &tdrive->partition_page;
	struct tape *tape;
	struct tape_partition *partition;
	uint16_t partition_size;
	int count = 0, idp = 0, i;

	bzero(page, sizeof(*page));
	page->page_code = MEDIUM_PARTITION_PAGE;
	page->page_length = sizeof(*page) - 2;
	tape = tdrive->tape;
	if (!tape)
		return;

	page->partition_units = 9; /* 1 G */
	page->max_addl_partitions = get_max_additional_partitions(tape);
	page->fdp = (0x1 << 2); /* POFM */
	page->fdp |= (0x3 << 3); /* PSUM */
	page->medium_fmt_recognition = 0x3;
	count = tape_partition_count(tape);
	sys_memset(page->partition_size, 0, sizeof(page->partition_size));
	for (i = 0; i < count; i++) {
		partition = tape_get_partition(tape, i);
		partition_size = partition_size_to_units(partition->size, page->partition_units & 0xF);
		page->partition_size[i] = htobe16(partition_size);
		if (!partition->partition_id) {
			if (partition->size != tape->size || tape->size != tape->set_size)
				idp = 1;
		}
#if 0
		page->page_length += 2;
#endif
	}
	page->addl_partitions_defined = count - 1;
	if (count > 1)
		idp = 1;

	if (idp)
		page->fdp |= 0x20;
	else
		page->fdp |= 0x80;
}

int 
tdrive_config_compression_page(struct tdrive *tdrive, int enable)
{
	struct data_compression_page *page = &tdrive->compression_page;

	if (enable)
	{
		page->compression_algorithm = htobe32(0xFF);
		page->dcc |= 0x80;
	}
	else
	{
		page->compression_algorithm = 0x00;
		page->dcc &= ~(0x80);
	}

	return 0;
}

static void
tdrive_init_rw_error_recovery_page(struct tdrive *tdrive)
{
	struct rw_error_recovery_page *page = &tdrive->rw_recovery_page;

	bzero(page, sizeof(*page));
	page->page_code = READ_WRITE_ERROR_RECOVERY_PAGE;
	page->page_length = sizeof(struct rw_error_recovery_page) - offsetof(struct rw_error_recovery_page, dcr);
}

static void
tdrive_init_disconnect_reconnect_page(struct tdrive *tdrive)
{
	struct disconnect_reconnect_page *page = &tdrive->disreconn_page;

	bzero(page, sizeof(*page));
	page->page_code = DISCONNECT_RECONNECT_PAGE;
	page->page_length = sizeof(struct disconnect_reconnect_page) - offsetof(struct disconnect_reconnect_page, buffer_full_ratio);
}

static void
tdrive_init_drive_params(struct drive_parameters *drive_params)
{
	drive_params->granularity = TDRIVE_GRANULARITY;
	drive_params->max_block_size[0] = (TDRIVE_MAX_BLOCK_SIZE >> 16) & 0xFF;
	drive_params->max_block_size[1] = (TDRIVE_MAX_BLOCK_SIZE >> 8) & 0xFF;
	drive_params->max_block_size[2] = (TDRIVE_MAX_BLOCK_SIZE) & 0xFF;
	drive_params->min_block_size[0] = (TDRIVE_MIN_BLOCK_SIZE >> 8) & 0xFF;
	drive_params->min_block_size[1] = (TDRIVE_MIN_BLOCK_SIZE) & 0xFF;
}

static void
tdrive_init_inquiry_data(struct tdrive *tdrive)
{
	struct inquiry_data *inquiry = &tdrive->inquiry;

	bzero(inquiry, sizeof(*inquiry));
	inquiry->device_type = T_SEQUENTIAL;
	inquiry->rmb = 0x80;
	inquiry->version = ANSI_VERSION_SCSI3;
	inquiry->response_data = RESPONSE_DATA; 
	inquiry->additional_length = STANDARD_INQUIRY_LEN - 5; /* n - 4 */
	sys_memset(&inquiry->vendor_id, ' ', 8);
	memcpy(&inquiry->vendor_id, VENDOR_ID_QUADSTOR, strlen(VENDOR_ID_QUADSTOR));
	sys_memset(&inquiry->product_id, ' ', 16);
	memcpy(&inquiry->product_id, PRODUCT_ID_QUADSTOR, strlen(PRODUCT_ID_QUADSTOR));
	memcpy(&inquiry->revision_level, PRODUCT_REVISION_QUADSTOR, strlen(PRODUCT_REVISION_QUADSTOR));
}

static void
tdrive_init_handlers(struct tdrive *tdrive)
{
	switch (tdrive->make)
	{
		case DRIVE_TYPE_VHP_ULT232:
		case DRIVE_TYPE_VHP_ULT448:
		case DRIVE_TYPE_VHP_ULT460:
		case DRIVE_TYPE_VHP_ULT960:
		case DRIVE_TYPE_VHP_ULT1840:
		case DRIVE_TYPE_VHP_ULT3280:
		case DRIVE_TYPE_VHP_ULT6250:
		case DRIVE_TYPE_VIBM_3580ULT1:
		case DRIVE_TYPE_VIBM_3580ULT2:
		case DRIVE_TYPE_VIBM_3580ULT3:
		case DRIVE_TYPE_VIBM_3580ULT4:
		case DRIVE_TYPE_VIBM_3580ULT5:
		case DRIVE_TYPE_VIBM_3580ULT6:
			vultrium_init_handlers(tdrive);
			break;
		case DRIVE_TYPE_VHP_DLTVS80:
		case DRIVE_TYPE_VHP_DLTVS160:
		case DRIVE_TYPE_VHP_SDLT220:
		case DRIVE_TYPE_VHP_SDLT320:
		case DRIVE_TYPE_VHP_SDLT600:
		case DRIVE_TYPE_VQUANTUM_SDLT220:
		case DRIVE_TYPE_VQUANTUM_SDLT320:
		case DRIVE_TYPE_VQUANTUM_SDLT600:
			vsdlt_init_handlers(tdrive);
			break;
		default:
			break;
	}
}

static int 
tdrive_init(struct tdrive *tdrive, struct vdeviceinfo *deviceinfo)
{
	int retval;

	tdrive->write_devq = devq_init(deviceinfo->tl_id, deviceinfo->target_id, &tdrive->tdevice, "dwriteq", tdrive_proc_write_cmd);
	if (unlikely(!tdrive->write_devq))
		return -1;

	retval = tdevice_init(&tdrive->tdevice, T_SEQUENTIAL, deviceinfo->tl_id, deviceinfo->target_id, deviceinfo->name, tdrive_proc_cmd, "tdrv");
	if (unlikely(retval != 0)) {
		devq_exit(tdrive->write_devq);
		return -1;
	}

	tdrive->tdrive_lock = sx_alloc("tdrive lock");
	SLIST_INIT(&tdrive->density_list);
	LIST_INIT(&tdrive->media_list);
	TDRIVE_SET_BUFFERED_MODE(tdrive);

	tdrive_init_mode_block_descriptor(tdrive);
	tdrive_init_data_compression_page(tdrive, deviceinfo);
	tdrive_init_medium_partition_page(tdrive);
	tdrive_init_device_configuration_page(tdrive);
	tdrive_init_device_configuration_ext_page(tdrive);
	tdrive_init_disconnect_reconnect_page(tdrive);
	tdrive_init_rw_error_recovery_page(tdrive);
	tdrive_init_handlers(tdrive);

	if (tdrive->handlers.init_inquiry_data)
		(*tdrive->handlers.init_inquiry_data)(tdrive);
	else
		tdrive_init_inquiry_data(tdrive);

	return 0;
}

static void
tdrive_construct_serial_number(struct tdrive *tdrive, struct vdeviceinfo *deviceinfo)
{
	switch (tdrive->make) {
	case DRIVE_TYPE_VHP_DLTVS80:
	case DRIVE_TYPE_VHP_DLTVS160:
	case DRIVE_TYPE_VHP_SDLT220:
	case DRIVE_TYPE_VHP_SDLT320:
	case DRIVE_TYPE_VHP_SDLT600:
	case DRIVE_TYPE_VHP_ULT232:
	case DRIVE_TYPE_VHP_ULT448:
	case DRIVE_TYPE_VHP_ULT460:
	case DRIVE_TYPE_VHP_ULT960:
	case DRIVE_TYPE_VHP_ULT1840:
	case DRIVE_TYPE_VHP_ULT3280:
	case DRIVE_TYPE_VHP_ULT6250:
	case DRIVE_TYPE_VQUANTUM_SDLT220:
	case DRIVE_TYPE_VQUANTUM_SDLT320:
	case DRIVE_TYPE_VQUANTUM_SDLT600:
	case DRIVE_TYPE_VIBM_3580ULT1:
	case DRIVE_TYPE_VIBM_3580ULT2:
	case DRIVE_TYPE_VIBM_3580ULT3:
	case DRIVE_TYPE_VIBM_3580ULT4:
	case DRIVE_TYPE_VIBM_3580ULT5:
	case DRIVE_TYPE_VIBM_3580ULT6:
		sprintf(tdrive->unit_identifier.serial_number, "QDRS%03X%03X", deviceinfo->tl_id, deviceinfo->target_id);
		break;
	default:
		debug_check(1);
		break;
	}
}

struct tdrive *
tdrive_new(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo)
{
	struct tdrive *tdrive;
	int retval;

	tdrive = zalloc(sizeof(struct tdrive), M_DRIVE, Q_WAITOK);
	if (unlikely(!tdrive)) {
		debug_warn("Cannot allocate for a new drive\n");
		return NULL;
	}

	tdrive->mchanger = mchanger;
	tdrive->make = deviceinfo->make;
	tdrive_construct_serial_number(tdrive, deviceinfo);
	strcpy(deviceinfo->serialnumber, tdrive->unit_identifier.serial_number);
	tdrive->serial_len = strlen(tdrive->unit_identifier.serial_number);

	retval = tdrive_init(tdrive, deviceinfo);
	if (unlikely(retval != 0)) {
		debug_warn("tdrive init failed\n");
		free(tdrive, M_DRIVE);
		return NULL;
	}

	return tdrive;
}

static void
tdrive_free_density_list(struct tdrive *tdrive)
{
	struct density_descriptor *desc; 

	while ((desc = SLIST_FIRST(&tdrive->density_list)) != NULL) {
		SLIST_REMOVE_HEAD(&tdrive->density_list, d_list);
		free(desc, M_DRIVE);
	}
	return;
}

static void	
tdrive_free_tapes(struct tdrive *tdrive, int delete)
{
	struct tape *tape, *next;

	LIST_FOREACH_SAFE(tape, &tdrive->media_list, t_list, next) {
		LIST_REMOVE(tape, t_list);
		tape_free(tape, delete);
	}
}

void
tdrive_cbs_disable(struct tdrive *tdrive)
{
	cbs_disable_device((struct tdevice *)tdrive);
}

void
tdrive_cbs_remove(struct tdrive *tdrive)
{
	device_wait_all_initiators(&tdrive->tdevice.istate_list);
	cbs_remove_device((struct tdevice *)tdrive);
}

void
tdrive_free(struct tdrive *tdrive, int delete)
{
	tdrive_cbs_disable(tdrive);
	/* Wait for the current threads to complete */
	if (tdrive->tape && (!tdrive->tape->locked || tdrive->tape->locked_by == tdrive)) {
		tdrive_empty_write_queue(tdrive);
		tape_flush_buffers(tdrive->tape);
		if (tdrive->mchanger)
			tape_free(tdrive->tape, delete);
	}

	tdrive_cbs_remove(tdrive);
	tdrive_free_tapes(tdrive, delete);
	tdrive_free_density_list(tdrive);
	tdevice_exit(&tdrive->tdevice);
	devq_exit(tdrive->write_devq);
	sx_free(tdrive->tdrive_lock);
	free(tdrive, M_DRIVE);
}

static void
tdrive_incr_pending_writes(struct tdrive *tdrive, uint32_t block_size, uint32_t num_blocks)
{
	struct tape_partition *partition = tdrive->tape->cur_partition;

	while (atomic_read(&partition->pending_size) > TDRIVE_WRITE_CACHE_SIZE)
		pause("psg", 10);

	atomic_add((block_size * num_blocks), &partition->pending_size);
	atomic_add(num_blocks, &partition->pending_writes);
}	

static void
tdrive_decr_pending_writes(struct tdrive *tdrive, uint32_t block_size, uint32_t num_blocks)
{
	struct tape_partition *partition = tdrive->tape->cur_partition;

	atomic_sub((block_size * num_blocks), &partition->pending_size);
	atomic_sub(num_blocks, &partition->pending_writes);
}

int
tdrive_compression_enabled(struct tdrive *tdrive)
{
	struct data_compression_page *page = &tdrive->compression_page;
	struct device_configuration_page *dev_page = &tdrive->configuration_page;
	return (dev_page->select_data_compression_algorithm && (page->dcc & 0x80));
}

int
__tdrive_load_tape(struct tdrive *tdrive, struct tape *tape)
{
	if (unlikely(tdrive->tape != NULL))
	{
		return -1;
	}

	tdrive->tape = tape;
	if (tdrive->handlers.load_tape)
	{
		(*tdrive->handlers.load_tape)(tdrive, tape);
	}
	atomic_set_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags);
	tdrive_init_medium_partition_page(tdrive);
	TDRIVE_STATS_ADD(tdrive, load_count, 1);
	return 0;
}

int
tdrive_load_tape(struct tdrive *tdrive, struct tape *tape)
{
	int retval;

	tdrive_lock(tdrive);
	retval = __tdrive_load_tape(tdrive, tape);
	tdrive_unlock(tdrive);
	return retval;
}

void
tdrive_init_tape_metadata(struct tdevice *tdevice, struct tape *tape)
{
	struct raw_tape *raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));
	struct tdrive *tdrive = (struct tdrive *)tdevice;
	struct vtl_info *vtl_info = &raw_tape->vtl_info;

	strcpy(vtl_info->name, tdevice->name);
	strcpy(vtl_info->serialnumber, tdrive->unit_identifier.serial_number); 
	vtl_info->tl_id = tdevice->tl_id;
	vtl_info->type = tdrive->make;
}

int
tdrive_new_vcartridge(struct tdrive *tdrive, struct vcartridge *vinfo)
{
	struct tape *tape;

	debug_check(tdrive->mchanger);

	tape = tape_new((struct tdevice *)tdrive, vinfo);
	if (!tape)
		return -1;

	if (!tdrive->tape)
		__tdrive_load_tape(tdrive, tape);
	LIST_INSERT_HEAD(&tdrive->media_list, tape, t_list);
	return 0;
}

static struct tape *
__tdrive_find_tape(struct tdrive *tdrive, uint32_t tape_id)
{
	struct tape *tape;

	LIST_FOREACH(tape, &tdrive->media_list, t_list) {
		if (tape->tape_id == tape_id)
			return tape;
	}
	return NULL;
}

static struct tape *
tdrive_find_tape(struct tdrive *tdrive, uint32_t tape_id)
{
	struct tape *tape;

	tdrive_lock(tdrive);
	tape = __tdrive_find_tape(tdrive, tape_id);
	tdrive_unlock(tdrive);
	return tape;
}

int
tdrive_reset_stats(struct tdrive *tdrive, struct vdeviceinfo *deviceinfo)
{
	bzero(&tdrive->stats, sizeof(tdrive->stats));
	return 0;
}

int
tdrive_vcartridge_info(struct tdrive *tdrive, struct vcartridge *vcartridge)
{
	struct tape *tape;

	tape = tdrive_find_tape(tdrive, vcartridge->tape_id);
	if (!tape)
		return -1;
	tape_get_info(tape, vcartridge);
	return 0;
}

int
tdrive_get_info(struct tdrive *tdrive, struct vdeviceinfo *deviceinfo)
{
	tdrive_lock(tdrive);
	if (tdrive->tape)
		strcpy(deviceinfo->tape_label, tdrive->tape->label);
	memcpy(&deviceinfo->stats, &tdrive->stats, sizeof(tdrive->stats));
	deviceinfo->stats.write_ticks = ticks_to_msecs(tdrive->stats.write_ticks);
	deviceinfo->stats.read_ticks = ticks_to_msecs(tdrive->stats.read_ticks);
	deviceinfo->stats.compression_enabled = tdrive_compression_enabled(tdrive);
	tdrive_unlock(tdrive);
	return 0;
}

int
tdrive_delete_vcartridge(struct tdrive *tdrive, struct vcartridge *vcartridge)
{
	int retval;
	struct tape *tape;

	if (tdrive->mchanger)
		tape = tdrive->tape;
	else
		tape = tdrive_find_tape(tdrive, vcartridge->tape_id);
	if (!tape)
		return -1;

	if (tape == tdrive->tape) {
		retval = tdrive_unload_tape(tdrive, NULL);
		if (unlikely(retval != 0))
			return -1;
	}

	LIST_REMOVE_INIT(tape, t_list);
	tape_free(tape, vcartridge->free_alloc);
	return 0;
}

int
tdrive_load_vcartridge(struct tdrive *tdrive, struct vcartridge *vinfo)
{
	struct tape *tape;

	debug_check(tdrive->mchanger);
	tape = tape_load((struct tdevice *)tdrive, vinfo);

	if (!tape)
		return -1;

	LIST_INSERT_HEAD(&tdrive->media_list, tape, t_list);
	return 0;
}

static int
tdrive_rewind_on_unload(struct tdrive *tdrive)
{
	switch (tdrive->make)
	{
		case DRIVE_TYPE_VIBM_3580ULT1:
		case DRIVE_TYPE_VIBM_3580ULT2:
		case DRIVE_TYPE_VIBM_3580ULT3:
		case DRIVE_TYPE_VIBM_3580ULT4:
		case DRIVE_TYPE_VIBM_3580ULT5:
		case DRIVE_TYPE_VIBM_3580ULT6:
			return 0;
		default:
			return 1;
	}
}

int tdrive_unload_tape(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	int retval;
	struct initiator_state *istate;
	struct istate_list *istate_list;

	tdrive_lock(tdrive);
	if (!tdrive->tape) {
		tdrive_unlock(tdrive);
		return 0;
	}

	if (!atomic_test_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags)) {
		tdrive->tape = NULL;
		tdrive_init_medium_partition_page(tdrive);
		tdrive_unlock(tdrive);
		return 0;
	}

	istate_list = &tdrive->tdevice.istate_list;
	SLIST_FOREACH(istate, istate_list, i_list) {
		if (istate->prevent_medium_removal)
		{
			debug_warn("One of the initiators prevented medium removal\n");
			if (!ctio || !iid_equal(istate->i_prt, istate->t_prt, istate->init_int, ctio->i_prt, ctio->t_prt, ctio->init_int))
			{
				tdrive_unlock(tdrive);
				return -1;
			}
		}
	}

	/* Unloading a tape would involve flushing the cache buffers to disk */
	/* Keeping track how the tape was priorly unloaded */
	tdrive_empty_write_queue(tdrive);
	retval = tape_cmd_unload(tdrive->tape, tdrive_rewind_on_unload(tdrive));
	if (unlikely(retval != 0)) {
		debug_warn("tape_cmd_unload failed\n");
		tdrive_unlock(tdrive);
		return -1;
	}
	atomic_clear_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags);

	tdrive->tape = NULL;
	if (tdrive->handlers.unload_tape)
		(*tdrive->handlers.unload_tape)(tdrive);
	tdrive_init_medium_partition_page(tdrive);
	tdrive_unlock(tdrive);
	return 0;
}

static int
tdrive_cmd_report_luns(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint32_t allocation_length;
	uint8_t *cdb = ctio->cdb;
	int length, num_luns = 1;

	allocation_length = be32toh(*((uint32_t *)(&cdb[6])));
	if (!allocation_length)
		return 0;

	length = 8 + num_luns * 8;
	ctio_allocate_buffer(ctio, length, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
		return -1;

	bzero(ctio->data_ptr, length);
	if (ctio->init_int == TARGET_INT_FC)
		__write_lun(tdrive->tdevice.tl_id, tdrive->tdevice.target_id, ctio->data_ptr+8);
	ctio->scsi_status = SCSI_STATUS_OK;
	*((uint32_t *)ctio->data_ptr) = htobe32(length - 8);
	ctio->dxfer_len = min_t(int, length, allocation_length);
	return 0;
}

struct extended_inquiry_page extended_inquiry = {
	.device_type = T_DIRECT,
	.page_code = EXTENDED_INQUIRY_VPD_PAGE,
	.page_length = 0x3C,
	.simpsup = (0x01 | 0x02 | 0x04), /* Simple, Ordered, Head of Queue commands */
};

static int
tdrive_copy_extended_inquiry_vpd_page(struct qsio_scsiio *ctio, uint16_t allocation_length)
{
	uint16_t min_len;

	min_len = min_t(uint16_t, allocation_length, sizeof(extended_inquiry));
	memcpy(ctio->data_ptr, &extended_inquiry, min_len);
	return min_len;
}

static int
tdrive_vendor_specific_page2(struct qsio_scsiio *ctio, uint16_t allocation_length)
{
	struct vendor_specific_page *page;

	page = (struct vendor_specific_page *)(ctio->data_ptr);
	page->device_type = T_SEQUENTIAL; /* peripheral qualifier */
	page->page_code = VENDOR_SPECIFIC_PAGE2;
	page->page_length = allocation_length - sizeof(struct vendor_specific_page);
	return allocation_length; 
}

static int
tdrive_evpd_inquiry_data(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t page_code, uint16_t allocation_length)
{
	int retval;
	uint16_t max_allocation_length;

	max_allocation_length = max_t(uint16_t, 64, allocation_length);
	ctio_allocate_buffer(ctio, max_allocation_length, Q_WAITOK);
	if (!ctio->data_ptr)
		return -1;
	bzero(ctio->data_ptr, ctio->dxfer_len);

	switch (page_code) {
	case VITAL_PRODUCT_DATA_PAGE:
		retval = tdrive_copy_vital_product_page_info(tdrive, ctio->data_ptr, allocation_length);
		break;
	case UNIT_SERIAL_NUMBER_PAGE:
		retval = tdrive_serial_number(tdrive, ctio->data_ptr, allocation_length);
		break;
	case DEVICE_IDENTIFICATION_PAGE:
		retval = tdrive_device_identification(tdrive, ctio->data_ptr, allocation_length);
		break;
	case EXTENDED_INQUIRY_VPD_PAGE:
		retval = tdrive_copy_extended_inquiry_vpd_page(ctio, allocation_length);
		break;
	case VENDOR_SPECIFIC_PAGE2:
		retval = tdrive_vendor_specific_page2(ctio, allocation_length);
		break;
	default:
		if (tdrive->handlers.evpd_inquiry)
			retval = (*tdrive->handlers.evpd_inquiry)(tdrive, ctio, page_code, allocation_length);
		else {
			ctio_free_data(ctio);
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);
			retval = 0;
		}
	}
	ctio->dxfer_len = retval;
	return 0;
}

static int
tdrive_standard_inquiry_data(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint16_t allocation_length)
{
	uint16_t min_len;

	min_len = min_t(uint16_t, allocation_length, sizeof(struct inquiry_data));
	ctio_allocate_buffer(ctio, min_len, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
	{
		return -1;
	}

	ctio->scsi_status = SCSI_STATUS_OK;
	memcpy(ctio->data_ptr, &tdrive->inquiry, min_len);	
	return 0;
}

static int
tdrive_cmd_inquiry(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	int retval;
	uint16_t allocation_length;
	uint8_t evpd, page_code;

	evpd = READ_BIT(cdb[1], 0);
	page_code = cdb[2];
	allocation_length = be16toh(*(uint16_t *)(&cdb[3]));

	if (!evpd && page_code)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	if (!allocation_length)
		return 0;

	if (!evpd)
		retval = tdrive_standard_inquiry_data(tdrive, ctio, allocation_length);
	else
		retval = tdrive_evpd_inquiry_data(tdrive, ctio, page_code, allocation_length);

	if (ctio->dxfer_len && ctio->init_int == TARGET_INT_ISCSI && ctio->ccb_h.target_lun) {
		ctio->data_ptr[0] = 0x7F; /* Invalid LUN */
		ctio->dxfer_len = 1;
	}
	return retval;
}

int
tdrive_media_valid(struct tdrive *tdrive, int voltype)
{
	if (!tdrive->handlers.valid_medium)
		return 0;

	return (*tdrive->handlers.valid_medium)(tdrive, voltype);
}


static int
tdrive_cmd_test_unit_ready(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{

	int media_valid = 0;
	uint8_t asc, ascq;

	if (tdrive->tape && tdrive->handlers.valid_medium)
		media_valid = (*tdrive->handlers.valid_medium)(tdrive, tdrive->tape->make);

	if (!tdrive->tape || (tdrive->tape->locked && tdrive->tape->locked_by != tdrive) || (media_valid != 0) || !atomic_read(&mdaemon_load_done)) {
		if (!tdrive->tape)
		{
			asc = MEDIUM_NOT_PRESENT_ASC;
			ascq = MEDIUM_NOT_PRESENT_ASCQ;
		}
		else if (media_valid != 0)
		{
			asc = INCOMPATIBLE_MEDIUM_INSTALLED_ASC;
			ascq = INCOMPATIBLE_MEDIUM_INSTALLED_ASCQ;
		}
		else
		{
			asc = MEDIUM_NOT_PRESENT_LOADABLE_ASC;
			ascq = MEDIUM_NOT_PRESENT_LOADABLE_ASCQ;
		}

		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_NOT_READY, 0, asc, ascq);
	}
	return 0;
}

static int
tdrive_cmd_erase(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t longbit;
	uint8_t sense_key = 0, asc = 0, ascq = 0;
	int retval;

	longbit = READ_BIT(cdb[1], 0);

	/* Ignore immed */
	tdrive_empty_write_queue(tdrive);

	if (tdrive->erase_from_bot) {
		if (!longbit)
			return 0;

		retval = tape_at_bop(tdrive->tape);
		if (!retval) {
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
			return 0;
		}
	}

	retval = tape_cmd_erase(tdrive->tape);
	if (retval == 0)
		return 0;

	switch (retval) {
	case MEDIA_ERROR:
		sense_key = SSD_KEY_MEDIUM_ERROR;
		asc = WRITE_ERROR_ASC;
		ascq = WRITE_ERROR_ASCQ;
		break;
	case OVERWRITE_WORM_MEDIA:
		sense_key = SSD_KEY_DATA_PROTECT;
		asc = WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASC;
		ascq = WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASCQ;
		break;
	default:
		debug_check(retval);
		break;
	}

	ctio_construct_sense(ctio, SSD_CURRENT_ERROR, sense_key, 0, asc, ascq);
	return 0;
}

static int
tdrive_cmd_load_unload(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t hold, eot, load;
	int retval;
	struct initiator_state *istate;

	load = READ_BIT(cdb[4], 0);
	eot = READ_BIT(cdb[4], 2);
	hold = READ_BIT(cdb[4], 3);

	if (eot) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  

		return 0;
	}

	if (load && !tdrive->tape)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_NOT_READY, 0, MEDIUM_NOT_PRESENT_ASC, MEDIUM_NOT_PRESENT_ASCQ);
		return 0;
	}

	if (!load && !tdrive->tape)
	{
		ctio->data_ptr = NULL;
		ctio->dxfer_len = 0;
		ctio->scsi_status = SCSI_STATUS_OK;
		return 0;
	}

	if (!load && !hold)
	{
		struct istate_list *istate_list;

		istate_list = &tdrive->tdevice.istate_list;
		SLIST_FOREACH(istate, istate_list, i_list) {
			if (istate->prevent_medium_removal)
			{
				ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, MEDIUM_REMOVAL_PREVENTED_ASC, MEDIUM_REMOVAL_PREVENTED_ASCQ);

				return 0;
			}
		}
	}


	if (load && !atomic_test_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags)) {
		retval = tape_cmd_load(tdrive->tape, 0); /* position at beginning of tape */
		if (retval != 0)
		{
			debug_warn("tape_cmd_load failed\n");
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_MEDIUM_ERROR, 0, 0, 0);
			return 0;
		}
		atomic_set_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags);
	}

	if (load && atomic_test_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags)) {
		tdrive_empty_write_queue(tdrive);
		retval = tape_cmd_rewind(tdrive->tape, 1);
		if (retval != 0) 
		{
			debug_warn("tape_cmd_rewind failed\n");
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_MEDIUM_ERROR, 0, 0, 0);
			return 0;
		}
	}	
	else if (!load &&  !hold && atomic_test_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags)) {
		tdrive_empty_write_queue(tdrive);
		retval = tape_cmd_unload(tdrive->tape, tdrive_rewind_on_unload(tdrive));
		if (retval != 0)
		{
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_MEDIUM_ERROR, 0, 0, 0);
			return 0;
		}
		atomic_clear_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags);
	}

	return 0;
}

static int
__tdrive_cmd_locate(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint64_t block_address, uint8_t cp, uint8_t pnum, uint8_t locate_type)
{
	int retval;

	tdrive_empty_write_queue(tdrive);

	retval = tape_cmd_locate(tdrive->tape, block_address, cp, pnum, locate_type);
	if (retval == 0)
		return 0; /* Always check for retval == 0 first */

	switch (retval) {
	case EOD_REACHED:
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR,
				SSD_KEY_BLANK_CHECK, 0,
				EOD_DETECTED_ASC, EOD_DETECTED_ASCQ);
		break;
	case MEDIA_ERROR:
	case INVALID_PARTITION:
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR,
				SSD_KEY_MEDIUM_ERROR, 0,
				SEQUENTIAL_POSITIONING_ERROR_ASC,
				SEQUENTIAL_POSITIONING_ERROR_ASCQ);
		break;
	default:
		debug_check(1);
		break;
	}
	return 0;

}

static int
tdrive_cmd_locate16(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint64_t block_address;
	uint8_t cp;
	uint8_t dest_type;
	uint8_t pnum;

	cp = READ_BIT(cdb[1], 1);
	dest_type = (cdb[1] >> 2) & 0x7;
	pnum = cdb[3];
	block_address = be64toh(*((uint64_t *)(&cdb[4])));

	debug_info("cp %x pnum %x dest type %x block address %llu\n", cp, pnum, dest_type, (unsigned long long)block_address);
	switch (dest_type) {
	case LOCATE_TYPE_BLOCK:
	case LOCATE_TYPE_FILE:
	case LOCATE_TYPE_EOD:
		break;
	default:
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);
		return 0;
	}
	return __tdrive_cmd_locate(tdrive, ctio, block_address, cp, pnum, dest_type);
}

static int
tdrive_cmd_locate(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t bt, cp;
	uint32_t block_address;
	uint8_t pnum;

	cp = READ_BIT(cdb[1], 1);
	bt = READ_BIT(cdb[1], 2);

	block_address = be32toh(*((uint32_t *)(&cdb[3])));
	pnum = cdb[8];	

	if (bt) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	return __tdrive_cmd_locate(tdrive, ctio, block_address, cp, pnum, LOCATE_TYPE_BLOCK);
}

#define INFORMATION_FIELD(info, fixed, num_blocks, done_blocks, block_size, ili_block_size) \
	if (fixed) 							\
	{								\
		info = htobe32(num_blocks - done_blocks); 	\
	}								\
	else 								\
	{								\
		info = htobe32(block_size - ili_block_size);	\
	}

static void
tdrive_construct_overwrite_worm_media_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t fixed, uint32_t num_blocks, uint32_t done_blocks, uint32_t block_size)
{
	uint32_t info = 0;

	INFORMATION_FIELD(info, fixed, num_blocks, done_blocks, block_size, 0);

	ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_KEY_MEDIUM_ERROR, info, WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASC, WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASCQ);
}

static void
tdrive_construct_vcartridge_overflow_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t fixed, uint32_t num_blocks, uint32_t done_blocks, uint32_t block_size)
{
	uint32_t info = 0;

	INFORMATION_FIELD(info, fixed, num_blocks, done_blocks, block_size, 0);

	/* Note that SSD_EOM should be set
	 * if we are beyond the EW on the tape
	 */
	ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_KEY_VOLUME_OVERFLOW|SSD_EOM, info, NO_ASC, NO_ASCQ);  
	return;

}

static void
tdrive_construct_ew_reached_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t fixed, uint32_t num_blocks, uint32_t done_blocks, uint32_t block_size)
{
	uint32_t info = 0;

	INFORMATION_FIELD(info, fixed, num_blocks, done_blocks, block_size, block_size);

	/* Note that SSD_EOM should be set
	 * if we are beyond the EW on the tape
	 */
	ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_KEY_NO_SENSE|SSD_EOM, info, EOM_DETECTED_ASC, EOM_DETECTED_ASCQ);  
	return;
}

static int
tdrive_cmd_validate_write_filemarks(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t immed;
	struct qsio_scsiio *new;
	int retval;
	uint32_t transfer_length;
	struct tape *tape = tdrive->tape;

	immed = READ_BIT(cdb[1], 0);
	transfer_length = READ_24(cdb[2], cdb[3], cdb[4]);

	new = ctio_new(Q_WAITOK);
	if (unlikely(!new)) {
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_HARDWARE_ERROR, 0, INTERNAL_TARGET_FAILURE_ASC, INTERNAL_TARGET_FAILURE_ASCQ);
		return 0;
	}

	device_initialize_ctio(ctio, new);

	retval = tape_partition_validate_write((struct tape_partition *)tape->cur_partition, 0, 0);
	if (retval == 0)
		goto insert_cmd;

	switch (retval) {
	case OVERWRITE_WORM_MEDIA:
		ctio_free_all(new);
		tdrive_construct_overwrite_worm_media_sense(tdrive, ctio, 1, transfer_length, 0, transfer_length);
		return 0;
	case VOLUME_OVERFLOW_ENCOUNTERED:
		tdrive_construct_vcartridge_overflow_sense(tdrive, ctio, 1, transfer_length, 0, transfer_length);
		ctio_free_all(new);
		return 0;
	case EW_REACHED:
		tdrive_construct_ew_reached_sense(tdrive, ctio, 1, transfer_length, transfer_length, transfer_length);
		break;
	}

insert_cmd:
	set_ctio_buffered(new);
	if (!immed) {
		ctio->data_ptr = new->data_ptr;
		ctio->dxfer_len = new->dxfer_len;
		ctio->pglist_cnt = new->pglist_cnt;
		new->data_ptr = NULL;
		new->pglist_cnt = new->dxfer_len = 0;
		ctio_free(new);
		devq_insert_ccb(tdrive->write_devq, (void *)ctio);
		
		tdrive_wait_for_write_queue(tdrive);
	}
	else {
		devq_insert_ccb(tdrive->write_devq, (void *)new);
	}
	return 0;
}

static int
tdrive_validate_medium_proportion_value(struct tape *tape, uint16_t proportion)
{
	switch (tape->make) {
	case VOL_TYPE_LTO_4:
		return (proportion > 0x123D);
	case VOL_TYPE_LTO_5:
	case VOL_TYPE_LTO_6:
		return (proportion > 0x0FD8);
	
	}
	return 0;
}

static uint64_t
get_fixed_partition_size(struct tape *tape, int pnum)
{
	if (pnum) {
		struct tape_partition *partition;

		partition = tape_get_partition(tape, 0);
		debug_check(partition->size >= tape->set_size);
		if (tape->set_size < partition->size)
			return 0;
		else
			return (tape->set_size - partition->size);
	}

	switch (tape->make) {
	case VOL_TYPE_LTO_4:
		return (52ULL * 1000 * 1000 * 1000); /* 52 GB */
	case VOL_TYPE_LTO_5:
		return (92ULL * 1000 * 1000 * 1000);
	case VOL_TYPE_LTO_6:
		return (154ULL * 1000 * 1000 * 1000);
	}
	return 0;
}

static uint64_t
tape_custom_size_adjust(struct tape *tape, uint64_t size)
{
	uint64_t default_size;
	uint64_t custom_size;
	uint64_t tape_size;
	
	default_size = get_vol_size_default(tape->make);
	if (!default_size || default_size == tape->size)
		return size;
	tape_size = (tape->size >> 30);
	custom_size = ((size * tape_size) / default_size);
	if (custom_size > size)
		return size;
	custom_size = max_t(uint64_t, custom_size, (1U << 30));
	return custom_size;
}

static int
tdrive_cmd_format_medium(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	struct tape *tape = tdrive->tape;
	struct tape_partition *partition;
	struct medium_partition_page *page;
	uint64_t partition_size;
	uint8_t *cdb = ctio->cdb;
	int i, count, retval;
	uint8_t format;
	uint8_t fdp;
	int64_t psize0 = 0 , psize1 = 0;
	int fill_to_max = 0;
	int num_partitions, partition_units;

	tdrive_wait_for_write_queue(tdrive);
	if (tape->worm) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_DATA_PROTECT, 0, WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASC, WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASCQ);
		return 0;
	}

	if (!tape_at_bot(tape)) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, POSITION_PAST_BEGINNING_OF_MEDIUM_ASC, POSITION_PAST_BEGINNING_OF_MEDIUM_ASCQ);
		return 0;
	}

	retval = tape_validate_format(tape);
	if (retval != 0) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_DATA_PROTECT, 0, WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASC, WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASCQ);
		return 0;
	}

	page = &tdrive->partition_page;
	format = cdb[2] & 0xF;

	/* SDP, FDP or IDP has to be set */
	if (format != 0x00 && ((page->fdp >> 5) & 0x7) == 0) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
		return 0;
	}

	fdp = (page->fdp >> 7) & 0x1;

	partition_units = page->partition_units & 0xF;

	if (!partition_units && page->addl_partitions_defined > 1) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
		return 0;
	}

	if (!partition_units)
		goto skip_fill_check;

	for (i = 0; i < page->addl_partitions_defined + 1; i++) {
		if (page->partition_size[i] == 0xFFFF) {
			if (fill_to_max || i > 1) {
				ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
				return -1;
			}
			fill_to_max = 1;
			continue;
		}
	}

skip_fill_check:
	if (!partition_units || fill_to_max) {
		uint16_t val0 = be16toh(page->partition_size[0]);
		uint16_t val1 = be16toh(page->partition_size[1]);
		uint64_t min_size = get_fixed_partition_size(tape, 0);

		if (val0 == 0xFFFF) {
			if (val1 > 38)
				val1 = 38;
			psize1 = min_size * val1;
			psize1 = tape_custom_size_adjust(tape, psize1);
			psize1 = align_size(psize1, BINT_UNIT_SIZE);
			if (psize1 > tape->set_size) {
				ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
				return 0;
			}
			psize0 = tape->set_size - psize1;
			psize0 = align_size(psize0, BINT_UNIT_SIZE);
		}
		else {
			if (val0 > 38)
				val0 = 38;
			psize0 = min_size * val0;
			psize0 = tape_custom_size_adjust(tape, psize0);
			psize0 = align_size(psize0, BINT_UNIT_SIZE);
			if (psize0 > tape->set_size) {
				ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
				return 0;
			}
			psize1 = tape->set_size - psize0;
			psize1 = align_size(psize1, BINT_UNIT_SIZE);
		}

		min_size = tape_custom_size_adjust(tape, min_size);
		if (psize0 < min_size || psize1 < min_size) {
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
			return 0;
		}
		goto skip_size_check;
	}

	if ((format == 0x01 || format == 0x02) && !fdp) {
		uint64_t size = 0;

		for (i = 0; i < page->addl_partitions_defined + 1; i++) {
			size += partition_size_from_units(be16toh(page->partition_size[i]), partition_units & 0xF); 
		}

		if (size > tape->set_size) {
			/* Should be volume overflow and checked in format medium */
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
			return 0;
		}

	}

skip_size_check:
	if (format == 0x00 || format == 0x02) {
		retval = tape_format_default(tape, tape->set_size, 0);
		if (retval != 0)
			return -1;
	}

	if (!format) {
		tdrive_init_medium_partition_page(tdrive);
		return 0;
	}

	count = tape_partition_count(tape);

	if (fdp)
		num_partitions = 2;
	else
		num_partitions = page->addl_partitions_defined + 1;

	for (i = num_partitions; i < count; i++) {
		retval = tape_delete_partition(tape, i);
		if (unlikely(retval != 0))
			return -1;
	}

	for (i = 0; i < num_partitions; i++) {
		partition = tape_get_partition(tape, i);
		if (fdp)
			partition_size = get_fixed_partition_size(tape, i);
		else if (partition_units && !fill_to_max)
			partition_size = partition_size_from_units(be16toh(page->partition_size[i]), partition_units);
		else
			partition_size = i ? psize1 : psize0;

		if (partition && partition_size_to_units(partition->size, partition_units) == partition_size_to_units(partition_size, partition_units))
			continue;
		partition_size = align_size(partition_size, BINT_UNIT_SIZE);
		if (!partition)
			retval = tape_add_partition(tape, partition_size, i);
		else
			retval = tape_partition_set_size(partition, partition_size, 0);
		if (unlikely(retval != 0))
			return -1;
	}
	tape_update_volume_change_reference(tape);
	tdrive_init_medium_partition_page(tdrive);
	return 0;
}

static int
tdrive_cmd_set_capacity(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	struct tape *tape = tdrive->tape;
	uint8_t *cdb = ctio->cdb;
	int retval, valid;
	uint16_t proportion;
	uint64_t set_size;

	tdrive_wait_for_write_queue(tdrive);
	proportion = be16toh(*(uint16_t *)(&cdb[3]));

	if (!tape_at_bot(tape)) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, POSITION_PAST_BEGINNING_OF_MEDIUM_ASC, POSITION_PAST_BEGINNING_OF_MEDIUM_ASCQ);
		return 0;
	}

	valid = tdrive_validate_medium_proportion_value(tape, proportion);
	if (!valid) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	set_size = (tape->size / 65535) * proportion;
	set_size = align_size(set_size, BINT_UNIT_SIZE);

	if (tape->set_size == set_size)
		return 0;

	if (tape->worm) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_DATA_PROTECT, 0, WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASC, WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASCQ);
		return 0;
	}

	if (!tape_at_bot(tape)) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, POSITION_PAST_BEGINNING_OF_MEDIUM_ASC, POSITION_PAST_BEGINNING_OF_MEDIUM_ASCQ);
		return 0;
	}

	device_unit_attention(&tdrive->tdevice, 0, ctio->i_prt, ctio->t_prt, ctio->init_int, MODE_PARAMETERS_CHANGED_ASC, MODE_PARAMETERS_CHANGED_ASCQ, 1); 
	retval = tape_format_default(tape, set_size, 1);
	if (retval != 0)
		return -1;
	return 0;
}

static int
tdrive_cmd_write_filemarks(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t wmsk;
	uint32_t transfer_length;
	uint8_t sense_key, asc, ascq;
	int retval;

	wmsk = READ_BIT(cdb[1], 1);
	transfer_length = READ_24(cdb[2], cdb[3], cdb[4]);

	if (!transfer_length)
	{
		tape_flush_buffers(tdrive->tape);
		return 0;
	}

	retval = tape_cmd_write_filemarks(tdrive->tape, wmsk, transfer_length);
	if (retval == 0)
		return 0;

	switch (retval) {
	case MEDIA_ERROR:
		sense_key = SSD_KEY_MEDIUM_ERROR;
		asc = WRITE_ERROR_ASC;
		ascq = WRITE_ERROR_ASCQ;
		break;
	case OVERWRITE_WORM_MEDIA:
		sense_key = SSD_KEY_DATA_PROTECT;
		asc = WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASC;
		ascq = WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASCQ;
		break;
	default:
		debug_check(1);
		return 0;
	}

	tape_flush_buffers(tdrive->tape);
	if (!ctio_buffered(ctio))
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, sense_key, htobe32(transfer_length), asc, ascq);
	else {
		struct initiator_state *istate;
		istate = ctio->istate;

		tdevice_reservation_lock(&tdrive->tdevice);
		device_add_sense(istate, SSD_DEFERRED_ERROR, sense_key, htobe32(transfer_length), asc, ascq);
		tdevice_reservation_unlock(&tdrive->tdevice);
	}
	return 0;
}

static uint8_t
tdrive_synchronize_at_ew(struct tdrive *tdrive)
{
	uint8_t *data = (uint8_t *)&tdrive->configuration_page;
	uint8_t sew;

	sew = (data[10] & 0x8 >> 3);
	return sew;
}

static inline int
block_size_valid(struct tdrive *tdrive, uint32_t block_size)
{
	if (block_size <= TDRIVE_MAX_BLOCK_SIZE)
		return 1;
	else
		return 0;
}

static int
tdrive_cmd_validate_write6(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t fixed;
	uint32_t transfer_length;
	uint32_t block_size;
	uint32_t num_blocks;
	struct qsio_scsiio *new;
	int retval;
	struct tape *tape = tdrive->tape;

	fixed = READ_BIT(cdb[1], 0);
	transfer_length = READ_24(cdb[2], cdb[3], cdb[4]);

	if (fixed) {
		block_size = tdrive_get_block_length(tdrive);
		num_blocks = transfer_length;
	} else {
		block_size = transfer_length;
		num_blocks = 1;
	}

	if (!block_size_valid(tdrive, block_size)) {
		uint32_t info = 0;

		ctio_free_data(ctio);
		INFORMATION_FIELD(info, fixed, num_blocks, 0, block_size, 0);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_KEY_ILLEGAL_REQUEST, info, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		debug_warn("Invalid block size %u\n", block_size);
		return 0;
	}

	new = ctio_new(Q_WAITOK);
	if (unlikely(!new)) {
		debug_warn("Cannot allocate for a new ctio failed\n");
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_HARDWARE_ERROR, 0, INTERNAL_TARGET_FAILURE_ASC, INTERNAL_TARGET_FAILURE_ASCQ);
		return 0;
	}
	device_initialize_ctio(ctio, new);

	retval = tape_partition_validate_write(tape->cur_partition, block_size, num_blocks);
	if (retval == 0)
		goto insert_cmd;

	debug_warn("retval %d\n", retval);
	switch (retval) {
	case OVERWRITE_WORM_MEDIA:
		ctio_free_data(ctio);
		ctio_free_all(new);
		tdrive_construct_overwrite_worm_media_sense(tdrive, ctio, fixed, num_blocks, 0, block_size);
		return 0;
	case VOLUME_OVERFLOW_ENCOUNTERED:
		ctio_free_data(ctio);
		ctio_free_all(new);
		tdrive_construct_vcartridge_overflow_sense(tdrive, ctio, fixed, num_blocks, 0, block_size);
		return 0;
	case EW_REACHED:
		tdrive_construct_ew_reached_sense(tdrive, ctio, 1, num_blocks, num_blocks, block_size);

		if (tdrive_synchronize_at_ew(tdrive))
			tdrive_empty_write_queue(tdrive);
		break;
	default:
		debug_check(1);
		break;
	}

insert_cmd:
	tdrive_incr_pending_writes(tdrive, block_size, num_blocks);
	set_ctio_buffered(new);
	if (!(TDRIVE_GET_BUFFERED_MODE(tdrive))) {
		ctio->data_ptr = new->data_ptr;
		ctio->dxfer_len = new->dxfer_len;
		ctio->pglist_cnt = new->pglist_cnt;
		new->data_ptr = NULL;
		new->pglist_cnt = new->dxfer_len = 0;
		ctio_free(new);
		devq_insert_ccb(tdrive->write_devq, (void *)ctio);
		tdrive_wait_for_write_queue(tdrive);
	}
	else {
		devq_insert_ccb(tdrive->write_devq, (void *)new);
	}
	return 0;
}

static int
tdrive_cmd_write6(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t fixed;
	uint32_t transfer_length;
	int retval;
	uint8_t sense_key, asc, ascq;
	uint32_t done_blocks = 0;
	uint32_t block_size;
	uint32_t num_blocks;
	uint32_t compressed_size;

	fixed = READ_BIT(cdb[1], 0);
	transfer_length = READ_24(cdb[2], cdb[3], cdb[4]);

	/* Return 0 for zero transfer length */
	if (!transfer_length)
	{
		return 0;
	}

	/* Setup the data buffers */
	if (fixed)
	{
		num_blocks = transfer_length;
		block_size = tdrive_get_block_length(tdrive);
	}
	else
	{
		block_size = transfer_length;
		num_blocks = 1;
	}

	compressed_size = 0;
	retval = tape_cmd_write(tdrive->tape, ctio, block_size, num_blocks, &done_blocks, tdrive_compression_enabled(tdrive), &compressed_size);
	if (ctio_buffered(ctio))
		tdrive_decr_pending_writes(tdrive, block_size, num_blocks);

	if (retval == 0) {
		TDRIVE_STATS_ADD(tdrive, write_bytes_processed, (block_size * num_blocks));
		if (compressed_size) {
			TDRIVE_STATS_ADD(tdrive, bytes_written_to_tape, compressed_size);
			TDRIVE_STATS_ADD(tdrive, compressed_bytes_written, compressed_size);
		}
		else
			TDRIVE_STATS_ADD(tdrive, bytes_written_to_tape, (block_size * num_blocks));
		return 0;
	}

	switch (retval) {
	case MEDIA_ERROR:
		sense_key = SSD_KEY_MEDIUM_ERROR;
		asc = WRITE_ERROR_ASC;
		ascq = WRITE_ERROR_ASCQ;
		TDRIVE_STATS_ADD(tdrive, write_errors, 1);
		break;
	case OVERWRITE_WORM_MEDIA:
		sense_key = SSD_KEY_DATA_PROTECT;
		asc = WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASC;
		ascq = WORM_MEDIUM_OVERWRITE_ATTEMPTED_ASCQ;
		break;
	default:
		debug_check(1);
		return 0;
	}

	if (!ctio_buffered(ctio))
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, sense_key, htobe32(transfer_length), asc, ascq);
	else {
		struct initiator_state *istate;
		istate = ctio->istate;

		tdevice_reservation_lock(&tdrive->tdevice);
		device_add_sense(istate, SSD_DEFERRED_ERROR, sense_key, htobe32(transfer_length), asc, ascq);
		tdevice_reservation_unlock(&tdrive->tdevice);
	}
	return 0;

}

static void
tdrive_construct_filemark_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t fixed, uint32_t num_blocks, uint32_t done_blocks, uint32_t block_size, uint32_t ili_block_size)
{
	uint32_t info = 0;

	INFORMATION_FIELD(info, fixed, num_blocks, done_blocks, block_size, ili_block_size);
	ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_FILEMARK | SSD_KEY_NO_SENSE, info, FILEMARK_DETECTED_ASC, FILEMARK_DETECTED_ASCQ); 
	return;
}

static void
tdrive_construct_setmark_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t fixed, uint32_t num_blocks, uint32_t done_blocks, uint32_t block_size, uint32_t ili_block_size)
{
	uint32_t info = 0;

	INFORMATION_FIELD(info, fixed, num_blocks, done_blocks, block_size, ili_block_size);
	ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_FILEMARK | SSD_KEY_NO_SENSE, info, SETMARK_DETECTED_ASC, SETMARK_DETECTED_ASCQ); 
	return;
}

static void
tdrive_construct_bomp_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t fixed, uint32_t num_blocks, uint32_t done_blocks, uint32_t block_size)
{
	uint32_t info = 0;

	INFORMATION_FIELD(info, fixed, num_blocks, done_blocks, block_size, 0);
	/* Note that SSD_EOM should be set
	 * if we are beyond the EW on the tape
	 */
	ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_KEY_NO_SENSE, info, BOM_DETECTED_ASC, BOM_DETECTED_ASCQ);
	return;

}

static void
tdrive_construct_ili_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t fixed, uint32_t num_blocks, uint32_t done_blocks, uint32_t block_size, uint8_t sili, uint8_t error_type, uint32_t ili_block_size)
{
	uint32_t info = 0;
	uint32_t tape_block_length = tdrive_get_block_length(tdrive);

	if (sili)
	{
		if (!tape_block_length && error_type == OVERLENGTH_COND_ENCOUNTERED)
		{
			return;
		}
		else if (error_type == UNDERLENGTH_COND_ENCOUNTERED)
		{
			return;
		}
	}

	INFORMATION_FIELD(info, fixed, num_blocks, done_blocks, block_size, ili_block_size);
	ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_KEY_NO_SENSE|SSD_ILI, info, NO_ASC, NO_ASCQ);
	return;

}

static void
tdrive_construct_eod_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t fixed, uint32_t num_blocks, uint32_t done_blocks, uint32_t block_size, uint32_t ili_block_size)
{
	uint32_t info = 0;

	INFORMATION_FIELD(info, fixed, num_blocks, done_blocks, block_size, ili_block_size);

	/* Note that SSD_EOM should be set
	 * if we are beyond the EW on the tape
	 */
	ctio_construct_sense(ctio, SSD_CURRENT_ERROR|SSD_ERRCODE_VALID, SSD_KEY_BLANK_CHECK, info, EOD_DETECTED_ASC, EOD_DETECTED_ASCQ);
	return;

}

static int
tdrive_cmd_read6(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t sili, fixed;
	uint32_t transfer_length;
	int retval;
	uint32_t done_blocks = 0;
	uint32_t block_size;
	uint32_t num_blocks;
	uint32_t ili_block_size = 0;
	uint32_t compressed_size = 0;
	uint32_t start_ticks;

	fixed = READ_BIT(cdb[1], 0);
	sili = READ_BIT(cdb[1], 1);

	if (fixed && sili) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	tdrive_empty_write_queue(tdrive);

	transfer_length = READ_24(cdb[2], cdb[3], cdb[4]);

	/* Return 0 for zero transfer length */
	if (!transfer_length)
		return 0;

	if (fixed)
	{
		block_size = tdrive_get_block_length(tdrive);
		num_blocks = transfer_length;
	}
	else
	{
		block_size = transfer_length;
		num_blocks = 1;
	}

	start_ticks = ticks;
	retval = tape_cmd_read(tdrive->tape, ctio, block_size, num_blocks, fixed, &done_blocks, &ili_block_size, &compressed_size);
	if (unlikely(retval < 0)) {
		debug_warn("tape_partition_read failed\n");
		ctio_free_data(ctio);
		return 0;
	}

	TDRIVE_STATS_ADD(tdrive, read_bytes_processed, ctio->dxfer_len);
	if (compressed_size) {
		TDRIVE_STATS_ADD(tdrive, bytes_read_from_tape, compressed_size);
		TDRIVE_STATS_ADD(tdrive, compressed_bytes_read, compressed_size);
	}
	else
		TDRIVE_STATS_ADD(tdrive, bytes_read_from_tape, ctio->dxfer_len);
	TDRIVE_STATS_ADD(tdrive, read_ticks, (ticks - start_ticks));

	if (retval == 0)
		return 0;

	switch (retval) {
	case FILEMARK_ENCOUNTERED:
		tdrive_construct_filemark_sense(tdrive,
			ctio, fixed,
			num_blocks, done_blocks, block_size, ili_block_size);
		break;
	case SETMARK_ENCOUNTERED:
		tdrive_construct_setmark_sense(tdrive,
			ctio, fixed,
			num_blocks, done_blocks, block_size, ili_block_size);
		break;
	case EOD_REACHED:
		tdrive_construct_eod_sense(tdrive,
			ctio, fixed,
			num_blocks, done_blocks, block_size, ili_block_size);
		break;
	case UNDERLENGTH_COND_ENCOUNTERED:
		tdrive_construct_ili_sense(tdrive,
			ctio, fixed,
			num_blocks, done_blocks, block_size,
			sili, UNDERLENGTH_COND_ENCOUNTERED, ili_block_size);
		break;
	case OVERLENGTH_COND_ENCOUNTERED:
		tdrive_construct_ili_sense(tdrive,
			ctio, fixed,
			num_blocks, done_blocks, block_size,
			sili, OVERLENGTH_COND_ENCOUNTERED, ili_block_size);
		break;
	case MEDIA_ERROR:
		TDRIVE_STATS_ADD(tdrive, read_errors, 1);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR,
			SSD_KEY_HARDWARE_ERROR, 0,
			INTERNAL_TARGET_FAILURE_ASC,
			INTERNAL_TARGET_FAILURE_ASCQ);
	default:
		break;
	}
	return 0;
}

static int
__tdrive_cmd_read_block_limits_mloi(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	ctio_allocate_buffer(ctio, 20, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
		return -1;
	bzero(ctio->data_ptr, 20);

	*((uint64_t *)(ctio->data_ptr + 12)) = htobe64(0xFFFFFFFF);
	return 0;
}

static int
__tdrive_cmd_read_block_limits(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	struct drive_parameters params;

	ctio_allocate_buffer(ctio, READ_BLOCK_LIMITS_CMDLEN, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
		return -1;

	tdrive_init_drive_params(&params);
	ctio->scsi_status = SCSI_STATUS_OK;
	memcpy(ctio->data_ptr, &params, READ_BLOCK_LIMITS_CMDLEN);
	return 0;
}

static int
tdrive_cmd_read_block_limits(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t mloi;

	mloi = cdb[1] & 0x1;

	if (!mloi)
		return __tdrive_cmd_read_block_limits(tdrive, ctio);
	else
		return __tdrive_cmd_read_block_limits_mloi(tdrive, ctio);
}

static int
tdrive_cmd_read_position(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t service_action;
	uint16_t allocation_length;

	service_action = cdb[1] & 0x1F;
	switch (service_action) {
	case READ_POSITION_SHORT:
		allocation_length = 20;
		break;
	case READ_POSITION_LONG:
		allocation_length = 32;
		break;
	case READ_POSITION_EXTENDED:
		allocation_length = min_t(int, 32, be16toh(*(uint16_t *)(&cdb[7])));
		break;
	default:
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);
		return 0;
	}

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
		return -1;

	tdrive_empty_write_queue(tdrive);
	tape_cmd_read_position(tdrive->tape, ctio, service_action);
	/* send the ccb */
	return 0;
}

static int
density_code_match(int density_code, int sdensity_code, int vol_type)
{
	switch (vol_type)
	{
		case VOL_TYPE_LTO_1:
			return (density_code == DENSITY_ULTRIUM_1);
		case VOL_TYPE_LTO_2:
			return (density_code == DENSITY_ULTRIUM_2);
		case VOL_TYPE_LTO_3:
			return (density_code == DENSITY_ULTRIUM_3);
		case VOL_TYPE_LTO_4:
			return (density_code == DENSITY_ULTRIUM_4);
		case VOL_TYPE_LTO_5:
			return (density_code == DENSITY_ULTRIUM_5);
		case VOL_TYPE_LTO_6:
			return (density_code == DENSITY_ULTRIUM_6);
		case VOL_TYPE_DLT_4:
			return (density_code == DENSITY_DLT_4_DEFAULT);
		case VOL_TYPE_VSTAPE:
			return (density_code == DENSITY_VSTAPE_DEFAULT);
		case VOL_TYPE_SDLT_1:
			return (density_code == DENSITY_SUPER_DLT_1_DEFAULT);
		case VOL_TYPE_SDLT_2:
			return (density_code == DENSITY_SUPER_DLT_2_DEFAULT);
		default:
			break;
	}
	return 0;
}

static void
__tdrive_report_medium_descriptors(struct tdrive *tdrive, uint8_t *buffer, int allocation_length, int *ret_byte_count, int *ret_done, uint8_t media)
{
	struct medium_descriptor medium_desc;
	struct tape *tape = tdrive->tape;
	char *description;
	int done = *ret_done;
	int worm = tape->worm;
	int min_len;
	uint16_t medium_length = 0;
	uint8_t density = 0;

	bzero(&medium_desc, sizeof(medium_desc));
	if (worm)
		medium_desc.medium_type = 0x01;
	medium_desc.descriptor_length = htobe16(0x34);
	medium_desc.media_width = htobe16(127);
	sys_memset(medium_desc.medium_type_name, ' ', 8);
	sys_memset(medium_desc.description, ' ', 8);
	memcpy(medium_desc.assigning_organization, tdrive->inquiry.vendor_id, 8);
	if (!worm)
		memcpy(medium_desc.medium_type_name, "Data", strlen("Data"));
	else
		memcpy(medium_desc.medium_type_name, "WORM", strlen("WORM"));

	switch (tape->make) {
	case VOL_TYPE_LTO_1:
		density = DENSITY_ULTRIUM_1;
		medium_length = 609;
		if (!worm)
			description = "Ultrium 1 Data Tape";
		else
			description = "Ultrium 1 WORM Tape";
		break;
	case VOL_TYPE_LTO_2:
		density = DENSITY_ULTRIUM_2;
		medium_length = 609;
		if (!worm)
			description = "Ultrium 2 Data Tape";
		else
			description = "Ultrium 2 WORM Tape";
		break;
	case VOL_TYPE_LTO_3:
		density = DENSITY_ULTRIUM_3;
		medium_length = 680;
		if (!worm)
			description = "Ultrium 3 Data Tape";
		else
			description = "Ultrium 3 WORM Tape";
		break;
	case VOL_TYPE_LTO_4:
		density = DENSITY_ULTRIUM_4;
		medium_length = 820;
		if (!worm)
			description = "Ultrium 4 Data Tape";
		else
			description = "Ultrium 4 WORM Tape";
		break;
	case VOL_TYPE_LTO_5:
		density = DENSITY_ULTRIUM_5;
		medium_length = 846;
		if (!worm)
			description = "Ultrium 5 Data Tape";
		else
			description = "Ultrium 5 WORM Tape";
		break;
	case VOL_TYPE_LTO_6:
		density = DENSITY_ULTRIUM_6;
		medium_length = 846;
		if (!worm)
			description = "Ultrium 6 Data Tape";
		else
			description = "Ultrium 6 WORM Tape";
		break;
	}
	medium_desc.primary_density_code = density;
	medium_desc.media_length = htobe16(medium_length);
	memcpy(medium_desc.description, description, strlen(description));

	min_len = min_t(int, allocation_length, sizeof(medium_desc));
	memcpy(buffer, &medium_desc, min_len);
	done += min_len;
	*ret_done = done;
	*ret_byte_count = sizeof(medium_desc);
}

static void
__tdrive_report_density_descriptors(struct tdrive *tdrive, uint8_t *buffer, int allocation_length, int *ret_byte_count, int *ret_done, uint8_t media)
{
	struct density_descriptor *desc;
	int done = *ret_done, byte_count = 0;
	int min_len;

	SLIST_FOREACH(desc, &tdrive->density_list, d_list) {
		if (media && (density_code_match(desc->pdensity_code, desc->sdensity_code, tdrive->tape->make) == 0))
			continue;

		byte_count += 52;
		if (done < allocation_length) {
			min_len = min_t(int, 52, (allocation_length - done));

			memcpy(buffer, desc, min_len);
			buffer += min_len;
			done += min_len;
		}

		if (media)
			break;
	}
	*ret_done = done;
	*ret_byte_count = byte_count;
}

static int
tdrive_cmd_report_density_support(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t media, medium_type;
	uint16_t allocation_length;
	uint8_t *buffer;
	int done = 0;
	int byte_count = 0;
	struct density_header *header;

	media = READ_BIT(cdb[1], 0);
	medium_type = READ_BIT(cdb[1], 1);
	allocation_length = be16toh(*(uint16_t *)(&cdb[7]));

	if (allocation_length < sizeof(struct density_header))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
	{
		debug_warn("Cannot allocate for a new ctio\n");
		return -1;
	}

	buffer = ctio->data_ptr;
	done += sizeof(struct density_header);
	buffer += sizeof(struct density_header);

	if (!medium_type || !is_lto_tape(tdrive->tape))
		__tdrive_report_density_descriptors(tdrive, buffer, allocation_length, &byte_count, &done, media);
	else
		__tdrive_report_medium_descriptors(tdrive, buffer, allocation_length, &byte_count, &done, media);
 
	header = (struct density_header *)(ctio->data_ptr);
	if (byte_count)
		byte_count += 2;

	header->avail_len = htobe16(byte_count);
	ctio->dxfer_len = done;
	return 0;
}

static int
tdrive_cmd_rewind(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	int retval;

	tdrive_empty_write_queue(tdrive);
	retval = tape_cmd_rewind(tdrive->tape, 0);
	if (retval != 0) 
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_MEDIUM_ERROR, 0, SEQUENTIAL_POSITIONING_ERROR_ASC, SEQUENTIAL_POSITIONING_ERROR_ASCQ);
		return 0;	
	}

	ctio->scsi_status = SCSI_STATUS_OK;
	ctio->dxfer_len = 0;
	ctio->data_ptr = NULL;
	return retval;
}

static int
tdrive_cmd_space(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t code;
	int count, todo;
	int retval;

	tdrive_empty_write_queue(tdrive);
	code = (cdb[1] & 0x07);
	count = READ_24(cdb[2], cdb[3], cdb[4]);

	if (count > 0x7FFFFF)
	{
		count = TWOS_COMPLEMENT(count);
	}

	if (!count && code != SPACE_CODE_END_OF_DATA)
		return 0;

	switch (code) {
	case SPACE_CODE_BLOCKS:
		break;
	case SPACE_CODE_FILEMARKS:
		break;
	case SPACE_CODE_END_OF_DATA:
		break;
	case SPACE_CODE_SETMARKS:
		break;
	case SPACE_CODE_SEQUENTIAL_FILEMARKS:
	case SPACE_CODE_SEQUENTIAL_SETMARKS:
	default:
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	todo = count;
	retval = tape_cmd_space(tdrive->tape, code, &todo);
	if (retval == 0)
		return 0;

	if (retval < 0) {
		debug_warn("tape_cmd_space failed for code %d and count %d\n", code, count);
		return 0;
	}

	if (count < 0)
		count = -(count); /* todo is always returned positive */

	if (todo < 0)
		todo = -(todo);

	switch (retval) {
	case MEDIA_ERROR:
		ctio_construct_sense(ctio, 
			SSD_CURRENT_ERROR, SSD_KEY_MEDIUM_ERROR, 
			0, SEQUENTIAL_POSITIONING_ERROR_ASC, 
			SEQUENTIAL_POSITIONING_ERROR_ASCQ);
		break;
	case SETMARK_ENCOUNTERED:
		tdrive_construct_setmark_sense(tdrive,
			ctio, 1 /* fake fixed */,
			count, (count - todo), 0 /* dont care */, 0);
		break;
	case FILEMARK_ENCOUNTERED:
		tdrive_construct_filemark_sense(tdrive,
			ctio, 1 /* fake fixed */,
			count, (count - todo), 0 /* dont care */, 0);
		break;	
	case BOM_REACHED:
		tdrive_construct_bomp_sense(tdrive,
			ctio, 1 /* fake fixed */,
			count, (count - todo), 0 /* dont care */);
		break;
	case EOD_REACHED:
		tdrive_construct_eod_sense(tdrive,
			ctio, 1 /* fake fixed */,
			count, (count - todo), 0 /* dont care */, 0);
		break;
	default:
		debug_check(1);
		break;
	}
	return 0;
}

static int
tdrive_copy_supported_log_page_info(struct tdrive *tdrive, uint8_t *buffer, uint16_t allocation_length)
{
	struct scsi_log_page *page = (struct scsi_log_page *)buffer;
	uint8_t num_pages = 0;

	bzero(page, sizeof(*page));
	page->page_code = 0x00;
	page->page_length = tdrive->log_info.num_pages;

	for (num_pages = 0; num_pages < tdrive->log_info.num_pages; num_pages++)
		page->page_data[num_pages] = tdrive->log_info.page_code[num_pages];

	return min_t(int, allocation_length, tdrive->log_info.num_pages + sizeof(struct scsi_log_page));
}

static int
tdrive_log_page_supported(struct tdrive *tdrive, uint8_t page_code)
{
	int i;

	for (i = 0; i < tdrive->log_info.num_pages; i++)
	{
		if (tdrive->log_info.page_code[i] == page_code)
		{
			return 1;
		}
	}
	return 0;
}

static int
tdrive_copy_read_attributes(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint32_t allocation_length, uint32_t first_attribute, struct tape_partition *partition)
{
	uint32_t done = 0;
	uint32_t avail = 0;
	struct read_attribute *attr;
	uint8_t *buffer;
	struct mam_attribute *mam_attr;
	int mam_attr_len, min_len;
	int i;

	if (allocation_length < 4)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	if (atomic_test_bit(PARTITION_MAM_CORRUPT, &partition->flags)) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_MEDIUM_ERROR, 0, AUXILIARY_MEMORY_READ_ERROR_ASC, AUXILIARY_MEMORY_READ_ERROR_ASCQ);
		return 0;
	}

	mam_attr = tape_partition_mam_get_attribute(partition, first_attribute);
	if (!mam_attr) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
	{
		/* Memory allocation failure */
		debug_warn("Unable to allocate for allocation length %d\n", allocation_length);
		return -1;
	}
	buffer = ctio->data_ptr;

	done = 4;

	tape_partition_update_mam(partition, first_attribute);
	for (i = 0; i < MAX_MAM_ATTRIBUTES; i++) {
		mam_attr = &partition->mam_attributes[i];
		if (i && !mam_attr->identifier)
			break;
		if (mam_attr->identifier == 0xFFFF)
			continue;
		if (!mam_attr->valid)
			continue;
		if (first_attribute > mam_attr->identifier)
			continue;
		mam_attr_len = mam_attr->length + 5;
		avail += mam_attr_len;
		attr = (struct read_attribute *)(buffer+done);
		if ((done + 5) <= allocation_length) {
			attr->identifier = htobe16(mam_attr->identifier);
			attr->format = mam_attr->format;
			attr->length = htobe16(mam_attr->length);
			done += 5;
		}
		if ((allocation_length - done) > 0) {
			min_len = min_t(int, allocation_length - done, mam_attr->length);
			memcpy(attr->value, mam_attr->value, min_len);
			done += min_len;
		}
	}

	bzero(buffer, 4);
	*((uint32_t *)buffer) = htobe32(avail);
	ctio->dxfer_len = done;
	return 0;
}

static int
tdrive_copy_read_attribute_list(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint32_t allocation_length, struct tape_partition *partition)
{
	struct mam_attribute *mam_attr;
	uint32_t done = 0;
	uint32_t avail = 0;
	uint8_t *buffer;
	int i;

	if (allocation_length < 4) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (unlikely(!ctio->data_ptr)) {
		/* Memory allocation failure */
		debug_warn("Unable to allocate for allocation length %d\n", allocation_length);
		return -1;
	}
	buffer = ctio->data_ptr;

	done = 4;

	for (i = 0; i < MAX_MAM_ATTRIBUTES; i++) {
		mam_attr = &partition->mam_attributes[i];
		if (i && !mam_attr->identifier)
			break;
		if (mam_attr->identifier == 0xFFFF)
			continue;
		if (!mam_attr->valid)
			continue;
		if ((done + 2) <= allocation_length) {
			*((uint16_t *)buffer+done) = htobe16(mam_attr->identifier);
			done += 2;
		}
		avail += 2;
	}

	bzero(buffer, 4);
	*((uint32_t *)buffer) = htobe32(avail);
	ctio->dxfer_len = done;
	return 0;
}

static int
tdrive_cmd_write_attribute(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	struct tape_partition *partition;
	uint8_t *cdb = ctio->cdb;
	struct mam_attribute *mam_attr;
	uint8_t partition_id;
	uint32_t parameter_list_length;
	uint32_t parameter_length, todo;
	struct read_attribute *attr;
	uint8_t *ptr;
	int attr_length;

	parameter_list_length = be32toh(*(uint32_t *)(&cdb[10]));
	if (!parameter_list_length)
		return 0;

	parameter_length = be32toh(*((uint32_t *)ctio->data_ptr));
	if ((parameter_length + 4) > parameter_list_length) {
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	partition_id = cdb[7];
	partition = tape_get_partition(tdrive->tape, partition_id);
	if (!partition) {
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	if (atomic_test_bit(PARTITION_MAM_CORRUPT, &partition->flags)) {
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_MEDIUM_ERROR, 0, AUXILIARY_MEMORY_WRITE_ERROR_ASC, AUXILIARY_MEMORY_WRITE_ERROR_ASCQ);
		return 0;
	}

	ptr = ctio->data_ptr + 4;
	todo = parameter_length;

	while (todo > 5) {
		attr = (struct read_attribute *)ptr;
		todo -= 5;
		ptr += 5;
		attr_length = be16toh(attr->length);
		if (todo < attr_length)
			break;
		mam_attr = tape_partition_mam_get_attribute(partition, be16toh(attr->identifier));
		if (!mam_attr || (mam_attr->format & 0x80) || (mam_attr->format != attr->format) || !mam_attr_length_valid(attr, mam_attr)) {
			ctio_free_data(ctio);
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
			return 0;
		} 
		ptr += attr_length;
		todo -= attr_length;
	}

	ptr = ctio->data_ptr + 4;
	todo = parameter_length;

	while (todo > 5) {
		attr = (struct read_attribute *)ptr;
		todo -= 5;
		ptr += 5;
		attr_length = be16toh(attr->length);
		if (todo < attr_length)
			break;
		mam_attr = tape_partition_mam_get_attribute(partition, be16toh(attr->identifier));
		if (!attr_length) {
			mam_attr->valid = 0;
		}
		else {
			memcpy(mam_attr->value, attr->value, attr_length);
			mam_attr->valid = 1;
			mam_attr_set_length(attr, mam_attr);
			mam_attr->raw_attr->length = mam_attr->length;
		}
		mam_attr->raw_attr->valid = mam_attr->valid;

		ptr += attr_length;
		todo -= attr_length;
	}

	ctio_free_data(ctio);
	return tape_partition_write_mam(partition);
}

static int
tdrive_cmd_read_attribute(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	struct tape_partition *partition;
	uint8_t *cdb = ctio->cdb;
	uint8_t service_action, partition_id;
	uint16_t first_attribute;
	uint32_t allocation_length;
	int retval = 0;

	service_action = cdb[1] & 0x1F;
	first_attribute = be16toh(*(uint16_t *)(&cdb[8]));
	allocation_length = be32toh(*((uint32_t *)(&cdb[10])));
	partition_id = cdb[7];

	partition = tape_get_partition(tdrive->tape, partition_id);
	if (!partition) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	switch (service_action)
	{
		case SERVICE_ACTION_READ_ATTRIBUTES:
			retval = tdrive_copy_read_attributes(tdrive, ctio, allocation_length, first_attribute, partition);
			break;
		case SERVICE_ACTION_READ_ATTRIBUTE_LIST:
			retval = tdrive_copy_read_attribute_list(tdrive, ctio, allocation_length, partition);
			break;
	}
	return retval;
}

static int
tdrive_cmd_log_sense6(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t sp;
	uint8_t page_code;
	uint16_t parameter_pointer, allocation_length, max_allocation_length;
	uint16_t page_length;

	sp = READ_BIT(cdb[1], 0);
	page_code = (cdb[2] & 0x3F); /* ??? check the mask */

	parameter_pointer = be16toh(*(uint16_t *)(&cdb[5]));
	allocation_length = be16toh(*(uint16_t *)(&cdb[7]));

	if (sp) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	max_allocation_length = max_t(uint16_t, 64, allocation_length);
	ctio_allocate_buffer(ctio, max_allocation_length, Q_WAITOK);
	if (unlikely(!ctio->data_ptr)) {
		/* Memory allocation failure */
		debug_warn("Unable to allocate for allocation length %d\n", allocation_length);
		return -1;
	}

	bzero(ctio->data_ptr, ctio->dxfer_len);

	switch (page_code) {
	case 0x00:
		page_length = tdrive_copy_supported_log_page_info(tdrive, ctio->data_ptr, allocation_length);
		break;
	default:
		if (!tdrive->handlers.additional_log_sense || !tdrive_log_page_supported(tdrive, page_code)) {
			ctio_free_data(ctio);
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
			return 0;
		}
		page_length = (*tdrive->handlers.additional_log_sense)(tdrive, page_code, ctio->data_ptr, allocation_length, parameter_pointer);
	}

	if (!page_length)
		ctio_free_data(ctio);
	ctio->dxfer_len = page_length;
	return 0;
}

void
tdrive_update_mode_header(struct tdrive *tdrive, struct mode_parameter_header6 *header)
{
	uint8_t buffered_mode;

	buffered_mode = (header->device_specific_parameter >> 4) & 0x07;

	if (buffered_mode)
	{
		TDRIVE_SET_BUFFERED_MODE(tdrive);
	}
	else
	{
		TDRIVE_SET_NON_BUFFERED_MODE(tdrive);
	}
}

void
tdrive_update_mode_header10(struct tdrive *tdrive, struct mode_parameter_header10 *header)
{
	uint8_t buffered_mode;

	buffered_mode = (header->device_specific_parameter >> 4) & 0x07;

	if (buffered_mode)
	{
		TDRIVE_SET_BUFFERED_MODE(tdrive);
	}
	else
	{
		TDRIVE_SET_NON_BUFFERED_MODE(tdrive);
	}
}

static int
tdrive_update_block_descriptor(struct tdrive *tdrive, uint8_t *data, int data_len)
{
	uint32_t block_length;

	if (data_len < 8) {
		debug_warn("data_len less than min 8 bytes\n");
		return -1;
	}

	block_length = READ_24(data[5], data[6], data[7]);

	/* Keep to a minimum of the tape tdrive sector size */
	if (block_length && block_length > TDRIVE_MAX_BLOCK_SIZE) {
		debug_warn("Invalid block length passed %u\n", block_length);
		return -1;
	}

	tdrive_wait_for_write_queue(tdrive);
	/* Keep in big endian format for memcpys which are done */
	memcpy(tdrive->block_descriptor.block_length, data + 5, 3);
	return 0;
}

static int
update_device_configuration_page(struct tdrive *tdrive, uint8_t *data, int data_len)
{
	uint8_t sew;
	uint8_t *olddata = (uint8_t *)&tdrive->configuration_page;

	if (data[1] != 0x0E)
		return 0;

	sew = (data[10] & 0x8 >> 3);
	if (sew)
	{
		olddata[10] = olddata[10] | 0x08;
	}
	else
	{
		olddata[10] = olddata[10] & ~0x08;
	}
	tdrive->configuration_page.select_data_compression_algorithm = data[14];
	return 0;
}

static int
update_medium_partition_page(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t *data, int data_len)
{
	struct medium_partition_page *new = (struct medium_partition_page *)data;
	struct medium_partition_page *page = &tdrive->partition_page;
	struct tape *tape = tdrive->tape;
	uint64_t size = 0, fill_to_max = 0;
	int i;
	int len;

	if (!tape) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, MEDIUM_NOT_PRESENT_ASC, MEDIUM_NOT_PRESENT_ASCQ);
		return -1;
	}

	len = offsetof(struct medium_partition_page, partition_size);
	if (data_len < len) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
		return -1;
	}

	if (new->partition_units && (new->partition_units & 0xF) < 9) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
		return -1;
	}

	if (!new->partition_units && new->addl_partitions_defined > 1) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
		return -1;
	}

	if (new->addl_partitions_defined > get_max_additional_partitions(tape)) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
		return -1;
	}

	if (((new->fdp >> 3) & 0x3) != 0x3) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
		return -1;
	}

	len += ((new->addl_partitions_defined + 1) * 2);
	if (data_len < len) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
		return -1;
	}

	if (!new->partition_units)
		goto skip_size_check;

	for (i = 0; i < new->addl_partitions_defined + 1; i++) {
		if (new->partition_size[i] == 0xFFFF) {
			if (fill_to_max || i > 1) {
				ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
				return -1;
			}
			fill_to_max = 1;
			continue;
		}
		size += partition_size_from_units(be16toh(new->partition_size[i]), new->partition_units & 0xF); 
	}

	if (fill_to_max)
		goto skip_size_check;
 
	if (size > tape->set_size) {
		/* Should be volume overflow and checked in format medium */
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_VALUE_INVALID_ASC, PARAMETER_VALUE_INVALID_ASCQ);  
		return -1;
	}

skip_size_check:
	page->page_length = len;
	page->partition_units = (new->partition_units & 0xF);
	page->addl_partitions_defined = new->addl_partitions_defined;
	page->fdp &= ~(0x1 << 2); /* Clear POFM */
	page->fdp &= ~0x80; /* Clear FDP */
	page->fdp &= ~0x40; /* Clear SDP */
	page->fdp &= ~0x20; /* Clear IDP */
	if (new->fdp & 0x20)
		page->fdp |= 0x20;
	else if (new->fdp & 0x40)
		page->fdp |= 0x40;
	else if (new->fdp & 0x80)
		page->fdp |= 0x80;

	for (i = 0; i < new->addl_partitions_defined + 1; i++) {
		page->partition_size[i] = new->partition_size[i];
	}

	return 0;
}

static int
update_data_compression_page(struct tdrive *tdrive, uint8_t *data, int data_len)
{
	struct data_compression_page *new = (struct data_compression_page *)data;
	uint8_t dce;
	uint8_t dcc;

	/* Only thing from the data compression page is the dce bit */
	dce = new->dcc & 0x80;
	dcc = tdrive->compression_page.dcc & 0x7F;
	tdrive->compression_page.dcc = (dcc | dce);
	return 0;
}


static int
tdrive_cmd_log_select6(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t sp, pcr;
	uint16_t parameter_length;

	sp = READ_BIT(cdb[1], 0);
	pcr = READ_BIT(cdb[1], 1);
	parameter_length = be16toh(*(uint16_t *)(&cdb[7]));

	/* No saved parameters supported */
	if ((pcr && parameter_length) || sp)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	ctio->data_ptr = NULL;
	ctio->dxfer_len = 0;
	ctio->scsi_status = SCSI_STATUS_OK;

	if (pcr)
		return 0;

	/* The rest is to be implemented yet */
	return 0;
}

static uint16_t
get_mode_page_length(uint8_t *data)
{
	uint8_t page_code;
	uint16_t page_length;

	page_code = data[0] & 0x3F;
	switch (page_code) {
		case DATA_COMPRESSION_PAGE:
		case MEDIUM_PARTITION_PAGE:
			return data[1] + 2;
		case DEVICE_CONFIGURATION_PAGE:
			page_length = data[1]; 
			if (page_length == 0x0E)
				return page_length + 2;
			page_length = be16toh(*(uint16_t *)(&data[2]));
			return (page_length + 4);
		case CONTROL_MODE_PAGE:
			page_length = data[1]; 
			if (page_length == 0x0A)
				return page_length + 2;
			page_length = be16toh(*(uint16_t *)(&data[2]));
			return (page_length + 4);
	}
	return (data[1] + 2);
}

static int
tdrive_cmd_mode_select6(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	int todo = ctio->dxfer_len;
	uint8_t *data;
	int done = 0;
	uint8_t page_code;
	uint16_t page_length;
	struct mode_parameter_header6 *header; 

	if (todo < (sizeof (struct mode_parameter_header6)))
	{
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;

	}

	header = (struct mode_parameter_header6 *)ctio->data_ptr;
	tdrive_update_mode_header(tdrive, header);
	todo -= sizeof(struct mode_parameter_header6);
	done += sizeof(struct mode_parameter_header6);

	data = ctio->data_ptr+done; 
	/* For now ignore block descriptors */
	if (header->block_descriptor_length)
	{
		int retval;

		retval = tdrive_update_block_descriptor(tdrive, data, header->block_descriptor_length);

		if (retval < 0)
		{
			ctio_free_data(ctio);
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
			return 0;
		}
	}

	todo -= header->block_descriptor_length;
	done += header->block_descriptor_length;

	while (todo > 4) {

		data = ctio->data_ptr+done;
		page_code = data[0] & 0x3F;
		page_length = get_mode_page_length(data);
		if (page_length > todo)
			break;

		switch (page_code) {
			case DATA_COMPRESSION_PAGE:
				update_data_compression_page(tdrive, data, todo);
				break;
			case MEDIUM_PARTITION_PAGE:
				update_medium_partition_page(tdrive, ctio, data, todo);
				break;
			case DEVICE_CONFIGURATION_PAGE:
				update_device_configuration_page(tdrive, data, todo);
			default:
				break;
		}
		todo -= page_length;
		done += page_length;
	}

	ctio_free_data(ctio);
	return 0;
}

static int
tdrive_cmd_mode_select10(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	int todo = ctio->dxfer_len;
	uint8_t *data;
	int done = 0;
	struct mode_parameter_header10 *header; 

	if (todo < (sizeof (struct mode_parameter_header10)))
	{
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;

	}

	header = (struct mode_parameter_header10 *)ctio->data_ptr;
	tdrive_update_mode_header10(tdrive, header);
	todo -= sizeof(struct mode_parameter_header10);
	done += sizeof(struct mode_parameter_header10);

	data = ctio->data_ptr+done; 
	/* For now ignore block descriptors */
	if (header->block_descriptor_length)
	{
		int retval;

		retval = tdrive_update_block_descriptor(tdrive, data, be16toh(header->block_descriptor_length));

		if (retval < 0)
		{
			ctio_free_data(ctio);
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
			return 0;
		}
	}

	todo -= be16toh(header->block_descriptor_length);
	done += be16toh(header->block_descriptor_length);

	while (todo > 4)
	{
		uint8_t page_code, page_length;

		data = ctio->data_ptr+done;
		page_code = data[0] & 0x3F;
		page_length = get_mode_page_length(data);
		if (page_length > todo)
			break;

		switch (page_code)
		{
			case DATA_COMPRESSION_PAGE:
				update_data_compression_page(tdrive, data, todo);
				break;
			case MEDIUM_PARTITION_PAGE:
				update_medium_partition_page(tdrive, ctio, data, todo);
				break;
			case DEVICE_CONFIGURATION_PAGE:
				update_device_configuration_page(tdrive, data, todo);
			default:
				break;
		}
		todo -= page_length;
		done += page_length;
	}

	ctio_free_data(ctio);
	return 0;
}

static void
copy_changeable_medium_configuration_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct medium_configuration_page page;

	bzero(&page, sizeof(page));
	page.page_code = MEDIUM_CONFIGURATION_PAGE;
	page.page_length = 0x1E;
	memcpy(buffer, &page, min_len);
}

static void
copy_current_medium_configuration_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct medium_configuration_page page;

	bzero(&page, sizeof(page));
	page.page_code = MEDIUM_CONFIGURATION_PAGE;
	page.page_length = 0x1E;
	if (tdrive->tape && tdrive_tape_loaded(tdrive)) {
		if (tdrive->tape->worm)
			page.wormm = 0x01;
		page.worm_mode_label_restrictions = 0x01;
		page.worm_mode_filemark_restrictions = 0x02;
	}
	memcpy(buffer, &page, min_len);
}

static void
copy_changeable_rw_error_recovery_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct rw_error_recovery_page page;

	bzero(&page, sizeof(page));
	page.page_code = READ_WRITE_ERROR_RECOVERY_PAGE;
	page.page_length = sizeof(struct rw_error_recovery_page) - offsetof(struct rw_error_recovery_page, dcr);
	memcpy(buffer, &page, min_len);
}

static void
copy_current_rw_error_recovery_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	memcpy(buffer, &tdrive->rw_recovery_page, min_len);
}

static void
copy_changeable_disconnect_reconnect_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct disconnect_reconnect_page page;

	bzero(&page, sizeof(page));
	page.page_code = DISCONNECT_RECONNECT_PAGE;
	page.page_length = sizeof(struct disconnect_reconnect_page) - offsetof(struct disconnect_reconnect_page, buffer_full_ratio);
	memcpy(buffer, &page, min_len);
}

static void
copy_current_disconnect_reconnect_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	memcpy(buffer, &tdrive->disreconn_page, min_len);
}

struct control_mode_page control_mode_page = {
	.page_code = CONTROL_MODE_PAGE,
	.page_length = 0x0A,
	.tas = 0x40,
};

static void
copy_changeable_control_mode_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct control_mode_page page;

	bzero(&page, sizeof(page));
	page.page_code = CONTROL_MODE_PAGE;
	page.page_length = 0x0A,
	memcpy(buffer, &page, min_len);
}

static void
copy_current_control_mode_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	memcpy(buffer, &control_mode_page, min_len);
}

struct control_mode_dp_page control_mode_dp_page = {
	.page_code = CONTROL_MODE_PAGE,
	.sub_page_code = CONTROL_MODE_DATA_PROTECTION_PAGE,
	.lbp_info_length = 0x04,
};

static void
copy_changeable_control_mode_dp_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct control_mode_dp_page page;

	bzero(&page, sizeof(page));
	page.page_code = CONTROL_MODE_PAGE;
	page.sub_page_code = CONTROL_MODE_DATA_PROTECTION_PAGE;
	page.page_length = htobe16(0x1C),
	memcpy(buffer, &page, min_len);
}

static void
copy_current_control_mode_dp_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	control_mode_dp_page.page_length = htobe16(0x1C),
	memcpy(buffer, &control_mode_dp_page, min_len);
}

static void
copy_changeable_device_configuration_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct device_configuration_page page;

	bzero(&page, sizeof(page));
	page.page_code = DEVICE_CONFIGURATION_PAGE;
	page.page_length = 0xE;
	page.sew = 0x08;
	page.select_data_compression_algorithm = 0xFF;
	memcpy(buffer, &page, min_len);
}

static void
copy_current_device_configuration_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct tape *tape;

	tape = tdrive->tape;
	if (tape && tdrive_tape_loaded(tdrive))
		tdrive->configuration_page.active_partition = tape->cur_partition->partition_id;
	else
		tdrive->configuration_page.active_partition = 0;
	memcpy(buffer, &tdrive->configuration_page, min_len);
}

static void
copy_changeable_device_configuration_ext_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct device_configuration_ext_page page;

	bzero(&page, sizeof(page));
	page.page_code = DEVICE_CONFIGURATION_PAGE;
	page.sub_page_code = DEVICE_CONFIGURATION_EXTENSION_PAGE;
	page.page_length = htobe16(0x1C);
	memcpy(buffer, &page, min_len);
}

static void
copy_current_device_configuration_ext_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	memcpy(buffer, &tdrive->configuration_ext_page, min_len);
}

static int
copy_changeable_medium_partition_page(struct tdrive *tdrive, uint8_t *buffer, int todo)
{
	struct medium_partition_page page;
	int min_len = min_t(int, todo, tdrive->partition_page.page_length + 2);

	bzero(&page, sizeof(page));
	page.page_code = MEDIUM_PARTITION_PAGE;
	page.page_length = sizeof(page) - 2;
	page.addl_partitions_defined = 0xFF;
	page.fdp = 0xFC;
	page.partition_units = 0xF;
	sys_memset(page.partition_size, 0xFF, sizeof(page.partition_size));

	memcpy(buffer, &page, min_len);
	return min_len;
}

static int 
copy_current_medium_partition_page(struct tdrive *tdrive, uint8_t *buffer, int todo)
{
	int min_len = min_t(int, todo, tdrive->partition_page.page_length + 2);

	memcpy(buffer, &tdrive->partition_page, min_len);
	return min_len;
}

static void
copy_changeable_data_compression_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	struct data_compression_page page;

	bzero(&page, sizeof(page));
	page.page_code = DATA_COMPRESSION_PAGE;
	page.page_length = sizeof(struct data_compression_page) - offsetof(struct data_compression_page, dcc);
	page.dcc |= 0x80; /* Data compression enable modifyable */
}

static void
copy_current_data_compression_page(struct tdrive *tdrive, uint8_t *buffer, int min_len)
{
	memcpy(buffer, &tdrive->compression_page, min_len);
}

int 
tdrive_copy_vital_product_page_info(struct tdrive *tdrive, uint8_t *buffer, uint16_t allocation_length)
{
	struct vital_product_page *page = (struct vital_product_page *)buffer;
	uint8_t num_pages = 0;

	bzero(page, sizeof(*page));
	page->device_type = T_SEQUENTIAL;
	page->page_code = 0x00;
	page->page_length = tdrive->evpd_info.num_pages;

	for (num_pages = 0; num_pages < tdrive->evpd_info.num_pages; num_pages++)
	{
		page->page_type[num_pages] = tdrive->evpd_info.page_code[num_pages];
	}

	return min_t(int, allocation_length, (tdrive->evpd_info.num_pages + sizeof(struct vital_product_page)));
}	

static int
mode_sense_current_values(struct tdrive *tdrive, uint8_t *buffer, uint16_t allocation_length, uint8_t dbd, uint8_t page_code, uint8_t sub_page_code, int *start_offset)
{
	int offset = *start_offset;
	int avail = 0;
	int min_len;

	if (page_code == ALL_PAGES || page_code == READ_WRITE_ERROR_RECOVERY_PAGE)
	{
		min_len = min_t(int, sizeof(struct rw_error_recovery_page), allocation_length - offset);
		if (min_len > 0)
		{
			copy_current_rw_error_recovery_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct rw_error_recovery_page);
	}
	if (page_code == ALL_PAGES || page_code == DISCONNECT_RECONNECT_PAGE)
	{
		min_len = min_t(int, sizeof(struct disconnect_reconnect_page), allocation_length - offset); 
		if (min_len > 0)
		{
			copy_current_disconnect_reconnect_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct disconnect_reconnect_page);
	}

	if (page_code == ALL_PAGES || page_code == CONTROL_MODE_PAGE)
	{
		if (!sub_page_code) {
			min_len = min_t(int, sizeof(struct control_mode_page), allocation_length - offset); 
			if (min_len > 0) {
				copy_current_control_mode_page(tdrive, buffer+offset, min_len);
				offset += min_len;
			}
			avail += sizeof(struct control_mode_page);
		}
		else if (sub_page_code == CONTROL_MODE_DATA_PROTECTION_PAGE) {
			min_len = min_t(int, sizeof(struct control_mode_dp_page), allocation_length - offset); 
			if (min_len > 0) {
				copy_current_control_mode_dp_page(tdrive, buffer+offset, min_len);
				offset += min_len;
			}
			avail += sizeof(struct control_mode_dp_page);
		}
	}

	if (page_code == ALL_PAGES || page_code == DATA_COMPRESSION_PAGE) {
		min_len = min_t(int, sizeof(struct data_compression_page), allocation_length - offset); 
		if (min_len > 0) {
			copy_current_data_compression_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct data_compression_page);
	}

	if (page_code == ALL_PAGES || page_code == DEVICE_CONFIGURATION_PAGE) {
		if (!sub_page_code) {
			min_len = min_t(int, sizeof(struct device_configuration_page), allocation_length - offset); 
			if (min_len > 0) {
				copy_current_device_configuration_page(tdrive, buffer+offset, min_len);
				offset += min_len;
			}
			avail += sizeof(struct device_configuration_page);
		}
		else if (sub_page_code == DEVICE_CONFIGURATION_EXTENSION_PAGE) {
			min_len = min_t(int, sizeof(struct device_configuration_ext_page), allocation_length - offset); 
			if (min_len > 0) {
				copy_current_device_configuration_ext_page(tdrive, buffer+offset, min_len);
				offset += min_len;
			}
			avail += sizeof(struct device_configuration_ext_page);
		}
	}

	if (page_code == ALL_PAGES || page_code == MEDIUM_PARTITION_PAGE) {

		min_len = min_t(int, sizeof(struct medium_partition_page), allocation_length - offset); 
		if (min_len > 0)
		{
			min_len = copy_current_medium_partition_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += tdrive->partition_page.page_length + 2;
	}

	if (page_code == ALL_PAGES || page_code == MEDIUM_CONFIGURATION_PAGE) {

		min_len = min_t(int, sizeof(struct medium_configuration_page), allocation_length - offset); 
		if (min_len > 0) {
			copy_current_medium_configuration_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct medium_configuration_page);
	}

	*start_offset = offset;
	return avail;
}

static int
mode_sense_changeable_values(struct tdrive *tdrive, uint8_t *buffer, uint16_t allocation_length, uint8_t dbd, uint8_t page_code, uint8_t sub_page_code, int *start_offset)
{
	int offset = *start_offset;
	int avail = 0;
	int min_len;

	bzero(buffer, allocation_length);

	if (page_code == ALL_PAGES || page_code == READ_WRITE_ERROR_RECOVERY_PAGE)
	{
		min_len = min_t(int, sizeof(struct rw_error_recovery_page), allocation_length - offset);
		if (min_len > 0)
		{
			copy_changeable_rw_error_recovery_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct rw_error_recovery_page);
	}
	if (page_code == ALL_PAGES || page_code == DISCONNECT_RECONNECT_PAGE)
	{
		min_len = min_t(int, sizeof(struct disconnect_reconnect_page), allocation_length - offset); 
		if (min_len > 0)
		{
			copy_changeable_disconnect_reconnect_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct disconnect_reconnect_page);
	}

	if (page_code == ALL_PAGES || page_code == CONTROL_MODE_PAGE)
	{
		if (!sub_page_code) {
			min_len = min_t(int, sizeof(struct control_mode_page), allocation_length - offset); 
			if (min_len > 0) {
				copy_changeable_control_mode_page(tdrive, buffer+offset, min_len);
				offset += min_len;
			}
			avail += sizeof(struct control_mode_page);
		}
		else if (sub_page_code == CONTROL_MODE_DATA_PROTECTION_PAGE) {
			min_len = min_t(int, sizeof(struct control_mode_dp_page), allocation_length - offset); 
			if (min_len > 0) {
				copy_changeable_control_mode_dp_page(tdrive, buffer+offset, min_len);
				offset += min_len;
			}
			avail += sizeof(struct control_mode_dp_page);
		}
	}

	if (page_code == ALL_PAGES || page_code == DATA_COMPRESSION_PAGE) {
		min_len = min_t(int, sizeof(struct data_compression_page), allocation_length - offset); 
		if (min_len > 0) {
			copy_changeable_data_compression_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct data_compression_page);
	}

	if (page_code == ALL_PAGES || page_code == DEVICE_CONFIGURATION_PAGE) {
		if (!sub_page_code) {
			min_len = min_t(int, sizeof(struct device_configuration_page), allocation_length - offset); 
			if (min_len > 0) {
				copy_changeable_device_configuration_page(tdrive, buffer+offset, min_len);
				offset += min_len;
			}
			avail += sizeof(struct device_configuration_page);
		}
		else if (sub_page_code == DEVICE_CONFIGURATION_EXTENSION_PAGE) {
			min_len = min_t(int, sizeof(struct device_configuration_ext_page), allocation_length - offset); 
			if (min_len > 0) {
				copy_changeable_device_configuration_ext_page(tdrive, buffer+offset, min_len);
				offset += min_len;
			}
			avail += sizeof(struct device_configuration_ext_page);
		}
	}

	if (page_code == ALL_PAGES || page_code == MEDIUM_PARTITION_PAGE) {

		min_len = min_t(int, sizeof(struct medium_partition_page), allocation_length - offset); 
		if (min_len > 0) {
			min_len = copy_changeable_medium_partition_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += tdrive->partition_page.page_length + 2;
	}

	if (page_code == ALL_PAGES || page_code == MEDIUM_CONFIGURATION_PAGE) {

		min_len = min_t(int, sizeof(struct medium_configuration_page), allocation_length - offset); 
		if (min_len > 0) {
			copy_changeable_medium_configuration_page(tdrive, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct medium_configuration_page);
	}

	*start_offset = offset;
	return avail;
}

static int
mode_sense_default_values(struct tdrive *tdrive, uint8_t *buffer, uint16_t allocation_length, uint8_t dbd, uint8_t page_code, uint8_t sub_page_code, int *start_offset)
{
	return mode_sense_current_values(tdrive, buffer, allocation_length, dbd, page_code, sub_page_code, start_offset);
}

static int
mode_sense_saved_values(struct tdrive *tdrive, uint8_t *buffer, uint16_t allocation_length, uint8_t dbd, uint8_t page_code, uint8_t sub_page_code, int *start_offset)
{
	return mode_sense_current_values(tdrive, buffer, allocation_length, dbd, page_code, sub_page_code, start_offset);
}

static int
mode_sense_block_descriptor(struct tdrive *tdrive, uint8_t *buffer, uint16_t allocation_length, int *start_offset)
{
	int offset = *start_offset;
	int min_len;

	min_len = min_t(int, sizeof(struct mode_parameter_block_descriptor), allocation_length - offset);
	if (min_len > 0)
	{
		memcpy(buffer+offset, &tdrive->block_descriptor, min_len);
		offset += min_len;
	}

	*start_offset = offset;
	return 0;
}

static int
tdrive_cmd_mode_sense10(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t dbd;
	uint8_t pc, page_code, sub_page_code;
	uint16_t allocation_length;
	int offset;
	struct mode_parameter_header10 *header;
	int avail;

	dbd = READ_BIT(cdb[1], 3);
	pc = cdb[2] >> 6;
	page_code = (cdb[2] & 0x3F);
	sub_page_code = cdb[3];

	allocation_length = be16toh(*(uint16_t *)(&cdb[7]));
	if (!allocation_length)
		return 0;

	if (allocation_length < (sizeof(struct mode_parameter_header10) - offsetof(struct mode_parameter_header10, medium_type)))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
	{
		return -1;
	}

	bzero(ctio->data_ptr, allocation_length);
	/* Check if we can atleast send back the mode parameter header */
	header = (struct mode_parameter_header10 *)ctio->data_ptr;
	offset = min_t(int, allocation_length, sizeof(struct mode_parameter_header10));
 	avail = sizeof(struct mode_parameter_header10);
	if (!dbd)
	{
		mode_sense_block_descriptor(tdrive, ctio->data_ptr, allocation_length, &offset);
		header->block_descriptor_length = htobe16(sizeof(struct mode_parameter_block_descriptor));
		avail += sizeof(struct mode_parameter_block_descriptor); 
	}
	else
	{
		header->block_descriptor_length = 0;
	}

	switch (pc) {
		case MODE_SENSE_CURRENT_VALUES:
			avail += mode_sense_current_values(tdrive, ctio->data_ptr, allocation_length, dbd, page_code, sub_page_code, &offset);
			break;
		case MODE_SENSE_CHANGEABLE_VALUES:
			avail += mode_sense_changeable_values(tdrive, ctio->data_ptr, allocation_length, dbd, page_code, sub_page_code, &offset);
			break;
		case MODE_SENSE_DEFAULT_VALUES:
			avail += mode_sense_default_values(tdrive, ctio->data_ptr, allocation_length, dbd, page_code, sub_page_code, &offset);
			break;
		case MODE_SENSE_SAVED_VALUES:
			ctio_free_data(ctio);
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, SAVING_PARAMETERS_NOT_SUPPORTED_ASC, SAVING_PARAMETERS_NOT_SUPPORTED_ASCQ);
			return 0;
	}

	header->mode_data_length = htobe16(avail - offsetof(struct mode_parameter_header10, medium_type));
	if (tdrive->tape && tdrive_tape_loaded(tdrive))
		header->medium_type = tdrive->mode_header.medium_type;
	header->device_specific_parameter = tdrive->mode_header.wp;
	ctio->dxfer_len = offset;
	return 0;
}

static int
tdrive_cmd_mode_sense6(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t dbd;
	uint8_t pc, page_code, sub_page_code;
	uint8_t allocation_length;
	int offset;
	struct mode_parameter_header6 *header;
	int avail;

	dbd = READ_BIT(cdb[1], 3);
	pc = cdb[2] >> 6;
	page_code = (cdb[2] & 0x3F);
	sub_page_code = cdb[3];

	allocation_length = cdb[4];
	if (!allocation_length)
	{
		return 0;
	}

	if (allocation_length < (sizeof(struct mode_parameter_header6) - offsetof(struct mode_parameter_header6, medium_type)))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}


	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
	{
		return -1;
	}

	bzero(ctio->data_ptr, allocation_length);
	/* Check if we can atleast send back the mode parameter header */
	header = (struct mode_parameter_header6 *)ctio->data_ptr;
	offset = min_t(int, allocation_length, sizeof(struct mode_parameter_header6));
 	avail = sizeof(struct mode_parameter_header6);
	if (!dbd)
	{
		mode_sense_block_descriptor(tdrive, ctio->data_ptr, allocation_length, &offset);
		header->block_descriptor_length = sizeof(struct mode_parameter_block_descriptor);
		avail += sizeof(struct mode_parameter_block_descriptor);
	}
	else
	{
		header->block_descriptor_length = 0;
	}

	switch (pc) {
		case MODE_SENSE_CURRENT_VALUES:
			avail += mode_sense_current_values(tdrive, ctio->data_ptr, allocation_length, dbd, page_code, sub_page_code, &offset);
			break;
		case MODE_SENSE_CHANGEABLE_VALUES:
			avail += mode_sense_changeable_values(tdrive, ctio->data_ptr, allocation_length, dbd, page_code, sub_page_code, &offset);
			break;
		case MODE_SENSE_DEFAULT_VALUES:
			avail += mode_sense_default_values(tdrive, ctio->data_ptr, allocation_length, dbd, page_code, sub_page_code, &offset);
			break;
		case MODE_SENSE_SAVED_VALUES:
			avail += mode_sense_saved_values(tdrive, ctio->data_ptr, allocation_length, dbd, page_code, sub_page_code, &offset);
			break;
	}

	header->mode_data_length = avail - offsetof(struct mode_parameter_header6, medium_type);
	if (tdrive->tape && tdrive_tape_loaded(tdrive))
		header->medium_type = tdrive->mode_header.medium_type;
	header->device_specific_parameter = tdrive->mode_header.wp;
	ctio->dxfer_len = offset;
	return 0;
}

static int
tdrive_cmd_persistent_reserve_in(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t service_action;
	uint16_t allocation_length;
	int retval;

	service_action = (cdb[1] & 0x1F);
	allocation_length = be16toh(*(uint16_t *)(&cdb[7]));

	debug_info("service action %x allocation length %d\n", service_action, allocation_length);
	if (allocation_length < 8)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	if (service_action == SERVICE_ACTION_READ_KEYS)
	{
		retval = persistent_reservation_read_keys(ctio, allocation_length, &tdrive->tdevice.reservation);
	}
	else if (service_action == SERVICE_ACTION_READ_RESERVATIONS)
	{
		retval = persistent_reservation_read_reservations(ctio, allocation_length, &tdrive->tdevice.reservation);
	}
	else if (service_action == SERVICE_ACTION_READ_CAPABILITIES)
	{
		retval = persistent_reservation_read_capabilities(ctio, allocation_length);
	}
	else if (service_action == SERVICE_ACTION_READ_FULL)
	{
		retval = persistent_reservation_read_full(ctio, allocation_length, &tdrive->tdevice.reservation);
	} 
	else
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		retval = 0;
	}
	return retval;
}

static int
tdrive_cmd_persistent_reserve_out(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t service_action;
	uint8_t scope;
	uint16_t parameter_list_length;
	int retval;

	scope = READ_NIBBLE_HIGH(cdb[2]);

	if (scope)
	{
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	service_action = (cdb[1] & 0x1F);

	parameter_list_length = be16toh(*(uint16_t *)(&cdb[7]));
	if (parameter_list_length != 24) {
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0,  PARAMETER_LIST_LENGTH_ERROR_ASC,  PARAMETER_LIST_LENGTH_ERROR_ASCQ);
		return 0;
	}

	debug_info("type %x scope %x service action %x parameter list length %d\n", type, scope, service_action, parameter_list_length);
	switch(service_action)
	{
		case SERVICE_ACTION_REGISTER:
			retval = persistent_reservation_handle_register(&tdrive->tdevice, ctio);
			break;
		case SERVICE_ACTION_REGISTER_IGNORE:
			retval = persistent_reservation_handle_register_and_ignore(&tdrive->tdevice, ctio);
			break;
		case SERVICE_ACTION_RESERVE:
			retval = persistent_reservation_handle_reserve(&tdrive->tdevice, ctio);
			break;
		case SERVICE_ACTION_RELEASE:
			retval = persistent_reservation_handle_release(&tdrive->tdevice, ctio);
			break;
		case SERVICE_ACTION_CLEAR:
			retval = persistent_reservation_handle_clear(&tdrive->tdevice, ctio);
			break;
		case SERVICE_ACTION_PREEMPT:
			retval = persistent_reservation_handle_preempt(&tdrive->tdevice, ctio, 0);
			break;
		case SERVICE_ACTION_PREEMPT_ABORT:
			retval = persistent_reservation_handle_preempt(&tdrive->tdevice, ctio, 1);
			break;
		default:
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
			retval = 0;
	}

	ctio_free_data(ctio);
	return retval;
}

static int
tdrive_cmd_reserve(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	struct tdevice *tdevice;
	struct reservation *reservation;

	tdevice = &tdrive->tdevice;
	reservation = &tdevice->reservation;

	tdevice_reservation_lock(tdevice);
	if (tdrive_cmd_access_ok(tdrive, ctio) != 0) {
		tdevice_reservation_unlock(tdevice);
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		return 0;
	}

	reservation->is_reserved = 1;
	reservation->type = RESERVATION_TYPE_RESERVE;
	port_fill(reservation->i_prt, ctio->i_prt);
	port_fill(reservation->t_prt, ctio->t_prt);
	reservation->init_int = ctio->init_int;
	tdevice_reservation_unlock(tdevice);
	return 0;
}

static int
tdrive_cmd_release(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	struct tdevice *tdevice;
	struct reservation *reservation;

	tdevice = &tdrive->tdevice;
	reservation = &tdevice->reservation;

	tdevice_reservation_lock(tdevice);
	if (iid_equal(reservation->i_prt, reservation->t_prt, reservation->init_int, ctio->i_prt, ctio->t_prt, ctio->init_int))
	{
		reservation->is_reserved = 0;
	}
	tdevice_reservation_unlock(tdevice);
	return 0;
}

static int
tdrive_cmd_request_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	struct initiator_state *istate;

	istate = ctio->istate;
	tdevice_reservation_lock(&tdrive->tdevice);
	device_request_sense(ctio, istate, tdrive->add_sense_len);
	if (tdrive->handlers.additional_request_sense)
	{
		(*tdrive->handlers.additional_request_sense)(tdrive, ctio);
	}
	tdevice_reservation_unlock(&tdrive->tdevice);
	return 0;
}

static int
tdrive_cmd_prevent_allow_medium_removal(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t prevent;
	struct initiator_state *istate;

	prevent = (cdb[4] & 0x03);
	istate = ctio->istate;
	istate->prevent_medium_removal = prevent;
	ctio->scsi_status = SCSI_STATUS_OK;
	ctio->dxfer_len = 0;
	ctio->data_ptr = NULL;
	return 0;
}

static int
tdrive_cmd_send_diagnostic(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	ctio_free_data(ctio);
	return 0;
}

static int
tdrive_cmd_receive_diagnostic_results(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t page_code;
	uint16_t allocation_length;
	struct receive_diagnostic_header header;
	int min_len;

	page_code = cdb[2];
	allocation_length = be16toh(*(uint16_t *)(&cdb[3]));
	min_len = min_t(int, sizeof(struct read_buffer_header), allocation_length);
	if (!min_len)
		return 0;
	bzero(&header, sizeof(header));
	header.page_code = page_code;

	ctio_allocate_buffer(ctio, min_len, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
		return -1;
	memcpy(ctio->data_ptr, &header, min_len);
	return 0;
}

#define RB_MODE_HEADER_DATA	0x00
#define RB_MODE_DATA		0x02
#define RB_MODE_DESCRIPTOR	0x03

#define RB_BUFFER_ID_VPD		0x03

static void
read_buffer_data(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t buffer_id, uint32_t buffer_offset, int send_header)
{
	struct read_buffer_header header;
	uint8_t *buffer = ctio->data_ptr;
	int avail = ctio->dxfer_len;
	int min_len;
	int capacity = 0;
	int done = 0;

	if (send_header)
		done += sizeof(header);

	switch (buffer_id) {
	case RB_BUFFER_ID_VPD:
		capacity = tdrive->unit_identifier.identifier_length + sizeof(struct device_identifier);
		min_len = min_t(int, capacity, avail - done);
		if (min_len > 0)
			memcpy(buffer+done, &tdrive->unit_identifier, min_len);
		break;
	}
	header.buffer_capacity = htobe32(capacity);
	min_len = min_t(int, avail, sizeof(header));
	memcpy(buffer, &header, min_len);
}

static void
read_buffer_descriptor(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t buffer_id)
{
	struct read_buffer_descriptor desc;
	int capacity = 0;
	int min_len;

	bzero(&desc, sizeof(desc));
	switch (buffer_id) {
	case RB_BUFFER_ID_VPD:
		capacity = tdrive->unit_identifier.identifier_length + sizeof(struct device_identifier);
		break;
	}
	desc.buffer_capacity[0] = (capacity >> 16) & 0xFF;
	desc.buffer_capacity[1] = (capacity >> 8) & 0xFF;
	desc.buffer_capacity[2] = (capacity) & 0xFF;
	min_len = min_t(int, ctio->dxfer_len, sizeof(desc));
	memcpy(ctio->data_ptr, &desc, min_len);
	ctio->dxfer_len = min_len;
}

static int
tdrive_cmd_read_buffer(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint32_t allocation_length, buffer_offset;
	uint8_t buffer_id, mode;
	int header = 0;
	int max_buffer_len = 512; /* For now we only support VPD */
	int min_len;

	buffer_id = cdb[2];
	mode = cdb[1] & 0x1F;
	buffer_offset = READ_24(cdb[3], cdb[4], cdb[5]);
	allocation_length = READ_24(cdb[6], cdb[7], cdb[8]);

	min_len = min_t(int, allocation_length, max_buffer_len);
	ctio_allocate_buffer(ctio, min_len, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
		return -1;

	bzero(ctio->data_ptr, ctio->dxfer_len);
	switch (mode) {
	case RB_MODE_HEADER_DATA:
		header = 1;
	case RB_MODE_DATA:
		read_buffer_data(tdrive, ctio, buffer_id, buffer_offset, header);
		break;
	case RB_MODE_DESCRIPTOR:
		read_buffer_descriptor(tdrive, ctio, buffer_id);
		break;
	}
	return 0;
}

int
tdrive_cmd_access_ok(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	struct reservation *reservation = &tdrive->tdevice.reservation;
	uint8_t write_excl = 0;
	uint8_t excl_access = 0;
	uint8_t write_excl_ro = 0;
	uint8_t excl_access_ro = 0;
	uint8_t registered = 0;
	struct registration *tmp;

	if (device_reserved(ctio, reservation) == 0)
	{
		return 0;
	}

	if (reservation->type == RESERVATION_TYPE_RESERVE)
	{
		switch (cdb[0])
		{
			case LOG_SELECT:
			case MODE_SELECT_6:
			case MODE_SELECT_10:
			case WRITE_ATTRIBUTE:
			case MODE_SENSE_6:
			case MODE_SENSE_10:
			case PERSISTENT_RESERVE_IN:
			case PERSISTENT_RESERVE_OUT:
			case TEST_UNIT_READY:
			case RESERVE:
			case ERASE:
			case LOCATE:
			case LOCATE_16:
			case LOAD_UNLOAD:
			case READ_6:
			case READ_POSITION:
			case REWIND:
			case SPACE:
			case WRITE_6:
			case WRITE_FILEMARKS:
			case SET_CAPACITY:
			case FORMAT_MEDIUM:
			case READ_ATTRIBUTE:
			case READ_BUFFER:
			case SEND_DIAGNOSTIC:
			case RECEIVE_DIAGNOSTIC_RESULTS:
				return -1; /* conflict */
			case PREVENT_ALLOW:
			{
				uint8_t prevent = (cdb[4] & 0x3);
				if (prevent)
				{
					return -1;
				}
				return 0;
			} 
			case INQUIRY:
			case LOG_SENSE:
			case REPORT_LUNS:
			case RELEASE:
			case READ_BLOCK_LIMITS:
			case REPORT_DENSITY_SUPPORT:
				return 0; 
		}
		return 0;
	}

	SLIST_FOREACH(tmp, &reservation->registration_list, r_list) {
		if (iid_equal(tmp->i_prt, tmp->t_prt, tmp->init_int, ctio->i_prt, ctio->t_prt, ctio->init_int))
		{
			registered = 1;
			break;
		}
	}

	switch (reservation->persistent_type)
	{
		case RESERVATION_TYPE_WRITE_EXCLUSIVE:
			write_excl = 1;
			break;
		case RESERVATION_TYPE_EXCLUSIVE_ACCESS:
			excl_access = 1;
			break;
		case RESERVATION_TYPE_WRITE_EXCLUSIVE_RO:
			write_excl_ro = 1;
			break;
		case RESERVATION_TYPE_EXCLUSIVE_ACCESS_RO:
			excl_access_ro = 1;
			break;
	}

	switch(cdb[0])
	{
		case LOG_SELECT:
		case MODE_SENSE_6:
		case MODE_SENSE_10:
		case MODE_SELECT_6:
		case MODE_SELECT_10:
		case WRITE_ATTRIBUTE:
		case TEST_UNIT_READY:
		case ERASE:
		case LOAD_UNLOAD:
		case WRITE_6:
		case WRITE_FILEMARKS:
		case SET_CAPACITY:
		case FORMAT_MEDIUM:
		case SEND_DIAGNOSTIC:
		case RECEIVE_DIAGNOSTIC_RESULTS:
			if (write_excl || excl_access)
			{
				return -1;
			}
			if ((write_excl_ro || excl_access_ro) && !registered)
			{
				return -1;
			}
			return 0;
		case LOCATE:
		case LOCATE_16:
		case READ_6:
		case READ_POSITION:
		case REWIND:
		case SPACE:
		case READ_ATTRIBUTE:
		case READ_BUFFER:
			if (excl_access)
			{
				return -1;
			}
			if (excl_access_ro && !registered)
			{
				return -1;
			}
			return 0;
		case PREVENT_ALLOW:
		{
			uint8_t prevent = (cdb[4] & 0x3);

			if (!prevent)
			{
				return 0;
			}
			if (write_excl || excl_access)
			{
				return -1;
			}
			if ((write_excl_ro || excl_access_ro) && !registered)
			{
				return -1;
			}
			return 0;
		}
		case INQUIRY:
		case PERSISTENT_RESERVE_IN:
		case PERSISTENT_RESERVE_OUT:
		case REPORT_LUNS:
		case READ_BLOCK_LIMITS:
		case REPORT_DENSITY_SUPPORT:
			return 0;
		case RELEASE:
		case RESERVE:
			return -1;
	}

	return 0;
}

int
tdrive_check_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t cmd)
{
	struct initiator_state *istate;
	struct sense_info *sinfo;

	tdrive_lock(tdrive);
	istate = ctio->istate;
	sinfo = device_get_sense(istate);
	if (!sinfo) {
		tdrive_unlock(tdrive);
		return 0;
	}
	ctio_free_data(ctio);
	device_move_sense(ctio, sinfo);
	tdrive_unlock(tdrive);
	return -1;
}

int
tdrive_check_cmd(void *drive, uint8_t op)
{
	switch(op)
	{
		case TEST_UNIT_READY:
		case INQUIRY:
		case READ_BLOCK_LIMITS:
		case MODE_SENSE_6:
		case MODE_SENSE_10:
		case MODE_SELECT_6:
		case MODE_SELECT_10:
		case WRITE_ATTRIBUTE:
		case LOG_SELECT:
		case LOG_SENSE:
		case LOAD_UNLOAD:
		case REWIND:
		case WRITE_6:
		case ERASE:
		case WRITE_FILEMARKS:
		case SET_CAPACITY:
		case FORMAT_MEDIUM:
		case READ_6:
		case SPACE:
		case REPORT_LUNS:
		case PREVENT_ALLOW:
		case RESERVE:
		case RELEASE:
		case PERSISTENT_RESERVE_IN:
		case PERSISTENT_RESERVE_OUT:
		case REQUEST_SENSE:
		case READ_POSITION:
		case LOCATE:
		case LOCATE_16:
		case REPORT_DENSITY_SUPPORT:
		case READ_ATTRIBUTE:
		case READ_BUFFER:
		case SEND_DIAGNOSTIC:
		case RECEIVE_DIAGNOSTIC_RESULTS:
			return 0;
	}
	debug_info("Invalid cmd %x\n", op);
	return -1;
}

void
tdrive_proc_cmd(void *drive, void *iop)
{
	struct tdrive *tdrive = drive;
	struct qsio_scsiio *ctio = iop;
	uint8_t *cdb = ctio->cdb;
	int retval;
	struct initiator_state *istate;
	struct sense_info *sinfo;
	int media_valid = 0;

	tdrive_lock(tdrive);
	istate = ctio->istate;
	if (!istate) {
		debug_warn("cmd %x from disallowed initiator\n", cdb[0]);
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, LOGICAL_UNIT_NOT_SUPPORTED_ASC, LOGICAL_UNIT_NOT_SUPPORTED_ASCQ);
		goto out;
	}

	switch(cdb[0]) {
	case INQUIRY:
	case REPORT_LUNS:
	case REQUEST_SENSE:
		break;
	default:
		sinfo = device_get_sense(istate);
		if (!sinfo)
			break;
		ctio_free_data(ctio);
		device_move_sense(ctio, sinfo);
		goto out;
	}

	if (tdrive->tape && tdrive->handlers.valid_medium)
	{
		media_valid = (*tdrive->handlers.valid_medium)(tdrive, tdrive->tape->make);
	}

	if (!tdrive->tape || !atomic_test_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags) || (tdrive->tape->locked && tdrive->tape->locked_by != tdrive) || (media_valid != 0))
	{
		uint8_t asc;
		uint8_t ascq;

		if (!tdrive->tape) {
			asc = MEDIUM_NOT_PRESENT_ASC;
			ascq = MEDIUM_NOT_PRESENT_ASCQ;
		} else if (media_valid != 0) {
			asc = INCOMPATIBLE_MEDIUM_INSTALLED_ASC;
			ascq = INCOMPATIBLE_MEDIUM_INSTALLED_ASCQ;
		} else if (!atomic_test_bit(TDRIVE_FLAGS_TAPE_LOADED, &tdrive->flags)) {
			asc = MEDIUM_NOT_PRESENT_LOADABLE_ASC;
			asc = MEDIUM_NOT_PRESENT_LOADABLE_ASCQ;
		} else {
			asc = LOGICAL_UNIT_IS_IN_PROCESS_OF_BECOMING_READY_ASC;
			ascq = LOGICAL_UNIT_IS_IN_PROCESS_OF_BECOMING_READY_ASCQ;
		}

		/* Ok allow only a few operations */
		switch(cdb[0]) {
			case INQUIRY:
			case TEST_UNIT_READY:
			case REPORT_LUNS:
			case MODE_SENSE_6:
			case MODE_SENSE_10:
			case MODE_SELECT_6:
			case MODE_SELECT_10:
			case LOG_SENSE:
			case LOG_SELECT:
			case READ_BLOCK_LIMITS:
			case RESERVE:
			case RELEASE:
			case REQUEST_SENSE:
			case PERSISTENT_RESERVE_IN:
			case PERSISTENT_RESERVE_OUT:
			case PREVENT_ALLOW:
				break;
			case LOAD_UNLOAD:
				if (tdrive->tape && !(tdrive->tape->locked && tdrive->tape->locked_by != tdrive))
				{
					break;
				}
			default:
				ctio_free_data(ctio);
				ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_NOT_READY, 0, asc, ascq);
				goto out;
		}
	}

	if (tdrive_cmd_access_ok(tdrive, ctio) != 0) 
	{
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		ctio_free_data(ctio);
		goto out;
	}

	switch(cdb[0]) {
		case TEST_UNIT_READY:
			retval = tdrive_cmd_test_unit_ready(tdrive, ctio);	
			break;
		case INQUIRY:
			retval = tdrive_cmd_inquiry(tdrive, ctio);
			break;
		case READ_BLOCK_LIMITS:
			retval = tdrive_cmd_read_block_limits(tdrive, ctio);
			break;
		case MODE_SENSE_6:
			retval = tdrive_cmd_mode_sense6(tdrive, ctio);
			break;
		case MODE_SENSE_10:
			retval = tdrive_cmd_mode_sense10(tdrive, ctio);
			break;
		case MODE_SELECT_6:
			retval = tdrive_cmd_mode_select6(tdrive, ctio);
			break;
		case MODE_SELECT_10:
			retval = tdrive_cmd_mode_select10(tdrive, ctio);
			break;
		case LOG_SELECT:
			retval = tdrive_cmd_log_select6(tdrive, ctio);
			break;
		case LOG_SENSE:
			retval = tdrive_cmd_log_sense6(tdrive, ctio);
			break;
		case READ_ATTRIBUTE:
			retval = tdrive_cmd_read_attribute(tdrive, ctio);
			break;
		case WRITE_ATTRIBUTE:
			retval = tdrive_cmd_write_attribute(tdrive, ctio);
			break;
		case READ_BUFFER:
			retval = tdrive_cmd_read_buffer(tdrive, ctio);
			break;
		case SEND_DIAGNOSTIC:
			retval = tdrive_cmd_send_diagnostic(tdrive, ctio);
			break;
		case RECEIVE_DIAGNOSTIC_RESULTS:
			retval = tdrive_cmd_receive_diagnostic_results(tdrive, ctio);
			break;
		case LOAD_UNLOAD:
			retval = tdrive_cmd_load_unload(tdrive, ctio);
			break;
		case REWIND:
			retval = tdrive_cmd_rewind(tdrive, ctio);
			break;
		case WRITE_6:
			retval = tdrive_cmd_validate_write6(tdrive, ctio);
			break;
		case ERASE:
			retval = tdrive_cmd_erase(tdrive, ctio);
			break;
		case WRITE_FILEMARKS:
			retval = tdrive_cmd_validate_write_filemarks(tdrive, ctio);
			break;
		case SET_CAPACITY:
			retval = tdrive_cmd_set_capacity(tdrive, ctio);
			break;
		case FORMAT_MEDIUM:
			retval = tdrive_cmd_format_medium(tdrive, ctio);
			break;
		case READ_6:
			retval = tdrive_cmd_read6(tdrive, ctio);
			break;
		case SPACE:
			retval = tdrive_cmd_space(tdrive, ctio);
			break;
		case REPORT_LUNS:
			retval = tdrive_cmd_report_luns(tdrive, ctio);
			break;
		case PREVENT_ALLOW:
			retval = tdrive_cmd_prevent_allow_medium_removal(tdrive, ctio);
			break;
		case RESERVE:
			retval = tdrive_cmd_reserve(tdrive, ctio);
			break;
		case RELEASE:
			retval = tdrive_cmd_release(tdrive, ctio);
			break;
		case PERSISTENT_RESERVE_IN:
			retval = tdrive_cmd_persistent_reserve_in(tdrive, ctio);
			break;
		case PERSISTENT_RESERVE_OUT:
			retval = tdrive_cmd_persistent_reserve_out(tdrive, ctio);
			break;
		case REQUEST_SENSE:
			retval = tdrive_cmd_request_sense(tdrive, ctio);
			break;
		case READ_POSITION:
			retval = tdrive_cmd_read_position(tdrive, ctio);
			break;
		case LOCATE:
			retval = tdrive_cmd_locate(tdrive, ctio);
			break;
		case LOCATE_16:
			retval = tdrive_cmd_locate16(tdrive, ctio);
			break;
		case REPORT_DENSITY_SUPPORT:
			retval = tdrive_cmd_report_density_support(tdrive, ctio);
			break;
		default:
			debug_info("Invalid command op %x\n", cdb[0]);
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_COMMAND_OPERATION_CODE_ASC, INVALID_COMMAND_OPERATION_CODE_ASCQ);
			retval = 0;
			break;
	}

	if (retval != 0)
	{
		debug_check(ctio->dxfer_len);
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_HARDWARE_ERROR, 0, INTERNAL_TARGET_FAILURE_ASC, INTERNAL_TARGET_FAILURE_ASCQ);
	}

out:
	if (!ctio_buffered(ctio))
		device_send_ccb(ctio);
	else
		ctio_free(ctio);
	tdrive_unlock(tdrive);
}

void
tdrive_proc_write_cmd(void *drive, void *iop)
{
	struct tdrive *tdrive = drive;
	struct qsio_scsiio *ctio = iop;
	uint8_t *cdb = ctio->cdb;
	uint64_t start_ticks = ticks;;

	switch(cdb[0]) {
	case WRITE_6:
		tdrive_cmd_write6(tdrive, ctio);
		break;
	case WRITE_FILEMARKS:
		tdrive_cmd_write_filemarks(tdrive, ctio);
		break;
	default:
		debug_check(1);
	}
	TDRIVE_STATS_ADD(tdrive, write_ticks, (ticks - start_ticks));

	if (ctio_buffered(ctio))
		ctio_free(ctio);
}

void
tdrive_reset(struct tdrive *tdrive, uint64_t i_prt[], uint64_t t_prt[], uint8_t init_int)
{
	struct tdevice *tdevice = (struct tdevice *)tdrive;
	struct initiator_state *istate;
	struct istate_list *istate_list = &tdevice->istate_list;
	struct reservation *reservation = &tdrive->tdevice.reservation;

	tdrive_lock(tdrive);
	if (reservation->is_reserved && reservation->type == RESERVATION_TYPE_RESERVE) {
		reservation->is_reserved = 0;
		reservation->type = 0;
	}
	tdrive_unlock(tdrive);

	tdevice_reservation_lock(tdevice);
	SLIST_FOREACH(istate, istate_list, i_list) {
		istate->prevent_medium_removal = 0;
	}
	tdevice_reservation_unlock(tdevice);
}

int
tdrive_device_identification(struct tdrive *tdrive, uint8_t *buffer, int length)
{
	struct device_identification_page *page = (struct device_identification_page *)buffer;
	struct logical_unit_identifier *unit_identifier;
	uint32_t page_length = 0;
	int done = 0;
	uint8_t idlength;

	if (length < sizeof(struct vital_product_page))
	{
		return -1;
	}

	page->device_type = T_SEQUENTIAL;
	page->page_code = DEVICE_IDENTIFICATION_PAGE;
	page_length = tdrive->unit_identifier.identifier_length + sizeof(struct device_identifier);
	page->page_length = page_length;

	done += sizeof(struct device_identification_page);

	idlength = tdrive->unit_identifier.identifier_length + sizeof(struct device_identifier);
	if (done + idlength > length)
	{
		return done;
	}

	unit_identifier = (struct logical_unit_identifier *)(buffer+done);
	memcpy(unit_identifier, &tdrive->unit_identifier, idlength);
	done += idlength;
	return done;
}

int
tdrive_serial_number(struct tdrive *tdrive, uint8_t *buffer, int length)
{
	struct serial_number_page *page = (struct serial_number_page *) buffer;
	int min_len;

	if (length < sizeof(struct vital_product_page))
		return -1;

	bzero(page, sizeof(struct vital_product_page));
	page->device_type = T_SEQUENTIAL; /* peripheral qualifier */
	page->page_code = UNIT_SERIAL_NUMBER_PAGE;
	page->page_length =  tdrive->serial_len;

	min_len = min_t(int, tdrive->serial_len, (length - sizeof(struct vital_product_page)));
	if (min_len)
		memcpy(page->serial_number, tdrive->unit_identifier.serial_number, min_len);
	return (min_len+sizeof(struct vital_product_page));
}

int
tdrive_load(struct tdrive *tdrive, struct vdeviceinfo *deviceinfo)
{
	struct tape *tape;
	int retval;
	int load;

	tape = tdrive->tape;
	load = deviceinfo->mod_type;
	if (!load && !tape)
		return 0;

	if (load && tape && strcmp(tape->label, deviceinfo->tape_label) == 0)
		return 0;

	if (tape) {
		retval = tdrive_unload_tape(tdrive, NULL);
		if (unlikely(retval != 0))
			return -1;
	} 

	if (!load)
		return 0;

	tape = tdrive_find_tape(tdrive, deviceinfo->tape_id);
	if (unlikely(!tape)) {
		debug_warn("Cannot find tape at id %s\n", deviceinfo->tape_label);
		return -1;
	}

	retval = tdrive_load_tape(tdrive, tape);
	if (unlikely(retval != 0)) {
		debug_warn("Cannot load tape at id %s\n", deviceinfo->tape_label);
		return -1;
	}

	return 0;
}


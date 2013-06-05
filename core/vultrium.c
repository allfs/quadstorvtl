/* 
 * Copyright (C) Shivaram Upadhyayula <shivaram.u@quadstor.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
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

#include "mchanger.h"
#include "sense.h"
#include "vendor.h"
#include "vdevdefs.h"
#include "tape.h"
#include "tape_partition.h"

static int
vultrium_device_identification(struct tdrive *tdrive, uint8_t *buffer, int length)
{
	return tdrive_device_identification(tdrive, buffer, length);
}

static int
vultrium_serial_number(struct tdrive *tdrive, uint8_t *buffer, int length)
{
	return tdrive_serial_number(tdrive, buffer, length);
}

static int
vultrium_vendor_specific_page2_vpd(struct tdrive *tdrive, uint8_t *buffer, uint16_t allocation_length)
{
	struct vendor_specific_page *page;

	bzero(buffer, allocation_length);
	page = (struct vendor_specific_page *)(buffer);
	page->device_type = T_SEQUENTIAL; /* peripheral qualifier */
	page->page_code = VENDOR_SPECIFIC_PAGE2;
	page->page_length = allocation_length - sizeof(struct vendor_specific_page);
	return allocation_length;
}

static int
vultrium_evpd_inquiry(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t page_code, uint16_t allocation_length)
{
	uint16_t max_allocation_length;
	int retval;

	max_allocation_length = max_t(uint16_t, 64, allocation_length);
	ctio_allocate_buffer(ctio, max_allocation_length, Q_WAITOK);
	if (unlikely(!ctio->data_ptr))
		return -1;

	bzero(ctio->data_ptr, ctio->dxfer_len);

	switch (page_code)
	{
		case UNIT_SERIAL_NUMBER_PAGE:
			retval = vultrium_serial_number(tdrive, ctio->data_ptr, allocation_length);
			if (unlikely(retval < 0))
			{
				goto err;
			}

			ctio->dxfer_len = retval;
			break;
		case DEVICE_IDENTIFICATION_PAGE:
			retval = vultrium_device_identification(tdrive, ctio->data_ptr, allocation_length);
			if (unlikely(retval < 0))
			{
				goto err;
			}

			ctio->dxfer_len = retval;
			break;
		case VITAL_PRODUCT_DATA_PAGE:
			retval = tdrive_copy_vital_product_page_info(tdrive, ctio->data_ptr, allocation_length);
			if (unlikely(retval < 0))
			{
				goto err;
			}

			ctio->dxfer_len = retval;
			break;
		case VENDOR_SPECIFIC_PAGE2:
			retval = vultrium_vendor_specific_page2_vpd(tdrive, ctio->data_ptr, allocation_length);
			if (unlikely(retval < 0))
			{
				goto err;
			}

			ctio->dxfer_len = retval;
			break;
		default:
			goto err;
	}
	return 0;
err:
	ctio_free_data(ctio);
	ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);
	return 0;
}

static inline int
is_hp_drive(struct tdrive *tdrive)
{
	switch (tdrive->make) {
		case DRIVE_TYPE_VHP_ULT232:
		case DRIVE_TYPE_VHP_ULT448:
		case DRIVE_TYPE_VHP_ULT460:
		case DRIVE_TYPE_VHP_ULT960:
		case DRIVE_TYPE_VHP_ULT1840:
			return 1;
	}
	return 0;
}

static void
vultrium_unload_tape(struct tdrive *tdrive)
{
	uint8_t density = 0;

	switch (tdrive->make)
	{	
		case DRIVE_TYPE_VIBM_3580ULT1:
			density = DENSITY_ULTRIUM_1;
			break;
		case DRIVE_TYPE_VIBM_3580ULT2:
			density = DENSITY_ULTRIUM_2;
			break;
		case DRIVE_TYPE_VIBM_3580ULT3:
			density = DENSITY_ULTRIUM_3;
			break;
		case DRIVE_TYPE_VIBM_3580ULT4:
			density = DENSITY_ULTRIUM_4;
			break;
	}
	tdrive->block_descriptor.density_code = density;
}

static int
vultrium_load_tape(struct tdrive *tdrive, struct tape *tape)
{
	uint8_t density = 0;
	uint8_t medium_type = 0;
	int worm = tape->worm;

	switch (tape->make) {	
	case VOL_TYPE_LTO_1:
		density = DENSITY_ULTRIUM_1;
		medium_type = worm ? MEDIUM_TYPE_LTO_1_WORM: MEDIUM_TYPE_LTO_1;
		break;
	case VOL_TYPE_LTO_2:
		density = DENSITY_ULTRIUM_2;
		medium_type = worm ? MEDIUM_TYPE_LTO_2_WORM: MEDIUM_TYPE_LTO_2;
		break;
	case VOL_TYPE_LTO_3:
		density = DENSITY_ULTRIUM_3;
		medium_type = worm ? MEDIUM_TYPE_LTO_3_WORM : MEDIUM_TYPE_LTO_3;
		break;
	case VOL_TYPE_LTO_4:
		density = DENSITY_ULTRIUM_4;
		medium_type = worm ? MEDIUM_TYPE_LTO_4_WORM: MEDIUM_TYPE_LTO_4;
		break;
	case VOL_TYPE_LTO_5:
		density = DENSITY_ULTRIUM_5;
		medium_type = worm ? MEDIUM_TYPE_LTO_5_WORM: MEDIUM_TYPE_LTO_5;
		break;
	case VOL_TYPE_LTO_6:
		density = DENSITY_ULTRIUM_6;
		medium_type = worm ? MEDIUM_TYPE_LTO_6_WORM: MEDIUM_TYPE_LTO_6;
		break;
	default:
		break;
	}

	if (!is_hp_drive(tdrive))
		tdrive->mode_header.medium_type = medium_type;
	else
		tdrive->mode_header.medium_type = 0;
	tdrive->block_descriptor.density_code = density;
	return 0;	
}

static void
vultrium_update_device_configuration_page(struct tdrive *tdrive)
{
	struct device_configuration_page *page = &tdrive->configuration_page;

	page->rew |= 0x40; /* BIS */
	page->select_data_compression_algorithm = 0x01;
}

static void
vultrium_update_data_compression_page(struct tdrive *tdrive)
{
	struct data_compression_page *page = &tdrive->compression_page;
	page->compression_algorithm = be32toh(0x01);
	page->decompression_algorithm = be32toh(0x01);
}

static void
vultrium_init_inquiry_data(struct tdrive *tdrive)
{
	struct inquiry_data *inquiry = &tdrive->inquiry;

	bzero(inquiry, sizeof(*inquiry));
	inquiry->device_type = T_SEQUENTIAL;
	inquiry->rmb = 0x80;
	inquiry->version = ANSI_VERSION_SCSI3_SPC3;
	inquiry->response_data = RESPONSE_DATA; 
	inquiry->additional_length = STANDARD_INQUIRY_LEN - 5; /* n - 4 */
	inquiry->mchangr = 0x80; /* BQUE */
#if 0
	inquiry->linked = 0x2; /* CMDQUE */
#endif
	memcpy(&inquiry->vendor_id, tdrive->unit_identifier.vendor_id, 8);
	memcpy(&inquiry->product_id, tdrive->unit_identifier.product_id, 16);
	memcpy(&inquiry->revision_level, PRODUCT_REVISION_QUADSTOR, strlen(PRODUCT_REVISION_QUADSTOR));
}

#define MAIN_PARTITION_REMAINING_CAPACITY	0x01
#define ALT_PARTITION_REMAINING_CAPACITY	0x02
#define MAIN_PARTITION_MAXIMUM_CAPACITY		0x03
#define ALT_PARTITION_MAXIMUM_CAPACITY		0x04

static uint16_t
vultrium_tape_capacity_log_sense(struct tdrive *tdrive, uint8_t *buffer, uint16_t buffer_length, uint16_t parameter_pointer)
{
	struct tape *tape;
	struct tape_partition *partition;
	struct scsi_log_page page;
	struct log_parameter32 *param, tmp;
	int done, min_len, page_length = 0;
	uint64_t maximum_capacity0 = 0, maximum_capacity1 = 0;
	uint64_t remaining_capacity0 = 0, remaining_capacity1 = 0;

	bzero(&page, sizeof(page));
	page.page_code = TAPE_CAPACITY_LOG_PAGE;
	done = min_t(int, sizeof(page), buffer_length);

	/* tdrive->tape should never be null here */
	tape = tdrive->tape;
	if (unlikely(!tape))
		goto out;

	partition = tape_get_partition(tape, 0);
	if (partition) {
		maximum_capacity0 = partition->size >> 20;
		remaining_capacity0 = (partition->size - partition->used)  >> 20;
	}

	partition = tape_get_partition(tape, 1);
	if (partition) {
		maximum_capacity1 = partition->size >> 20;
		remaining_capacity1 = (partition->size - partition->used)  >> 20;
	}

	tmp.parameter_flags = 0x40; /* Disable Save */
	tmp.parameter_length = sizeof(uint32_t);
	if (parameter_pointer <= MAIN_PARTITION_REMAINING_CAPACITY) {
		/* Total uncorrected errors*/
		param = (struct log_parameter32 *)(buffer+done);
		tmp.parameter_code = htobe16(MAIN_PARTITION_REMAINING_CAPACITY);
		tmp.parameter_value = htobe32(remaining_capacity0);
		min_len = min_t(int, sizeof(tmp), buffer_length - done);
		memcpy(param, &tmp, min_len);
		done += min_len;
		page_length += sizeof(tmp);
	}

	if (parameter_pointer <= ALT_PARTITION_REMAINING_CAPACITY) {
		/* Total uncorrected errors*/
		param = (struct log_parameter32 *)(buffer+done);
		tmp.parameter_code = htobe16(ALT_PARTITION_REMAINING_CAPACITY);
		tmp.parameter_value = htobe32(remaining_capacity1);
		min_len = min_t(int, sizeof(tmp), buffer_length - done);
		memcpy(param, &tmp, min_len);
		done += min_len;
		page_length += sizeof(tmp);
	}

	if (parameter_pointer <= MAIN_PARTITION_MAXIMUM_CAPACITY) {
		/* Total uncorrected errors*/
		param = (struct log_parameter32 *)(buffer+done); 	
		tmp.parameter_code = htobe16(MAIN_PARTITION_MAXIMUM_CAPACITY);
		tmp.parameter_value = htobe32(maximum_capacity0);
		min_len = min_t(int, sizeof(tmp), buffer_length - done);
		memcpy(param, &tmp, min_len);
		done += min_len;
		page_length += sizeof(tmp);
	}

	if (parameter_pointer <= ALT_PARTITION_MAXIMUM_CAPACITY) {
		/* Total uncorrected errors*/
		param = (struct log_parameter32 *)(buffer+done); 	
		tmp.parameter_code = htobe16(ALT_PARTITION_MAXIMUM_CAPACITY);
		tmp.parameter_value = htobe32(maximum_capacity1);
		min_len = min_t(int, sizeof(tmp), buffer_length - done);
		memcpy(param, &tmp, min_len);
		done += min_len;
		page_length += sizeof(tmp);
	}

out:
	page.page_length = htobe16(page_length);
	min_len = min_t(int, sizeof(page), buffer_length);
	memcpy(buffer, &page, min_len);
	return done;
}

static uint16_t
vultrium_log_sense(struct tdrive *tdrive, uint8_t page_code, uint8_t *buffer, uint16_t buffer_length, uint16_t parameter_pointer)
{
	switch (page_code) {
	case TAPE_CAPACITY_LOG_PAGE:
		return vultrium_tape_capacity_log_sense(tdrive, buffer, buffer_length, parameter_pointer);
	default:
		break;
	}
	return 0;
}

static int
vultrium_valid_medium(struct tdrive *tdrive, int voltype)
{
	switch (tdrive->make) {
	case DRIVE_TYPE_VHP_ULT232:
	case DRIVE_TYPE_VIBM_3580ULT1:
		if (unlikely(voltype != VOL_TYPE_LTO_1))
			return -1;
		return 0;
	case DRIVE_TYPE_VHP_ULT448:
	case DRIVE_TYPE_VHP_ULT460:
	case DRIVE_TYPE_VIBM_3580ULT2:
		if (unlikely((voltype != VOL_TYPE_LTO_1) && (voltype != VOL_TYPE_LTO_2)))
			return -1;
		return 0;
	case DRIVE_TYPE_VHP_ULT960:
	case DRIVE_TYPE_VIBM_3580ULT3:
		if (unlikely((voltype != VOL_TYPE_LTO_1) && (voltype != VOL_TYPE_LTO_2) && (voltype != VOL_TYPE_LTO_3)))
			return -1;
		return 0;
	case DRIVE_TYPE_VHP_ULT1840:
	case DRIVE_TYPE_VIBM_3580ULT4:
		if (unlikely((voltype != VOL_TYPE_LTO_2) && (voltype != VOL_TYPE_LTO_3) && (voltype != VOL_TYPE_LTO_4)))
			return -1;
		return 0;
	case DRIVE_TYPE_VHP_ULT3280:
	case DRIVE_TYPE_VIBM_3580ULT5:
		if (unlikely((voltype != VOL_TYPE_LTO_3) && (voltype != VOL_TYPE_LTO_4) && (voltype != VOL_TYPE_LTO_5)))
			return -1;
		return 0;
	case DRIVE_TYPE_VHP_ULT6250:
	case DRIVE_TYPE_VIBM_3580ULT6:
		if (unlikely((voltype != VOL_TYPE_LTO_3) && (voltype != VOL_TYPE_LTO_4) && (voltype != VOL_TYPE_LTO_5) && (voltype != VOL_TYPE_LTO_6)))
			return -1;
		return 0;
	default:
		break;
	}
	return -1;
}

static void
tdrive_add_lto1_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_ULTRIUM_1;
	desc->sdensity_code = DENSITY_ULTRIUM_1;
	desc->wrtok |= wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 4880;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(127);
	desc->tracks = htobe16(384);
	desc->capacity = htobe32(95367);

	memcpy(desc->organization, "LTO-CVE ", 8);
	memcpy(desc->density_name, "U-18    ", 8);
	sys_memset(desc->description, ' ', 20);
	memcpy(desc->description,  "Ultrium 1/8T", strlen("Ultrium 1/8T"));
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
tdrive_add_lto2_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_ULTRIUM_2;
	desc->sdensity_code = DENSITY_ULTRIUM_2;
	desc->wrtok |= wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 7398;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(127);
	desc->tracks = htobe16(512);
	desc->capacity = htobe32(190734);

	memcpy(desc->organization, "LTO-CVE ", 8);
	memcpy(desc->density_name, "U-28    ", 8);
	sys_memset(desc->description, ' ', 20);
	memcpy(desc->description,  "Ultrium 2/8T", strlen("Ultrium 2/8T"));
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
tdrive_add_lto3_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_ULTRIUM_3;
	desc->sdensity_code = DENSITY_ULTRIUM_3;
	desc->wrtok |= wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 9638;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(127);
	desc->tracks = htobe16(704);
	desc->capacity = htobe32(381469);

	memcpy(desc->organization, "LTO-CVE ", 8);
	memcpy(desc->density_name, "U-316   ", 8);
	sys_memset(desc->description, ' ', 20);
	memcpy(desc->description,  "Ultrium 3/16T", strlen("Ultrium 3/16T"));
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
tdrive_add_lto4_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_ULTRIUM_4;
	desc->sdensity_code = DENSITY_ULTRIUM_4;
	desc->wrtok |= wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 12725;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(127);
	desc->tracks = htobe16(896);
	desc->capacity = htobe32(800000);

	memcpy(desc->organization, "LTO-CVE ", 8);
	memcpy(desc->density_name, "U-416   ", 8);
	sys_memset(desc->description, ' ', 20);
	memcpy(desc->description,  "Ultrium 4/16T", strlen("Ultrium 4/16T"));
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
tdrive_add_lto5_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_ULTRIUM_5;
	desc->sdensity_code = DENSITY_ULTRIUM_5;
	desc->wrtok |= wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 15142;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(127);
	desc->tracks = htobe16(1280);
	desc->capacity = htobe32(1500000);

	memcpy(desc->organization, "LTO-CVE ", 8);
	memcpy(desc->density_name, "U-516   ", 8);
	sys_memset(desc->description, ' ', 20);
	memcpy(desc->description,  "Ultrium 5/16T", strlen("Ultrium 5/16T"));
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
tdrive_add_lto6_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_ULTRIUM_6;
	desc->sdensity_code = DENSITY_ULTRIUM_6;
	desc->wrtok |= wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 15142;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(127);
	desc->tracks = htobe16(2176);
	desc->capacity = htobe32(2500000);

	memcpy(desc->organization, "LTO-CVE ", 8);
	memcpy(desc->density_name, "U-616   ", 8);
	sys_memset(desc->description, ' ', 20);
	memcpy(desc->description,  "Ultrium 6/16T", strlen("Ultrium 6/16T"));
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
vultrium_init_density_support_descriptors(struct tdrive *tdrive)
{
	switch(tdrive->make)
	{
		case DRIVE_TYPE_VHP_ULT232:
		case DRIVE_TYPE_VIBM_3580ULT1:
			tdrive_add_lto1_density_descriptor(tdrive, 1, 1);
			break;
		case DRIVE_TYPE_VHP_ULT448:
		case DRIVE_TYPE_VHP_ULT460:
		case DRIVE_TYPE_VIBM_3580ULT2:
			tdrive_add_lto2_density_descriptor(tdrive, 1, 1);
			tdrive_add_lto1_density_descriptor(tdrive, 0, 1);
			break;
		case DRIVE_TYPE_VHP_ULT960:
		case DRIVE_TYPE_VIBM_3580ULT3:
			tdrive_add_lto3_density_descriptor(tdrive, 1, 1);
			tdrive_add_lto2_density_descriptor(tdrive, 0, 1);
			tdrive_add_lto1_density_descriptor(tdrive, 0, 1);
			break;
		case DRIVE_TYPE_VHP_ULT1840:
		case DRIVE_TYPE_VIBM_3580ULT4:
			tdrive_add_lto4_density_descriptor(tdrive, 1, 1);
			tdrive_add_lto3_density_descriptor(tdrive, 0, 1);
			tdrive_add_lto2_density_descriptor(tdrive, 0, 1);
			break;
		case DRIVE_TYPE_VHP_ULT3280:
		case DRIVE_TYPE_VIBM_3580ULT5:
			tdrive_add_lto5_density_descriptor(tdrive, 1, 1);
			tdrive_add_lto4_density_descriptor(tdrive, 0, 1);
			tdrive_add_lto3_density_descriptor(tdrive, 0, 1);
			break;
		case DRIVE_TYPE_VHP_ULT6250:
		case DRIVE_TYPE_VIBM_3580ULT6:
			tdrive_add_lto6_density_descriptor(tdrive, 1, 1);
			tdrive_add_lto5_density_descriptor(tdrive, 0, 1);
			tdrive_add_lto4_density_descriptor(tdrive, 0, 1);
			tdrive_add_lto3_density_descriptor(tdrive, 0, 1);
			break;
		default:
			debug_check(1);
	}
}

void
vultrium_init_handlers(struct tdrive *tdrive)
{
	struct tdrive_handlers *handlers = &tdrive->handlers;
	char *product_id, *vendor_id;

	handlers->init_inquiry_data = vultrium_init_inquiry_data;
	handlers->evpd_inquiry = vultrium_evpd_inquiry;
	handlers->load_tape = vultrium_load_tape;
	handlers->unload_tape = vultrium_unload_tape;
	handlers->additional_log_sense = vultrium_log_sense;
	handlers->valid_medium = vultrium_valid_medium;

	tdrive->evpd_info.num_pages = 0x03;
	tdrive->evpd_info.page_code[0] = VITAL_PRODUCT_DATA_PAGE;
	tdrive->evpd_info.page_code[1] = DEVICE_IDENTIFICATION_PAGE;
	tdrive->evpd_info.page_code[2] = UNIT_SERIAL_NUMBER_PAGE;
	tdrive->supports_evpd = 1;

	tdrive->log_info.num_pages = 0x02;
	tdrive->log_info.page_code[0] = 0x00;
	tdrive->log_info.page_code[1] = TAPE_CAPACITY_LOG_PAGE;

	/* Ideally we should be moving this elsewhere */
	vultrium_update_device_configuration_page(tdrive);
	vultrium_update_data_compression_page(tdrive);

	switch (tdrive->make)
	{
		case DRIVE_TYPE_VHP_ULT232:
			product_id = PRODUCT_ID_HP_ULT1;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VHP_ULT448:
			product_id = PRODUCT_ID_HP_ULT2;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VHP_ULT460:
			product_id = PRODUCT_ID_HP_ULT2;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VHP_ULT960:
			product_id = PRODUCT_ID_HP_ULT3;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VHP_ULT1840:
			product_id = PRODUCT_ID_HP_ULT4;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VHP_ULT3280:
			product_id = PRODUCT_ID_HP_ULT5;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VHP_ULT6250:
			product_id = PRODUCT_ID_HP_ULT6;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VIBM_3580ULT1:
			product_id = PRODUCT_ID_IBM_ULT1_ALT;
			vendor_id = VENDOR_ID_IBM;
			break;
		case DRIVE_TYPE_VIBM_3580ULT2:
			product_id = PRODUCT_ID_IBM_ULT2_ALT;
			vendor_id = VENDOR_ID_IBM;
			break;
		case DRIVE_TYPE_VIBM_3580ULT3:
			product_id = PRODUCT_ID_IBM_ULT3_ALT;
			vendor_id = VENDOR_ID_IBM;
			break;
		case DRIVE_TYPE_VIBM_3580ULT4:
			product_id = PRODUCT_ID_IBM_ULT4_ALT;
			vendor_id = VENDOR_ID_IBM;
			break;
		case DRIVE_TYPE_VIBM_3580ULT5:
			product_id = PRODUCT_ID_IBM_ULT5_ALT;
			vendor_id = VENDOR_ID_IBM;
			break;
		case DRIVE_TYPE_VIBM_3580ULT6:
			product_id = PRODUCT_ID_IBM_ULT6_ALT;
			vendor_id = VENDOR_ID_IBM;
			break;
		default:
			debug_check(1);
			return;
	}

	device_init_unit_identifier(&tdrive->unit_identifier, vendor_id, product_id, tdrive->serial_len);
	tdrive->supports_devid = 1;
	vultrium_init_density_support_descriptors(tdrive);
	vultrium_unload_tape(tdrive);
	return;
}

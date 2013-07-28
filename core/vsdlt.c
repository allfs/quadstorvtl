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

#include "mchanger.h"
#include "sense.h"
#include "vendor.h"
#include "vdevdefs.h"

#define DRIVE_DENSITY_110GB_MASK  0xA0
#define DRIVE_DENSITY_160GB_MASK  0xB0

static void
vsdlt_init_inquiry_data(struct tdrive *tdrive)
{
	struct inquiry_data *inquiry = &tdrive->inquiry;

	bzero(inquiry, sizeof(*inquiry));
	inquiry->device_type = T_SEQUENTIAL;
	inquiry->rmb = 0x80;
	inquiry->version = ANSI_VERSION_SCSI3_SPC3;
	inquiry->response_data = RESPONSE_DATA | AERC_MASK | HISUP_MASK; 
	inquiry->additional_length = STANDARD_INQUIRY_LEN - 5; /* n - 4 */
	inquiry->mchangr = 0x80; /* BQUE */
#if 0
	inquiry->linked = 0x2; /* CMDQUE */
#endif
	memcpy(&inquiry->vendor_id, tdrive->unit_identifier.vendor_id, 8);
	memcpy(&inquiry->product_id, tdrive->unit_identifier.product_id, 16);
	memcpy(&inquiry->revision_level, PRODUCT_REVISION_QUADSTOR, strlen(PRODUCT_REVISION_QUADSTOR));
}

static void
vsdlt_unload_tape(struct tdrive *tdrive)
{
	tdrive->block_descriptor.density_code = 0;
}

static int
vsdlt_load_tape(struct tdrive *tdrive, struct tape *tape)
{

	uint8_t density = 0;

	switch (tape->make)
	{
		case VOL_TYPE_DLT_4:
			density = DENSITY_DLT_4_DEFAULT;
			tdrive->mode_header.medium_type = MEDIUM_TYPE_DLT_4;
			break;
		case VOL_TYPE_SDLT_1:
			density = DENSITY_SUPER_DLT_1_DEFAULT;
			tdrive->mode_header.medium_type = MEDIUM_TYPE_SDLT_1;
			break;
		case VOL_TYPE_SDLT_2:
			density = DENSITY_SUPER_DLT_2_DEFAULT;
			tdrive->mode_header.medium_type = MEDIUM_TYPE_SDLT_1;
			break;
		case VOL_TYPE_SDLT_3:
			density = DENSITY_SUPER_DLT_3_DEFAULT;
			tdrive->mode_header.medium_type = MEDIUM_TYPE_SDLT_2;
			break;
		case VOL_TYPE_VSTAPE:
			density = DENSITY_VSTAPE_DEFAULT;
			tdrive->mode_header.medium_type = MEDIUM_TYPE_VSTAPE;
			break;
		default:
			break;
	}

	tdrive->block_descriptor.density_code = density;
	return 0;
}

static void
vsdlt_update_device_configuration_page(struct tdrive *tdrive)
{
	struct device_configuration_page *page = &tdrive->configuration_page;

	page->rew |= 0x40; /* BIS */
	page->write_delay_time = htobe16(100);
	page->select_data_compression_algorithm = 0x01;
	return;
}

struct sdlt_specific_sense {
	uint8_t internal_status_code;
	uint16_t tape_motion_hours;
	uint32_t power_on_hours;
	uint32_t tape_remaining;
} __attribute__ ((__packed__));

#define ERRORS_SINCE_LAST	0x8000
#define TOTAL_RAW_WRITE_ERRORS	0x8001
#define TOTAL_DROPOUT_ERRORS	0x8002
#define TOTAL_SERVO_TRACKING_ERRORS	0x8003	

/* For now only report errors since last query */
static void
vsdlt_request_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio)
{
	struct qs_sense_data *sense_data = (struct qs_sense_data *)ctio->data_ptr;
	struct sdlt_specific_sense sense;
	int extra_len = offsetof(struct qs_sense_data, extra_bytes) + sizeof(struct sdlt_specific_sense) - offsetof(struct qs_sense_data, cmd_spec_info);
	int min_len;

	bzero(&sense, sizeof(sense));

	if (tdrive->tape)
	{
		uint64_t tape_used;
		uint32_t tape_remaining;
		uint8_t shift = 12;

		switch (tdrive->make)
		{
			/* Seems like it reports 8k blocks */
			case DRIVE_TYPE_VHP_DLTVS160:
				shift = 13;
				break;	
			default:
				break;
		}

		tape_used = tape_usage(tdrive->tape);
		tape_remaining = (uint32_t)((tdrive->tape->size - tape_used) >> shift); /* 4k blocks */
		sense.tape_remaining = htobe32(tape_remaining);
	}

 	min_len = min_t(int, ctio->dxfer_len - offsetof(struct qs_sense_data, extra_bytes), sizeof(struct sdlt_specific_sense));
	if (min_len > 0)
	{
		memcpy(sense_data->extra_bytes, &sense, min_len);
	}
	sense_data->extra_len = extra_len;
	return;
}

static int
vsdlt_valid_medium(struct tdrive *tdrive, int voltype)
{
	switch(tdrive->make)
	{
		case DRIVE_TYPE_VHP_DLTVS80:
			if (voltype != VOL_TYPE_DLT_4)
			{
				return -1;
			}
			return 0;
		case DRIVE_TYPE_VHP_DLTVS160:
			if (voltype != VOL_TYPE_VSTAPE)
			{
				return -1;
			}
			return 0;
		case DRIVE_TYPE_VHP_SDLT220:
		case DRIVE_TYPE_VQUANTUM_SDLT220:
			if (voltype != VOL_TYPE_SDLT_1)
			{
				return -1;
			}
			return 0;
		case DRIVE_TYPE_VHP_SDLT320:
		case DRIVE_TYPE_VQUANTUM_SDLT320:
			if ((voltype != VOL_TYPE_SDLT_1) && (voltype != VOL_TYPE_SDLT_2))
			{
				return -1;
			}
			return 0;
		case DRIVE_TYPE_VHP_SDLT600:
		case DRIVE_TYPE_VQUANTUM_SDLT600:
			if ((voltype != VOL_TYPE_SDLT_1) && (voltype != VOL_TYPE_SDLT_2) && (voltype != VOL_TYPE_SDLT_3))
			{
				return -1;
			}
			return 0;

		default:
			break;
	}

	return -1;
}

static void
tdrive_add_dlt4_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_DLT_4_DEFAULT;
	desc->sdensity_code = DENSITY_DLT_4_DEFAULT;
	desc->wrtok = wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 577250;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(0);
	desc->tracks = htobe16(0);
	desc->capacity = htobe32(38146);

	sys_memset(desc->organization, ' ', 8);
	sys_memset(desc->density_name, ' ', 8);
	sys_memset(desc->description, ' ', 20);
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
tdrive_add_vstape_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_VSTAPE_DEFAULT;
	desc->sdensity_code = DENSITY_VSTAPE_DEFAULT;
	desc->wrtok = wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 4375000;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(0);
	desc->tracks = htobe16(0);
	desc->capacity = htobe32(76293);

	sys_memset(desc->organization, ' ', 8);
	sys_memset(desc->density_name, ' ', 8);
	sys_memset(desc->description, ' ', 20);
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
tdrive_add_sdlt220_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_SUPER_DLT_2_DEFAULT;
	desc->sdensity_code = DENSITY_SUPER_DLT_2_DEFAULT;
	desc->wrtok = wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 3325000;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(0);
	desc->tracks = htobe16(0);
	desc->capacity = htobe32(104904);

	sys_memset(desc->organization, ' ', 8);
	sys_memset(desc->density_name, ' ', 8);
	sys_memset(desc->description, ' ', 20);
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
tdrive_add_sdlt600_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_SUPER_DLT_3_DEFAULT;
	desc->sdensity_code = DENSITY_SUPER_DLT_3_DEFAULT;
	desc->wrtok = wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 5825000;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(0);
	desc->tracks = htobe16(0);
	desc->capacity = htobe32(152587);

	sys_memset(desc->organization, ' ', 8);
	sys_memset(desc->density_name, ' ', 8);
	sys_memset(desc->description, ' ', 20);
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
tdrive_add_sdlt320_density_descriptor(struct tdrive *tdrive, uint8_t isdefault, uint8_t wrtok)
{
	struct density_descriptor *desc;
	uint32_t bits_per_mm;

	desc = zalloc(sizeof(struct density_descriptor), M_DRIVE, Q_WAITOK);
	desc->pdensity_code = DENSITY_SUPER_DLT_2_DEFAULT;
	desc->sdensity_code = DENSITY_SUPER_DLT_2_DEFAULT;
	desc->wrtok = wrtok << 7;
	desc->wrtok |= isdefault << 5;
	bits_per_mm = 4750000;
	desc->bits_per_mm[0] = (bits_per_mm >> 16) & 0xFF;
	desc->bits_per_mm[1] = (bits_per_mm >> 8) & 0xFF;
	desc->bits_per_mm[2] = (bits_per_mm) & 0xFF;
	desc->media_width = htobe16(0);
	desc->tracks = htobe16(0);
	desc->capacity = htobe32(292968);

	sys_memset(desc->organization, ' ', 8);
	sys_memset(desc->density_name, ' ', 8);
	sys_memset(desc->description, ' ', 20);
	SLIST_INSERT_HEAD(&tdrive->density_list, desc, d_list);
}

static void
vsdlt_init_density_support_descriptors(struct tdrive *tdrive)
{
	switch(tdrive->make)
	{
		case DRIVE_TYPE_VHP_DLTVS80:
			tdrive_add_dlt4_density_descriptor(tdrive, 1, 1);
			break;
		case DRIVE_TYPE_VHP_DLTVS160:
			tdrive_add_vstape_density_descriptor(tdrive, 1, 1);
			break;
		case DRIVE_TYPE_VHP_SDLT220:
		case DRIVE_TYPE_VQUANTUM_SDLT220:
			tdrive_add_sdlt220_density_descriptor(tdrive, 1, 1);
			break;
		case DRIVE_TYPE_VHP_SDLT320:
		case DRIVE_TYPE_VQUANTUM_SDLT320:
			tdrive_add_sdlt320_density_descriptor(tdrive, 1, 1);
			tdrive_add_sdlt220_density_descriptor(tdrive, 0, 1);
			break;
		case DRIVE_TYPE_VHP_SDLT600:
		case DRIVE_TYPE_VQUANTUM_SDLT600:
			tdrive_add_sdlt600_density_descriptor(tdrive, 1, 1);
			tdrive_add_sdlt320_density_descriptor(tdrive, 0, 1);
			tdrive_add_sdlt220_density_descriptor(tdrive, 0, 1);
			break;
		default:
			debug_check(1);
	}
}

void
vsdlt_init_handlers(struct tdrive *tdrive)
{
	struct tdrive_handlers *handlers = &tdrive->handlers;
	char *product_id, *vendor_id;

	handlers->init_inquiry_data = vsdlt_init_inquiry_data;
	handlers->load_tape = vsdlt_load_tape;
	handlers->unload_tape = vsdlt_unload_tape;
	handlers->additional_request_sense = vsdlt_request_sense;
	handlers->valid_medium = vsdlt_valid_medium;

	tdrive->add_sense_len = sizeof(struct sdlt_specific_sense);
	tdrive->evpd_info.num_pages = 0x04;
	tdrive->evpd_info.page_code[0] = VITAL_PRODUCT_DATA_PAGE;
	tdrive->evpd_info.page_code[1] = UNIT_SERIAL_NUMBER_PAGE;
	tdrive->evpd_info.page_code[2] = DEVICE_IDENTIFICATION_PAGE;
	tdrive->evpd_info.page_code[3] = EXTENDED_INQUIRY_VPD_PAGE;

	tdrive->log_info.num_pages = 0x01; 
	tdrive->log_info.page_code[0] = 0x00;

	/* Ideally we should be moving this elsewhere */
	vsdlt_update_device_configuration_page(tdrive);

	switch (tdrive->make)
	{
		case DRIVE_TYPE_VHP_DLTVS80:
			product_id = PRODUCT_ID_HP_VS80;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VHP_DLTVS160:
			product_id = PRODUCT_ID_HP_VS160;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VHP_SDLT220:
			product_id = PRODUCT_ID_HP_SDLT1;
			vendor_id = VENDOR_ID_COMPAQ;
			break;
		case DRIVE_TYPE_VHP_SDLT320:
			product_id = PRODUCT_ID_SDLT320;
			vendor_id = VENDOR_ID_COMPAQ;
			break;
		case DRIVE_TYPE_VHP_SDLT600:
			product_id = PRODUCT_ID_SDLT600;
			vendor_id = VENDOR_ID_HP;
			break;
		case DRIVE_TYPE_VQUANTUM_SDLT220:
			product_id = PRODUCT_ID_SDLT220;
			vendor_id = VENDOR_ID_QUANTUM;
			break;
		case DRIVE_TYPE_VQUANTUM_SDLT320:
			product_id = PRODUCT_ID_SDLT320;
			vendor_id = VENDOR_ID_QUANTUM;
			break;
		case DRIVE_TYPE_VQUANTUM_SDLT600:
			product_id = PRODUCT_ID_SDLT600;
			vendor_id = VENDOR_ID_QUANTUM;
			break;
		default:
			debug_check(1);
			return;
	}

	device_init_unit_identifier(&tdrive->unit_identifier, vendor_id, product_id, tdrive->serial_len);
	tdrive->erase_from_bot = 1;

	vsdlt_init_density_support_descriptors(tdrive);
	return;
}


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


static int
vibmtl_device_identification(struct mchanger *mchanger, uint8_t *buffer, int length)
{
	return mchanger_device_identification(mchanger, buffer, length);
}

static int
ibm_serial_number(struct mchanger *mchanger, uint8_t *buffer, int length)
{
	struct serial_number_page *page = (struct serial_number_page *) buffer;
	int min_len;

	if (length < sizeof(struct vital_product_page))
		return -1;

	bzero(page, sizeof(struct vital_product_page));
	page->device_type = T_CHANGER; /* peripheral qualifier */
	page->page_code = UNIT_SERIAL_NUMBER_PAGE;
	page->page_length = mchanger->serial_len + 0x04;

	min_len = min_t(int, mchanger->serial_len + 0x04, length - sizeof(struct vital_product_page));
	if (min_len) {
		char *tmp;

		memcpy(page->serial_number, mchanger->unit_identifier.serial_number, min_len - 0x04);
		if (length >= (sizeof(struct vital_product_page) + mchanger->serial_len + 0x04)) {
			tmp = ((char *)page->serial_number) + mchanger->serial_len;
			if (mchanger->make == LIBRARY_TYPE_VIBM_3584)
				snprintf(tmp, 4, "%04X", get_first_element_address(mchanger, STORAGE_ELEMENT));
			else if (mchanger->make == LIBRARY_TYPE_VIBM_TS3100)
				memcpy(tmp, "_LL3", 4);
		}
	}
	return (min_len+sizeof(struct vital_product_page));
}

static int
vibmtl_serial_number(struct mchanger *mchanger, uint8_t *buffer, int length)
{
	switch (mchanger->make) {
	case LIBRARY_TYPE_VIBM_3583:
		return mchanger_serial_number(mchanger, buffer, length);
	case LIBRARY_TYPE_VIBM_3584:
	case LIBRARY_TYPE_VIBM_TS3100:
		return ibm_serial_number(mchanger, buffer, length);
	default:
		debug_check(1);
		return 0;
	}
}

static int
vibmtl_evpd_inquiry(struct mchanger *mchanger, struct qsio_scsiio *ctio, uint8_t page_code, uint16_t allocation_length)
{
	int retval;

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (!ctio->data_ptr)
	{
		return -1;
	}

	bzero(ctio->data_ptr, allocation_length);

	switch (page_code)
	{
		case UNIT_SERIAL_NUMBER_PAGE:
			retval = vibmtl_serial_number(mchanger, ctio->data_ptr, allocation_length);
			if (retval < 0)
			{
				goto err;
			}
			ctio->dxfer_len = retval;
			break;
		case DEVICE_IDENTIFICATION_PAGE:
			retval = vibmtl_device_identification(mchanger, ctio->data_ptr, allocation_length);
			if (retval < 0)
			{
				goto err;
			}
			ctio->dxfer_len = retval;
			break;
		case VITAL_PRODUCT_DATA_PAGE:
			retval = mchanger_copy_vital_product_page_info(mchanger, ctio->data_ptr, allocation_length);
			if (retval < 0)
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

static void vibmtl_init_inquiry_data(struct mchanger* mchanger)
{
	struct inquiry_data *inquiry = &mchanger->inquiry;
	char *ptr;

	bzero(inquiry, sizeof(*inquiry));
	inquiry->device_type = T_CHANGER;
	inquiry->rmb |= RMB_MASK; /* Set the removal bit to one */
	if (mchanger->make != LIBRARY_TYPE_VIBM_TS3100)
	{
		inquiry->version = ANSI_VERSION_SCSI3; /* Current supported version. Need to do it a better way */
	}
	else
	{
		inquiry->version = ANSI_VERSION_SCSI3_SPC3; /* Current supported version. Need to do it a better way */
	}
	inquiry->response_data = RESPONSE_DATA; 
	inquiry->additional_length = STANDARD_INQUIRY_LEN_MC - 5; /* n - 4 */
	memcpy(&inquiry->vendor_id, mchanger->unit_identifier.vendor_id, 8);
	memcpy(&inquiry->product_id, mchanger->unit_identifier.product_id, 16);
	memcpy(&inquiry->revision_level, PRODUCT_REVISION_QUADSTOR, strlen(PRODUCT_REVISION_QUADSTOR));
	ptr = inquiry->vendor_specific;
	memcpy(ptr, "AB", 2);
	ptr += 2;
	memcpy(ptr, mchanger->unit_identifier.serial_number, mchanger->serial_len);
	if (mchanger->make == LIBRARY_TYPE_VIBM_3584)
		inquiry->mchangr = 0x20; /* Barcode scanner attached */
	else
		inquiry->vendor_specific[19] = 0x01; /* Barcode scanner attached */
}

static inline void
ibm_init_unit_identifier(struct mchanger *mchanger, char *vendor_id, char *product_id, int serial_len)
{
	struct logical_unit_identifier *unit_identifier = &mchanger->unit_identifier;
	char *tmp;

	unit_identifier->code_set = 0x02; /*logical unit idenifier */
	unit_identifier->identifier_type = UNIT_IDENTIFIER_T10_VENDOR_ID;
	sys_memset(unit_identifier->vendor_id, ' ', 8);
	strncpy(unit_identifier->vendor_id, vendor_id, strlen(vendor_id));
	sys_memset(unit_identifier->product_id, ' ', 16);
	strncpy(unit_identifier->product_id, product_id, strlen(product_id));
	unit_identifier->identifier_length = offsetof(struct logical_unit_identifier, serial_number) - offsetof(struct logical_unit_identifier, vendor_id);
	unit_identifier->identifier_length += serial_len;
	tmp = (((char *)(unit_identifier)) + 40);
	if (mchanger->make == LIBRARY_TYPE_VIBM_3584)
	{
		sprintf(tmp, "%04X", get_first_element_address(mchanger, STORAGE_ELEMENT));
		unit_identifier->identifier_length += 0x4;
	}
	else if (mchanger->make == LIBRARY_TYPE_VIBM_TS3100)
	{
		memcpy(tmp, "_LL3", 4);
		unit_identifier->identifier_length += 0x4;
	}
}

void
vibmtl_init_handlers(struct mchanger *mchanger)
{
	struct mchanger_handlers *handlers = &mchanger->handlers;
	char *product_id;

	handlers->init_inquiry_data = vibmtl_init_inquiry_data;
	handlers->evpd_inquiry = vibmtl_evpd_inquiry;
	mchanger->evpd_info.num_pages = 0x03;
	mchanger->evpd_info.page_code[0] = VITAL_PRODUCT_DATA_PAGE;
	mchanger->evpd_info.page_code[1] = DEVICE_IDENTIFICATION_PAGE;
	mchanger->evpd_info.page_code[2] = UNIT_SERIAL_NUMBER_PAGE;
	mchanger->supports_evpd = 1;

	switch (mchanger->make)
	{
		case LIBRARY_TYPE_VIBM_3583:
			product_id = PRODUCT_ID_VIBM_3583;
			device_init_unit_identifier(&mchanger->unit_identifier, VENDOR_ID_IBM, product_id, mchanger->serial_len);
			break;
		case LIBRARY_TYPE_VIBM_3584:
			product_id = PRODUCT_ID_VIBM_3584;
			ibm_init_unit_identifier(mchanger, VENDOR_ID_IBM, product_id, mchanger->serial_len);
			break;
		case LIBRARY_TYPE_VIBM_TS3100:
			product_id = PRODUCT_ID_VIBM_TS3100;
			ibm_init_unit_identifier(mchanger, VENDOR_ID_IBM, product_id, mchanger->serial_len);
			break;
		default:
			debug_check(1);
			return;
	}

	mchanger->supports_devid = 1;
	return;
}

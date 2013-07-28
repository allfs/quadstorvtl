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

static void vhptl_init_inquiry_data(struct mchanger* mchanger)
{
	struct inquiry_data *inquiry = &mchanger->inquiry;

	bzero(inquiry, sizeof(*inquiry));
	inquiry->device_type = T_CHANGER;
	inquiry->rmb |= RMB_MASK; /* Set the removal bit to one */
	inquiry->version = ANSI_VERSION_SCSI3; /* Current supported version. Need to do it a better way */
	inquiry->response_data = RESPONSE_DATA; 
	inquiry->additional_length = STANDARD_INQUIRY_LEN_MC - 5; /* n - 4 */
	memcpy(&inquiry->vendor_id, mchanger->unit_identifier.vendor_id, 8);
	memcpy(&inquiry->product_id, mchanger->unit_identifier.product_id, 16);
	memcpy(&inquiry->revision_level, PRODUCT_REVISION_QUADSTOR, strlen(PRODUCT_REVISION_QUADSTOR));
	sys_memset(inquiry->vendor_specific, ' ', sizeof(inquiry->vendor_specific));
	memcpy(inquiry->vendor_specific, PRODUCT_REVISION_FULL, strlen(PRODUCT_REVISION_FULL));
	inquiry->vendor_specific[19] = 0x01; /* Barcode scanner attached */
	return;
}

void
vhptl_init_handlers(struct mchanger *mchanger)
{
	struct mchanger_handlers *handlers = &mchanger->handlers;
	char *product_id;
	char *vendor_id = VENDOR_ID_HP;
	uint8_t serial_as_avoltag = 0;

	handlers->init_inquiry_data = vhptl_init_inquiry_data;

	switch (mchanger->make)
	{
		case LIBRARY_TYPE_VHP_ESL9000:
			product_id = PRODUCT_ID_VHP_ESL9000;
			serial_as_avoltag = 1;
			break;
		case LIBRARY_TYPE_VHP_ESLSERIES:
			product_id = PRODUCT_ID_VHP_ESLSERIES;
			serial_as_avoltag = 1;
			break;
		case LIBRARY_TYPE_VHP_EMLSERIES:
			product_id = PRODUCT_ID_VHP_EMLSERIES;
			break;
		case LIBRARY_TYPE_VHP_MSLSERIES:
			product_id = PRODUCT_ID_VHP_MSLSERIES;
			break;
		case LIBRARY_TYPE_VHP_MSL6000:
			product_id = PRODUCT_ID_VHP_MSL6000;
			serial_as_avoltag = 1;
			break;
		case LIBRARY_TYPE_VOVL_NEOSERIES:
			vendor_id = VENDOR_ID_OVERLAND;
			product_id = PRODUCT_ID_VOVL_NEOSERIES;
			serial_as_avoltag = 1;
			break;
		default:
			debug_check(1);
			return;
	}

	mchanger->serial_as_avoltag = serial_as_avoltag;
	device_init_unit_identifier(&mchanger->unit_identifier, vendor_id, product_id, mchanger->serial_len);
}

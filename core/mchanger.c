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
#include "mchanger.h"
#include "sense.h"
#include "vendor.h"
#include "vdevdefs.h"

static int mchanger_initialize_pages(struct mchanger *mchanger);
static void mchanger_init_transport_geometry_page(struct mchanger *mchanger);
static void mchanger_init_device_capabilities(struct mchanger *mchanger);

static uint8_t get_mchanger_element_invert(struct mchanger_element *element);

static struct mchanger_element_list *
mchanger_elem_list_type(struct mchanger *mchanger, int type)
{
	switch (type)
	{
		case MEDIUM_TRANSPORT_ELEMENT:
			return &mchanger->melem_list;
		case STORAGE_ELEMENT:
			return &mchanger->selem_list;
		case IMPORT_EXPORT_ELEMENT:
			return &mchanger->ielem_list;
		case DATA_TRANSFER_ELEMENT:
			return &mchanger->delem_list;
	}
	return NULL;
}

static void
mchanger_element_insert(struct mchanger *mchanger, struct mchanger_element *element)
{
	struct mchanger_element_list *element_list;

	element_list = mchanger_elem_list_type(mchanger, element->type);
	STAILQ_INSERT_TAIL(element_list, element, me_list);
}

static struct mchanger_element_list *
mchanger_elem_list(struct mchanger *mchanger, struct mchanger_element_list *prev_list)
{
	if (!prev_list)
		return &mchanger->melem_list;

	if (prev_list == &mchanger->melem_list)
		return &mchanger->selem_list;

	if (prev_list == &mchanger->selem_list)
		return &mchanger->ielem_list;

	if (prev_list == &mchanger->ielem_list)
		return &mchanger->delem_list;

	return NULL;
}

static uint16_t
get_next_element_address(struct mchanger *mchanger, int type)
{
	switch (type) {
	case MEDIUM_TRANSPORT_ELEMENT:
		return (DEFAULT_MT_START_ADDRESS + mchanger->changers);
	case STORAGE_ELEMENT:
		return DEFAULT_ST_START_ADDRESS + mchanger->slots;
	case DATA_TRANSFER_ELEMENT:
		return (DEFAULT_DT_START_ADDRESS + mchanger->drives);
	case IMPORT_EXPORT_ELEMENT:
		return (DEFAULT_IE_START_ADDRESS + mchanger->ieports);
	}
	return 0;
}

struct mchanger_element *
mchanger_add_element(struct mchanger *mchanger, int type, void *element_data)
{
	struct mchanger_element *element;
	int address = 1; /* Elements start from address 1 */
	uint8_t flags = ELEMENT_DESCRIPTOR_ACCESS_MASK; 

	element = zalloc(sizeof(struct mchanger_element), M_MCHANGERELEMENT, Q_WAITOK);
	address = get_next_element_address(mchanger, type);
	element->address = address;
	element->type = type;
	element->element_data = element_data;

	mchanger->num_elements++;
	switch (type) {
	case STORAGE_ELEMENT:
		mchanger->slots++;
		break;
	case DATA_TRANSFER_ELEMENT:
		mchanger->drives++;
		break;
	case IMPORT_EXPORT_ELEMENT:
		mchanger->ieports++;
		break;
	case MEDIUM_TRANSPORT_ELEMENT:
		mchanger->changers++;
		break;
	default:
		debug_check(1);
		break;
	}

	mchanger_element_insert(mchanger, element);
	mchanger_init_address_assignment_page(mchanger);
	update_mchanger_element_pvoltag(element);
	update_element_descriptor_address(element);
	update_mchanger_element_flags(element, get_mchanger_element_flags(element) | flags);

	return element;
}

void
mchanger_unit_attention_ie_accessed(struct mchanger *mchanger)
{
	device_unit_attention(&mchanger->tdevice, 1, 0ULL, 0ULL, 0, IMPORT_EXPORT_ACCESSED_ASC, IMPORT_EXPORT_ACCESSED_ASCQ, 1);
}

void
mchanger_unit_attention_medium_changed(struct mchanger *mchanger)
{
	device_unit_attention(&mchanger->tdevice, 1, 0ULL, 0ULL, 0, MEDIUM_MAY_HAVE_CHANGED_ASC, MEDIUM_MAY_HAVE_CHANGED_ASCQ, 1);
}

void
mchanger_unit_attention_mode_changed(struct mchanger *mchanger)
{
	device_unit_attention(&mchanger->tdevice, 1, 0ULL, 0ULL, 0, MODE_PARAMETERS_CHANGED_ASC, MODE_PARAMETERS_CHANGED_ASCQ, 1);
}

struct tape *
element_vcartridge(struct mchanger_element *element)
{
	struct tdrive *tdrive;

	switch(element->type)
	{
		case STORAGE_ELEMENT:
		case IMPORT_EXPORT_ELEMENT:
			return (element->element_data);
		case MEDIUM_TRANSPORT_ELEMENT:
			return NULL;
		case DATA_TRANSFER_ELEMENT:
			tdrive = (struct tdrive *)(element->element_data);
			if (tdrive)
				return tdrive->tape;
			return NULL;
	}
	debug_check(1);
	return NULL;
}

static void
mchanger_element_free(struct mchanger_element *element, int delete)
{
	if (element->type == DATA_TRANSFER_ELEMENT && element->element_data) {
		tdrive_free((struct tdrive *)element->element_data, delete);
	}
	else if (element->type == STORAGE_ELEMENT || element->type == IMPORT_EXPORT_ELEMENT)
	{
		struct tape *tape = element->element_data;

		if (tape && !tape->locked)
		{
			tape_flush_buffers(tape);
			tape_free(tape, delete);
		}
	}
	free(element, M_MCHANGERELEMENT);
}

int
mchanger_mod_device(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo)
{
	return 0;
}

static int 
mchanger_init(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo)
{
	int retval;

	mchanger->mchanger_lock = sx_alloc("mchanger lock");
	STAILQ_INIT(&mchanger->melem_list);
	STAILQ_INIT(&mchanger->selem_list);
	STAILQ_INIT(&mchanger->ielem_list);
	STAILQ_INIT(&mchanger->delem_list);
	LIST_INIT(&mchanger->export_list);

	retval = tdevice_init(&mchanger->tdevice, T_CHANGER, deviceinfo->tl_id, 0, deviceinfo->name, mchanger_proc_cmd, "mchngr");
	if (unlikely(retval != 0))
		return -1;
	return 0;
}

static void
mchanger_construct_serial_number(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo)
{
	switch (mchanger->make) {	
	case LIBRARY_TYPE_VADIC_SCALAR24:
	case LIBRARY_TYPE_VADIC_SCALAR100:
	case LIBRARY_TYPE_VADIC_SCALARi2000:
		sprintf(mchanger->unit_identifier.serial_number, "QTLS%03X%03X", deviceinfo->tl_id, deviceinfo->target_id);
		break;
	case LIBRARY_TYPE_VHP_ESL9000:
	case LIBRARY_TYPE_VHP_ESLSERIES:
	case LIBRARY_TYPE_VHP_EMLSERIES:
	case LIBRARY_TYPE_VHP_MSLSERIES:
	case LIBRARY_TYPE_VHP_MSL6000:
	case LIBRARY_TYPE_VOVL_NEOSERIES:
		sprintf(mchanger->unit_identifier.serial_number, "QTLS%03X%03X", deviceinfo->tl_id, deviceinfo->target_id);
		break;
	case LIBRARY_TYPE_VIBM_3583:
		sprintf(mchanger->unit_identifier.serial_number, "IBMQTLS%04X%03X", deviceinfo->tl_id, deviceinfo->target_id);
		break;
	case LIBRARY_TYPE_VIBM_3584:
	case LIBRARY_TYPE_VIBM_TS3100:
		sprintf(mchanger->unit_identifier.serial_number, "QTLS%04X%04X", deviceinfo->tl_id, deviceinfo->target_id);
		break;
	default:
		debug_check(1);
		break;
	}
}

struct mchanger *
mchanger_new(struct vdeviceinfo *deviceinfo)
{
	struct mchanger *mchanger;
	struct mchanger_element *element;
	int i, retval;;

	mchanger = zalloc(sizeof(struct mchanger), M_MCHANGER, Q_WAITOK);
	if (unlikely(!mchanger))
	{
		return NULL;
	}

	mchanger->make = deviceinfo->make;
	mchanger->vtype = deviceinfo->vtype;
	mchanger_construct_serial_number(mchanger, deviceinfo);
	strcpy(deviceinfo->serialnumber, mchanger->unit_identifier.serial_number);
	mchanger->serial_len = strlen(mchanger->unit_identifier.serial_number);
	retval = mchanger_init(mchanger, deviceinfo);
	if (unlikely(retval != 0)) {
		free(mchanger, M_MCHANGER);
		return NULL;
	}

	mchanger_add_element(mchanger, MEDIUM_TRANSPORT_ELEMENT, NULL);
	for (i = 0; i < deviceinfo->slots; i++) {
		/* NULL for now */
		/* would be filled in when a tape vcartridge is added */
		mchanger_add_element(mchanger, STORAGE_ELEMENT, NULL);
	}

	for (i = 0; i < deviceinfo->ieports; i++) {
		element = mchanger_add_element(mchanger, IMPORT_EXPORT_ELEMENT, NULL);
		update_mchanger_element_flags(element, get_mchanger_element_flags(element) | IE_MASK_INENAB | IE_MASK_EXENAB);
	}

	mchanger_initialize_pages(mchanger);
	return mchanger;
}

static void
__mchanger_free_elements(struct mchanger_element_list *element_list, int delete)
{
	struct mchanger_element *element;

	while ((element = STAILQ_FIRST(element_list)) != NULL) {
		STAILQ_REMOVE_HEAD(element_list, me_list);
		mchanger_element_free(element, delete);
	}
}


static void
mchanger_free_elements(struct mchanger *mchanger, int delete)
{
	__mchanger_free_elements(&mchanger->melem_list, delete);
	__mchanger_free_elements(&mchanger->selem_list, delete);
	__mchanger_free_elements(&mchanger->ielem_list, delete);
	__mchanger_free_elements(&mchanger->delem_list, delete);
}

struct tdrive *
mchanger_add_tdrive(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo)
{
	struct tdrive *tdrive;
	struct mchanger_element *element;

	mchanger_lock(mchanger);
	if (mchanger->drives > TL_MAX_DRIVES) {
		debug_warn("Not adding new drive as count exceeded max limit\n");
		goto err;
	}

	tdrive = tdrive_new(mchanger, deviceinfo);

	if (!tdrive)
		goto err;

	element = mchanger_add_element(mchanger, DATA_TRANSFER_ELEMENT, tdrive);

	memcpy(&element->edesc.identifier, &tdrive->unit_identifier, sizeof(struct logical_unit_identifier)); 
	if (element->edesc.identifier.identifier_length > mchanger->devid_len)
		mchanger->devid_len = element->edesc.identifier.identifier_length;

	memcpy(element->serialnumber, tdrive->unit_identifier.serial_number, tdrive->serial_len);
	mchanger_unlock(mchanger);
	return tdrive;
err:
	mchanger_unlock(mchanger);
	return NULL;
}

static struct mchanger_element *
get_free_ie_element(struct mchanger *mchanger)
{
	struct mchanger_element *element;

	STAILQ_FOREACH(element, &mchanger->ielem_list, me_list) {
		debug_check(element->type != IMPORT_EXPORT_ELEMENT);
		if (!element->element_data)
			return element;
	}
	return NULL;
}

static int
storage_element_in_use(struct mchanger *mchanger, struct mchanger_element *storage_element)
{
	struct mchanger_element *element;
	struct element_descriptor *edesc;
	uint16_t source_address;

	STAILQ_FOREACH(element, &mchanger->delem_list, me_list) {
		if (!element_vcartridge(element))
			continue;
 		edesc = &element->edesc;
		source_address = be16toh(edesc->common.source_storage_element_address);
		if (source_address == storage_element->address)
			return 1;
	}
	return 0;
}

static struct mchanger_element *
get_free_storage_element(struct mchanger *mchanger)
{
	struct mchanger_element *element;

	STAILQ_FOREACH(element, &mchanger->selem_list, me_list) {
		debug_check(element->type != STORAGE_ELEMENT);
		if (element->element_data)
			continue;
		if (storage_element_in_use(mchanger, element))
			continue;
		return element;
	}

	return NULL;
}

static int
element_address_valid_for_voltype(struct mchanger_element *element, int vol_type)
{
	int retval;

	if (!element || element->type == MEDIUM_TRANSPORT_ELEMENT)
		return 0;

	if (element_vcartridge(element))
		return 0;

	if (element->type != DATA_TRANSFER_ELEMENT)
		return 1;

	retval = tdrive_media_valid((struct tdrive *)element->element_data, vol_type);
	return (retval == 0);
}

static struct mchanger_element *
get_free_element(struct mchanger *mchanger, int eaddress, int vol_type)
{
	struct mchanger_element *element = NULL, *ie_element = NULL;

	if (eaddress) {
		element = mchanger_get_element(mchanger, eaddress);
		if (element_address_valid_for_voltype(element, vol_type))
			return element;
	}

	element = get_free_storage_element(mchanger);
	if (!element)
		return NULL;

	if (!SLIST_EMPTY(&mchanger->tdevice.istate_list)) {
		ie_element = get_free_ie_element(mchanger);
		if (ie_element)
		{
			element = ie_element;
		}
	}
	return element;
}

static void
mchanger_remove_export_tape(struct mchanger *mchanger, struct tape *tape)
{
	LIST_REMOVE_INIT(tape, t_list);
}

static void
mchanger_insert_export_tape(struct mchanger *mchanger, struct tape *tape)
{
	LIST_INSERT_HEAD(&mchanger->export_list, tape, t_list);
}

int
mchanger_load_vcartridge(struct mchanger *mchanger, struct vcartridge *vinfo)
{
	struct tape *tape;
	struct raw_tape *raw_tape;
	struct mchanger_element *element = NULL;

	element = get_free_element(mchanger, vinfo->elem_address, vinfo->type);
	if (!element) {
		debug_warn("Cannot get a free element for tape\n");
		return -1;
	}	
	
	tape = tape_load((struct tdevice *)mchanger, vinfo);
	if (!tape)
		return -1;

	raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));
	if (raw_tape->vstatus & MEDIA_STATUS_EXPORTED) {
		mchanger_insert_export_tape(mchanger, tape);
		return 0;
	}

	if (element->type == DATA_TRANSFER_ELEMENT)
		tdrive_load_tape((struct tdrive *)element->element_data, tape);
	else if (element->type == IMPORT_EXPORT_ELEMENT) {
		element->element_data = tape;
		update_mchanger_element_flags(element, get_mchanger_element_flags(element) | IE_MASK_IMPEXP);
	}
	else 
		element->element_data = tape;

	update_mchanger_element_flags(element, get_mchanger_element_flags(element) | ELEMENT_DESCRIPTOR_ACCESS_MASK | ELEMENT_DESCRIPTOR_FULL_MASK);
	update_mchanger_element_pvoltag(element);
	return 0;
}

void
mchanger_init_tape_metadata(struct tdevice *tdevice, struct tape *tape)
{
	struct raw_tape *raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));
	struct mchanger *mchanger = (struct mchanger *)tdevice;
	struct vtl_info *vtl_info = &raw_tape->vtl_info;
	struct tdrive *tdrive;
	struct mchanger_element *element;
	struct drive_info *drive_info;

	strcpy(vtl_info->name, tdevice->name);
	strcpy(vtl_info->serialnumber, mchanger->unit_identifier.serial_number); 
	vtl_info->tl_id = tdevice->tl_id;
	vtl_info->type = mchanger->make;
	vtl_info->slots = mchanger->slots;
	vtl_info->drives = mchanger->drives;
	vtl_info->ieports = mchanger->ieports;

	drive_info = &vtl_info->drive_info[0];
	STAILQ_FOREACH(element, &mchanger->delem_list, me_list) {
		tdrive = (struct tdrive *)(element->element_data);
		drive_info->target_id = tdrive->tdevice.target_id;
		drive_info->make = tdrive->make;
		strcpy(drive_info->name, tdrive->tdevice.name);
		strcpy(drive_info->serialnumber, tdrive->unit_identifier.serial_number);
		drive_info++;
	}
}

int
mchanger_new_vcartridge(struct mchanger *mchanger, struct vcartridge *vinfo)
{
	struct tape *tape;
	struct mchanger_element *element;

	element = get_free_element(mchanger, 0, vinfo->type);

	if (!element) {
		debug_warn("Couldnt find an empty slot/ieport");
		return -1;
	}

	tape = tape_new((struct tdevice *)mchanger, vinfo);
	if (!tape)
		return -1;

	element->element_data = tape;
	update_mchanger_element_flags(element, get_mchanger_element_flags(element) | ELEMENT_DESCRIPTOR_ACCESS_MASK | ELEMENT_DESCRIPTOR_FULL_MASK);
	update_mchanger_element_pvoltag(element);
	if (element->type == IMPORT_EXPORT_ELEMENT) {
		update_mchanger_element_flags(element, get_mchanger_element_flags(element) | IE_MASK_IMPEXP);
		mchanger_unit_attention_ie_accessed(mchanger);
	}
	else
		mchanger_unit_attention_medium_changed(mchanger);

	return 0;
}

static struct mchanger_element *
__mchanger_get_element_type(struct mchanger_element_list *element_list, uint16_t address)
{
	struct mchanger_element *element;

	STAILQ_FOREACH(element, element_list, me_list) {
		if (element->address == address)
			return element;
	}
	return NULL;
}

struct mchanger_element *
mchanger_get_element_type(struct mchanger *mchanger, int type, uint16_t address)
{
	struct mchanger_element_list *element_list;
	element_list = mchanger_elem_list_type(mchanger, type);
	return __mchanger_get_element_type(element_list, address);
}

struct mchanger_element *
mchanger_get_drive_element(struct mchanger *mchanger, struct tdrive *tdrive)
{
	struct mchanger_element *element;

	STAILQ_FOREACH(element, &mchanger->delem_list, me_list) {
		if (element->element_data == (void *)tdrive)
			return element;
	}
	return NULL;
}

struct mchanger_element *
mchanger_get_element(struct mchanger *mchanger, uint16_t address)
{
	struct mchanger_element *element;
	struct mchanger_element_list *element_list = NULL;

	while ((element_list = mchanger_elem_list(mchanger, element_list)) != NULL) {
		element = __mchanger_get_element_type(element_list, address);
		if (element)
			return element;
	}
	return NULL;
}

static void
mchanger_free_export_list(struct mchanger *mchanger, int delete)
{
	struct tape *tape;

	while ((tape = LIST_FIRST(&mchanger->export_list)) != NULL) {
		LIST_REMOVE(tape, t_list);
		tape_flush_buffers(tape);
		tape_free(tape, delete);
	}
}

void
mchanger_cbs_disable(struct mchanger *mchanger)
{
	struct mchanger_element *element;

	cbs_disable_device((struct tdevice *)mchanger);
	STAILQ_FOREACH(element, &mchanger->delem_list, me_list) {
		tdrive_cbs_disable((struct tdrive *)element->element_data);
	}
}

void
mchanger_cbs_remove(struct mchanger *mchanger)
{
	struct mchanger_element *element;

	STAILQ_FOREACH(element, &mchanger->delem_list, me_list) {
		tdrive_cbs_remove((struct tdrive *)element->element_data);
	}
	device_wait_all_initiators(&mchanger->tdevice.istate_list);
	cbs_remove_device((struct tdevice *)mchanger);
}

void
mchanger_free(struct mchanger *mchanger, int delete)
{
	mchanger_cbs_disable(mchanger);
	mchanger_cbs_remove(mchanger);
	device_wait_all_initiators(&mchanger->tdevice.istate_list);
	cbs_remove_device((struct tdevice *)mchanger);
	tdevice_exit(&mchanger->tdevice);
	mchanger_free_export_list(mchanger, delete);
	mchanger_free_elements(mchanger, delete);
	sx_free(mchanger->mchanger_lock);
	free(mchanger, M_MCHANGER);
}

static struct mchanger_element *
get_first_element(struct mchanger *mchanger, int type)
{
	struct mchanger_element_list *element_list;

	element_list = mchanger_elem_list_type(mchanger, type);
	return STAILQ_FIRST(element_list);
}

uint16_t
get_first_element_address(struct mchanger *mchanger, int type)
{
	switch (type)
	{
		case MEDIUM_TRANSPORT_ELEMENT:
			if (mchanger->changers)
				return DEFAULT_MT_START_ADDRESS;
			break;
		case STORAGE_ELEMENT:
			if (mchanger->slots)
				return DEFAULT_ST_START_ADDRESS;
			break;
		case IMPORT_EXPORT_ELEMENT:
			if (mchanger->ieports)
				return DEFAULT_IE_START_ADDRESS;
			break;
		case DATA_TRANSFER_ELEMENT:
			if (mchanger->drives)
				return DEFAULT_DT_START_ADDRESS;
			break;
	}
	return 0;
}

/*
 * Each type of autoloader we emulate would define this function 
 */
void
mchanger_init_address_assignment_page(struct mchanger *mchanger)
{
	struct element_address_assignment_page *assignment_page = &mchanger->assignment_page;

	bzero(assignment_page, sizeof(*assignment_page));

	assignment_page->page_code = ELEMENT_ADDRESS_ASSIGNMENT_PAGE; 
	assignment_page->page_length = sizeof(struct element_address_assignment_page) - offsetof(struct element_address_assignment_page, first_medium_transport_element_address);

	assignment_page->first_medium_transport_element_address = htobe16(get_first_element_address(mchanger, MEDIUM_TRANSPORT_ELEMENT));
	assignment_page->number_of_medium_transport_elements = htobe16(mchanger->changers);

	assignment_page->first_storage_element_address = htobe16(get_first_element_address(mchanger, STORAGE_ELEMENT));
	assignment_page->number_of_storage_elements = htobe16(mchanger->slots);

	assignment_page->first_import_export_element_address = htobe16(get_first_element_address(mchanger, IMPORT_EXPORT_ELEMENT));
	assignment_page->number_of_import_export_elements = htobe16(mchanger->ieports);

	assignment_page->first_data_transfer_element_address = htobe16(get_first_element_address(mchanger, DATA_TRANSFER_ELEMENT));
	assignment_page->number_of_data_transfer_elements = htobe16(mchanger->drives);

}

static void
mchanger_init_transport_geometry_page(struct mchanger *mchanger)
{
	struct transport_geometry_descriptor_page *geometry_page = &mchanger->geometry_page;

	bzero(geometry_page, sizeof(*geometry_page));
	geometry_page->page_code = TRANSPORT_GEOMETRY_DESCRIPTOR_PAGE;
	geometry_page->page_length = sizeof(struct transport_geometry_descriptor_page) - offsetof(struct transport_geometry_descriptor_page, rotate);
	geometry_page->rotate = ROTATE_NOT_POSSIBLE;
	geometry_page->member_number = 0; /* first element is zero*/

}

static void
mchanger_init_device_capabilities(struct mchanger *mchanger)
{
	struct device_capabilities_page *devcap_page = &mchanger->devcap_page;

	bzero(devcap_page, sizeof(*devcap_page));
	devcap_page->page_code =  DEVICE_CAPABILITIES_PAGE; /* No page savable */
	devcap_page->page_length = sizeof(struct device_capabilities_page) - offsetof(struct device_capabilities_page, st_cap);
	devcap_page->st_cap = STORAGE_CAPABILITY_ST_MASK | STORAGE_CAPABILITY_IE_MASK | STORAGE_CAPABILITY_DT_MASK;
	/* ok this needs to be filled in a better way */
	devcap_page->mt_mvcmd = 0x0E;
	devcap_page->st_mvcmd = 0x0E;
	devcap_page->ie_mvcmd = 0x0E;
	devcap_page->dt_mvcmd = 0x0E;
	if (mchanger->exg_possible)
	{
		devcap_page->mt_excmd = 0x00;
		devcap_page->st_excmd = 0x0E;
		devcap_page->ie_excmd = 0x0E;
		devcap_page->dt_excmd = 0x0E;
	}
}	

static void
mchanger_init_inquiry_data(struct mchanger *mchanger)
{
	struct inquiry_data *inquiry = &mchanger->inquiry;

	bzero(inquiry, sizeof(*inquiry));
	inquiry->device_type = T_CHANGER;
	inquiry->rmb |= RMB_MASK; /* Set the removal bit to one */
	inquiry->version = ANSI_VERSION_SCSI3; /* Current supported version. Need to do it a better way */
	inquiry->response_data = RESPONSE_DATA; 
	inquiry->additional_length = STANDARD_INQUIRY_LEN_MC - 5; /* n - 4 */
	sys_memset(&inquiry->vendor_id, ' ', 8);
	memcpy(&inquiry->vendor_id, VENDOR_ID_QUADSTOR, strlen(VENDOR_ID_QUADSTOR));
	sys_memset(&inquiry->product_id, ' ', 16);
	memcpy(&inquiry->product_id, PRODUCT_ID_QUADSTOR, strlen(PRODUCT_ID_QUADSTOR));
	memcpy(&inquiry->revision_level, PRODUCT_REVISION_QUADSTOR, strlen(PRODUCT_REVISION_QUADSTOR));
	sys_memset(inquiry->vendor_specific, ' ', sizeof(inquiry->vendor_specific));
	inquiry->vendor_specific[19] = 0x01; /* Barcode scanner attached */
	return;
}

static void
mchanger_init_handlers(struct mchanger *mchanger)
{
	switch (mchanger->make)
	{
		case LIBRARY_TYPE_VIBM_3583:
		case LIBRARY_TYPE_VIBM_3584:
		case LIBRARY_TYPE_VIBM_TS3100:
			vibmtl_init_handlers(mchanger);
			break;
		case LIBRARY_TYPE_VADIC_SCALAR24:
		case LIBRARY_TYPE_VADIC_SCALAR100:
		case LIBRARY_TYPE_VADIC_SCALARi2000:
			vadic_init_handlers(mchanger);
			break;
		case LIBRARY_TYPE_VQUANTUM_M2500:
			vqtl_init_handlers(mchanger);
			break;
		case LIBRARY_TYPE_VHP_ESL9000:
		case LIBRARY_TYPE_VHP_ESLSERIES:
		case LIBRARY_TYPE_VHP_EMLSERIES:
		case LIBRARY_TYPE_VHP_MSLSERIES:
		case LIBRARY_TYPE_VHP_MSL6000:
		case LIBRARY_TYPE_VOVL_NEOSERIES:
			vhptl_init_handlers(mchanger);
			break;
		default:
			break;
	} 
}

static int
mchanger_initialize_pages(struct mchanger *mchanger)
{
	mchanger_init_handlers(mchanger);
	if (mchanger->handlers.init_inquiry_data)
		(*mchanger->handlers.init_inquiry_data)(mchanger);
	else
		mchanger_init_inquiry_data(mchanger);

	/* Address assignment etc would be ours for now*/
	mchanger_init_address_assignment_page(mchanger);
	mchanger_init_transport_geometry_page(mchanger);
	mchanger->exg_possible = 1;
	mchanger_init_device_capabilities(mchanger);
	mchanger->log_info.num_pages = 0x01;
	mchanger->log_info.page_code[0] = 0x00;
	return 0;
}

static int
mchanger_cmd_request_sense(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	struct initiator_state *istate;

	istate = ctio->istate;
	tdevice_reservation_lock(&mchanger->tdevice);
	device_request_sense(ctio, istate, 0);
	tdevice_reservation_unlock(&mchanger->tdevice);
	return 0;
}


int
mchanger_cmd_report_luns(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint32_t allocation_length;
	uint8_t *cdb = ctio->cdb;
	int length, num_luns = 1;

	allocation_length = be32toh(*((uint32_t *)(&cdb[6])));
	if (!allocation_length)
		return 0;

	length = 8 + num_luns * 8;
	ctio_allocate_buffer(ctio, length, Q_WAITOK);
	if (!ctio->data_ptr)
		return -1;

	bzero(ctio->data_ptr, length);
	if (ctio->init_int == TARGET_INT_FC)
		__write_lun(mchanger->tdevice.tl_id, 0, ctio->data_ptr+8);

	ctio->scsi_status = SCSI_STATUS_OK;
	*((uint32_t *)ctio->data_ptr) = htobe32(length - 8);
	ctio->dxfer_len = min_t(int, length, allocation_length);
	return 0;
}

static int
mchanger_evpd_inquiry_data(struct mchanger *mchanger, struct qsio_scsiio *ctio, uint8_t page_code, uint16_t allocation_length)
{
	/* We would be coming here on edvc here */
	return (*mchanger->handlers.evpd_inquiry)(mchanger, ctio, page_code, allocation_length);
}

static int
mchanger_standard_inquiry_data(struct mchanger *mchanger, struct qsio_scsiio *ctio, uint16_t allocation_length)
{
	uint8_t min_len;

	min_len = min_t(uint8_t, allocation_length, sizeof(struct inquiry_data));

	/* As of now we return the STANDARD_INQUIRY_LEN */
	ctio_allocate_buffer(ctio, min_len, Q_WAITOK);
	if (!ctio->data_ptr)
	{
		return -1;
	}

	ctio->scsi_status = SCSI_STATUS_OK;
	memcpy(ctio->data_ptr, &mchanger->inquiry, min_len);
	return 0;
}

int
mchanger_cmd_inquiry(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	int retval;
	uint16_t allocation_length;
	uint8_t evpd, page_code;

	evpd = READ_BIT(cdb[1], 0);

	if (evpd && !mchanger->supports_evpd)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	page_code = cdb[2];
	allocation_length = be16toh(*(uint16_t *)(&cdb[3]));

	if (!evpd && page_code)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	if (!evpd)
		retval = mchanger_standard_inquiry_data(mchanger, ctio, allocation_length);
	else
		retval = mchanger_evpd_inquiry_data(mchanger, ctio, page_code, allocation_length);

	if (ctio->dxfer_len && ctio->init_int == TARGET_INT_ISCSI && ctio->ccb_h.target_lun) {
		ctio->data_ptr[0] = 0x7F; /* Invalid LUN */
		ctio->dxfer_len = 1;
	}
	return retval;
};

int
mchanger_cmd_test_unit_ready(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	if (!atomic_read(&mdaemon_load_done)) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_NOT_READY, 0, LOGICAL_UNIT_IS_IN_PROCESS_OF_BECOMING_READY_ASC, LOGICAL_UNIT_IS_IN_PROCESS_OF_BECOMING_READY_ASCQ);
	}
	return 0;
}

void
mchanger_lock_element(struct mchanger *mchanger, uint32_t address)
{
	struct mchanger_element *element;

	if (!address)
	{
		return;
	}

	element = mchanger_get_element(mchanger, address);
	debug_check(!element);
	update_mchanger_element_flags(element, get_mchanger_element_flags(element) & ~ELEMENT_DESCRIPTOR_ACCESS_MASK);
	return;
}

void
mchanger_unlock_element(struct mchanger *mchanger, uint32_t address)
{
	struct mchanger_element *element;

	if (!address)
	{
		return; /* From web UI */
	}

	element = mchanger_get_element(mchanger, address);
	debug_check(!element);

	update_mchanger_element_flags(element, get_mchanger_element_flags(element) | ELEMENT_DESCRIPTOR_ACCESS_MASK);
}

int
mchanger_cmd_exchange_medium(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint16_t medium_transport_address;
	uint16_t source_address;
	uint16_t first_dest_address;
	uint16_t second_dest_address;
	struct mchanger_element *source, *first_dest, *second_dest;
	struct mchanger_element *medium_transport;
	uint8_t inv1, inv2;
	int retval;
	struct tape *source_vcartridge, *first_vcartridge, *second_vcartridge;

	medium_transport_address = be16toh(*(uint16_t *)(&cdb[2]));
	source_address = be16toh(*(uint16_t *)(&cdb[4]));
	first_dest_address = be16toh(*(uint16_t *)(&cdb[6]));
	second_dest_address = be16toh(*(uint16_t *)(&cdb[8]));
	inv1 = (cdb[10] & 0x1);
	inv2 = ((cdb[10] & 0x10) >> 1);

	if (source_address <= 0 || first_dest_address <= 0 || second_dest_address <=0) {
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_ELEMENT_ADDRESS_ASC, INVALID_ELEMENT_ADDRESS_ASCQ);
		return 0;
	}

	source = mchanger_get_element(mchanger, source_address);
	first_dest = mchanger_get_element(mchanger, first_dest_address);
	if (source_address != second_dest_address)
	{
		second_dest = mchanger_get_element(mchanger, second_dest_address);
	}
	else
	{
		second_dest = source;
	}

	if (source == NULL || first_dest == NULL || second_dest == NULL )
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_ELEMENT_ADDRESS_ASC, INVALID_ELEMENT_ADDRESS_ASCQ);
		return 0;
	}

	source_vcartridge = element_vcartridge(source);
	first_vcartridge = element_vcartridge(first_dest);
	second_vcartridge = element_vcartridge(second_dest);

	if (source_vcartridge == NULL || first_vcartridge == NULL)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, MEDIUM_SOURCE_ELEMENT_EMPTY_ASC, MEDIUM_SOURCE_ELEMENT_EMPTY_ASCQ);
		return 0;
	}

	if (second_vcartridge != NULL && second_dest != source)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, MEDIUM_DESTINATION_ELEMENT_FULL_ASC, MEDIUM_DESTINATION_ELEMENT_FULL_ASCQ);
		return 0;
	}

	medium_transport = mchanger_get_element(mchanger, medium_transport_address);
	if (!medium_transport)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_ELEMENT_ADDRESS_ASC, INVALID_ELEMENT_ADDRESS_ASCQ);
		return 0;
	}

	if ((inv1 || inv2) && !(get_mchanger_element_invert(medium_transport)))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	if (source_vcartridge->locked || first_vcartridge->locked) /* Locked for import/export */
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, MEDIUM_MAGAZINE_NOT_ACCESSIBLE_ASC, MEDIUM_MAGAZINE_NOT_ACCESSIBLE_ASCQ);
		return 0;
	}


	if (!(source->edesc.common.flags & ELEMENT_DESCRIPTOR_ACCESS_MASK) ||
	    !(first_dest->edesc.common.flags & ELEMENT_DESCRIPTOR_ACCESS_MASK))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, MEDIUM_MAGAZINE_NOT_ACCESSIBLE_ASC, MEDIUM_MAGAZINE_NOT_ACCESSIBLE_ASCQ);
		return 0;
	}

	if (first_dest->type == DATA_TRANSFER_ELEMENT)
	{ 
		retval = tdrive_media_valid((struct tdrive *)first_dest->element_data, source_vcartridge->make);
		if (unlikely(retval != 0))
		{
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INCOMPATIBLE_MEDIUM_INSTALLED_ASC, INCOMPATIBLE_MEDIUM_INSTALLED_ASCQ);
			return 0;
		}
	}

	if (second_dest->type == DATA_TRANSFER_ELEMENT)
	{ 
		retval = tdrive_media_valid((struct tdrive *)second_dest->element_data, first_vcartridge->make);
		if (unlikely(retval != 0))
		{
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INCOMPATIBLE_MEDIUM_INSTALLED_ASC, INCOMPATIBLE_MEDIUM_INSTALLED_ASCQ);
			return 0;
		}
	}

	if (source->type == DATA_TRANSFER_ELEMENT)
	{
		retval = tdrive_unload_tape((struct tdrive *)source->element_data, ctio);
		if (unlikely(retval != 0))
		{
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_HARDWARE_ERROR, 0, UNLOAD_TAPE_FAILURE_ASC, UNLOAD_TAPE_FAILURE_ASCQ);
			return 0;
		}
	}

	if (first_dest->type == DATA_TRANSFER_ELEMENT)
	{
		retval = tdrive_unload_tape((struct tdrive *)first_dest->element_data, ctio);
		if (unlikely(retval != 0))
		{
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_HARDWARE_ERROR, 0, UNLOAD_TAPE_FAILURE_ASC, UNLOAD_TAPE_FAILURE_ASCQ);
			return 0;

		}
	}

	if (first_dest->type == DATA_TRANSFER_ELEMENT)
	{
		tdrive_load_tape((struct tdrive *)first_dest->element_data, source_vcartridge);
	} 
	else
	{
		first_dest->element_data = source_vcartridge;
	} 

	if (source->type != DATA_TRANSFER_ELEMENT)
	{
		source->element_data = NULL;
	}

	if (second_dest->type == DATA_TRANSFER_ELEMENT)
	{
		tdrive_load_tape((struct tdrive *)second_dest->element_data, first_vcartridge);
	} 
	else
	{
		second_dest->element_data = first_vcartridge;
	}

	mchanger_exchange_update(second_dest, first_dest, source);

	return 0;
}

void
mchanger_move_update(struct mchanger_element *destination, struct mchanger_element *source)
{
	update_element_descriptor_source_storage_address(destination, source);
	update_mchanger_element_flags(destination, get_mchanger_element_flags(destination) | ELEMENT_DESCRIPTOR_FULL_MASK);
	update_mchanger_element_flags(source, get_mchanger_element_flags(source) & ~ELEMENT_DESCRIPTOR_FULL_MASK);
	update_mchanger_element_pvoltag(source);
	update_mchanger_element_pvoltag(destination);
	return;
}

void
mchanger_exchange_update(struct mchanger_element *second_destination, struct mchanger_element *first_destination, struct mchanger_element *source)
{
	struct mchanger_element tmp_element;

	memcpy(&tmp_element, first_destination, sizeof(struct mchanger_element));
	mchanger_move_update(first_destination, source);
	mchanger_move_update(second_destination, &tmp_element);
	if (source->type == IMPORT_EXPORT_ELEMENT && source != second_destination)
	{
		update_mchanger_element_flags(source, get_mchanger_element_flags(source) & ~IE_MASK_IMPEXP);
	}

}

int
mchanger_cmd_move_medium(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint16_t medium_transport_address;
	uint16_t source_address;
	uint16_t destination_address;
	uint8_t  invert;
	struct mchanger_element *source, *destination;
	struct mchanger_element *medium_transport;
	struct tape *source_vcartridge, *destination_vcartridge;
	int retval;

	medium_transport_address = be16toh(*(uint16_t *)(&cdb[2]));
	source_address = be16toh(*(uint16_t *)(&cdb[4]));
	destination_address = be16toh(*(uint16_t *)(&cdb[6]));

	if (source_address <= 0 || destination_address <= 0)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_ELEMENT_ADDRESS_ASC, INVALID_ELEMENT_ADDRESS_ASCQ);
		return 0;
	}

	source = mchanger_get_element(mchanger, source_address);
	if (source_address != destination_address)
	{
		destination = mchanger_get_element(mchanger, destination_address);
	}
	else
	{
		destination = source;
	}

	if (source == NULL || destination == NULL)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_ELEMENT_ADDRESS_ASC, INVALID_ELEMENT_ADDRESS_ASCQ);
		return 0;
	}

	if (source == destination)
	{
		return 0;
	}

	source_vcartridge = element_vcartridge(source);
	destination_vcartridge = element_vcartridge(destination);

	if (source_vcartridge == NULL)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, MEDIUM_SOURCE_ELEMENT_EMPTY_ASC, MEDIUM_SOURCE_ELEMENT_EMPTY_ASCQ);
		return 0;
	}

	if (source_vcartridge->locked) /* Locked for import/export */
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, MEDIUM_MAGAZINE_NOT_ACCESSIBLE_ASC, MEDIUM_MAGAZINE_NOT_ACCESSIBLE_ASCQ);
		return 0;
	}

	if (destination_vcartridge != NULL && destination != source)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0,  MEDIUM_DESTINATION_ELEMENT_FULL_ASC,  MEDIUM_DESTINATION_ELEMENT_FULL_ASCQ);
		return 0;
	}

	if (medium_transport_address)
	{
		medium_transport = mchanger_get_element(mchanger, medium_transport_address);
	}
	else
	{
		medium_transport = get_first_element(mchanger, MEDIUM_TRANSPORT_ELEMENT);
	}

	if (!medium_transport)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_ELEMENT_ADDRESS_ASC, INVALID_ELEMENT_ADDRESS_ASCQ);
		return 0;
	}

	if (!(source->edesc.common.flags & ELEMENT_DESCRIPTOR_ACCESS_MASK))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, MEDIUM_MAGAZINE_NOT_ACCESSIBLE_ASC, MEDIUM_MAGAZINE_NOT_ACCESSIBLE_ASCQ);
		return 0;
	}

	/* ??? Do we need to check INV field */
	invert = READ_BIT(cdb[10], 0);
	if (invert && !(get_mchanger_element_invert(medium_transport)))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	if (source->address == destination->address)
	{
		debug_check(source != destination);
		return 0;
	}

	if (destination->type == DATA_TRANSFER_ELEMENT)
	{ 
		retval = tdrive_media_valid((struct tdrive *)destination->element_data, source_vcartridge->make);
		if (unlikely(retval != 0))
		{
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INCOMPATIBLE_MEDIUM_INSTALLED_ASC, INCOMPATIBLE_MEDIUM_INSTALLED_ASCQ);
			return 0;
		}
	}

	if (source->type == DATA_TRANSFER_ELEMENT)
	{
		retval = tdrive_unload_tape((struct tdrive *)source->element_data, ctio);
		if (unlikely(retval != 0))
		{
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_HARDWARE_ERROR, 0,  UNLOAD_TAPE_FAILURE_ASC,  UNLOAD_TAPE_FAILURE_ASCQ);
			return 0;
		}
	}

	/* Ok now if the type of destination is a tape drive then we need to
	 * load the tape
	 */
	if (destination->type == DATA_TRANSFER_ELEMENT)
	{
		tdrive_load_tape((struct tdrive *)destination->element_data, source_vcartridge);
	}
	else
	{
		destination->element_data = source_vcartridge;
	}

	if (source->type != DATA_TRANSFER_ELEMENT)
	{
		source->element_data = NULL;
	}

	mchanger_move_update(destination, source);
	source->edesc.common.invert &= ~ELEMENT_DESCRIPTOR_SVALID_MASK;
	source->edesc.common.source_storage_element_address = 0;
	if (source->type == IMPORT_EXPORT_ELEMENT)
	{
		update_mchanger_element_flags(source, get_mchanger_element_flags(source) & ~IE_MASK_IMPEXP);
	}

	ctio->scsi_status = SCSI_STATUS_OK;
	ctio->dxfer_len = 0;
	ctio->data_ptr = NULL;
	return 0;
}

int 
mchanger_copy_vital_product_page_info(struct mchanger *mchanger, uint8_t *buffer, uint16_t allocation_length)
{
	struct vital_product_page *page = (struct vital_product_page *)buffer;
	uint8_t num_pages = 0;

	if (allocation_length < (sizeof(struct vital_product_page)+mchanger->evpd_info.num_pages))
	{
		return -1;
	}

	bzero(page, sizeof(*page));
	page->device_type = 0x01;
	page->page_code = 0x00;
	page->page_length = mchanger->evpd_info.num_pages;

	for (num_pages = 0; num_pages < mchanger->evpd_info.num_pages; num_pages++) {
		page->page_type[num_pages] = mchanger->evpd_info.page_code[num_pages];
	}

	return (mchanger->evpd_info.num_pages + sizeof(struct vital_product_page));
}	

static int
error_element_descriptor(uint8_t *buffer, uint8_t voltag, uint16_t address, int idlen, int avail)
{
	struct element_descriptor_common *common = (struct element_descriptor_common *)(buffer);
	int min_len;
	int done = 0;

	min_len = min_t(int, sizeof(struct element_descriptor_common), avail);
	if (min_len > 0) {
		bzero(common, min_len);
		common->element_address = htobe16(address);
		common->flags = ELEMENT_DESCRIPTOR_EXCEPT_MASK;
		done += min_len;
		buffer += min_len;
		avail -= min_len;
	}

	/* Need to fill in approp asc, ascq */
	if (voltag)
	{
		min_len = min_t(int, avail, sizeof(struct voltag));
		if (min_len > 0) {
			bzero(buffer, min_len);
			done += min_len;
			buffer += min_len;
			avail -= min_len;
		}
	}

	min_len = min_t(int, avail, idlen);
	if (min_len > 0) {
		bzero(buffer, min_len);
		done += min_len;
	}
	return done;
}

static int
element_descriptor_copy_status(struct mchanger_element *element, uint8_t *buffer, uint8_t voltag, int avail)
{
	struct element_descriptor_common temp;
	int done = 0;
	int min_len;

	memcpy(&temp, &element->edesc.common, sizeof(struct element_descriptor_common));
	min_len = min_t(int, sizeof(struct element_descriptor_common), avail);
	if (min_len > 0)
	{
		memcpy(buffer, &temp, min_len);
		done += min_len;
		buffer += min_len;
		avail -= min_len;
	}

	if (voltag) {
		min_len = min_t(int, avail, sizeof(struct voltag));
		if (min_len > 0) {
			memcpy(buffer, &element->edesc.voltag, min_len);
			done += min_len;
		}
	}

	return done;
}

static inline int
element_idlength(struct mchanger *mchanger, uint8_t element_type)
{
	switch (element_type)
	{
		case MEDIUM_TRANSPORT_ELEMENT:
		case STORAGE_ELEMENT:
		case IMPORT_EXPORT_ELEMENT:
			return 0;
		case DATA_TRANSFER_ELEMENT:
			return mchanger->devid_len;
		default:
			debug_check(1);
			break;
	}
	return 0;
}

static void
copy_avoltag(struct mchanger_element *element, uint8_t *buffer, int min_len)
{
	struct voltag *voltag = (struct voltag *)(buffer);

	bzero(voltag, min_len);
	if (min_len > 4)
		memcpy(voltag->pvoltag+4, element->serialnumber, min_len);
}

static void
copy_identifier(struct mchanger *mchanger, uint8_t *buffer, uint8_t dvcid, struct mchanger_element *element, int min_len)
{
	struct device_identifier *identifier;

	identifier = (struct device_identifier *)(buffer);
	bzero(identifier, min_len);
	if (!dvcid || element->type != DATA_TRANSFER_ELEMENT)
		return;

	memcpy(buffer, &element->edesc.identifier, min_len);
	if (mchanger->make == LIBRARY_TYPE_VHP_EMLSERIES && min_len >= sizeof(struct identifier))
	{
		identifier->identifier_length += VHP_EMLESERIES_ID_INCR;
	}
}

static int
get_start_address(struct mchanger *mchanger, int element_type, int start_address)
{
	switch(element_type) {
	case MEDIUM_TRANSPORT_ELEMENT:
		if (start_address < DEFAULT_MT_START_ADDRESS)
			return DEFAULT_MT_START_ADDRESS;
		break;
	case STORAGE_ELEMENT:
		if (start_address < DEFAULT_ST_START_ADDRESS)
			return DEFAULT_ST_START_ADDRESS;
		break;
	case DATA_TRANSFER_ELEMENT:
		if (start_address < DEFAULT_DT_START_ADDRESS)
			return DEFAULT_DT_START_ADDRESS;
		break;
	case IMPORT_EXPORT_ELEMENT:
		if (start_address < DEFAULT_IE_START_ADDRESS)
			return DEFAULT_IE_START_ADDRESS;
		break;
	}
	return start_address;
}

static void 
read_element_status(struct mchanger *mchanger, unsigned char *buffer, int *buffer_offset, uint16_t start_element_address, uint16_t num_elements, uint16_t *num_elements_read, int alloc_len, uint8_t voltag, uint8_t dvcid, int *bytes_read, int *num_elements_avail, int element_type, int *first_element_address)
{
	struct mchanger_element *element;
	int offset = *buffer_offset;
	struct element_status_page *epage;
	uint16_t descriptor_len;
	uint32_t byte_count = 0;
	struct mchanger_element_list *element_list;
	uint16_t idlength = 0;
	uint16_t avoltag_len = 0;
	uint16_t current_address;
	uint8_t error_desc;
	int min_len;

	if (*num_elements_read >= num_elements)
	{
		return;
	}

	descriptor_len = sizeof(struct element_descriptor_common);
	if (!dvcid)
	{
		idlength = sizeof(struct device_identifier);
	}
	else
	{
		idlength = sizeof(struct device_identifier) + element_idlength(mchanger, element_type);
		if (mchanger->make == LIBRARY_TYPE_VHP_EMLSERIES)
		{
			idlength += VHP_EMLESERIES_ID_INCR;
		}
	}

	if (voltag) {
		descriptor_len += sizeof(struct voltag);
	}

	if (voltag && element_type == DATA_TRANSFER_ELEMENT && mchanger->serial_as_avoltag)
	{
		avoltag_len += sizeof(struct voltag);
	}

	offset += sizeof(struct element_status_page);
	element_list = mchanger_elem_list_type(mchanger, element_type);
	current_address = get_start_address(mchanger, element_type, start_element_address);
	STAILQ_FOREACH(element, element_list, me_list) {
		/* Not the specified element type */
		debug_check(element->type != element_type);

		if (!element->address && element->type != MEDIUM_TRANSPORT_ELEMENT)
			continue;

		if (element->address < start_element_address)
			continue;

again:
		error_desc = 0;

		if (element->address == current_address)
		{
			offset += element_descriptor_copy_status(element, buffer+offset, voltag, alloc_len - offset);

			if (avoltag_len)
			{
				min_len = min_t(int, alloc_len - offset, sizeof(struct voltag)); 
				if (min_len > 0)
				{
					copy_avoltag(element, buffer+offset, min_len);
					offset += min_len;
				}
			}
			min_len = min_t(int, alloc_len - offset, idlength); 
			if (min_len > 0)
			{
				copy_identifier(mchanger, buffer+offset, dvcid, element, min_len);
				offset += min_len;
			}
		}
		else
		{
			offset += error_element_descriptor(buffer+offset, voltag, current_address, idlength, alloc_len - offset);
			error_desc = 1;
		}
		*num_elements_read = *num_elements_read + 1;
		if ((*first_element_address == -1) || (element->address < *first_element_address))
		{
			*first_element_address = element->address;
		}
		byte_count += descriptor_len+idlength+avoltag_len;
		*num_elements_avail =  *num_elements_avail + 1; 
		current_address++;

		/* We have satisfied the request */
		if (*num_elements_read >= num_elements)
		{
			break;
		}

		if (error_desc)
		{
			goto again;
		}
	}

	if (byte_count && (*buffer_offset+sizeof(struct element_status_page) <= alloc_len))
	{
		epage = (struct element_status_page *)(buffer+(*buffer_offset));
		bzero(epage, sizeof(*epage));
		epage->element_type_code = element_type;

		if (voltag)
		{
			epage->voltag = 0x80; 
		}

		if (avoltag_len)
		{
			epage->voltag |= 0x40; 
		}

		epage->element_descriptor_length = htobe16(descriptor_len + idlength + avoltag_len);
		epage->byte_count_of_descriptor_data = htobe32(byte_count);
		*buffer_offset = offset;
		*bytes_read += (byte_count + sizeof(struct element_status_page));
	}
	else if (byte_count)
	{
		*bytes_read += (byte_count + sizeof(struct element_status_page));
	}
	return;
}

int
mchanger_cmd_read_element_status(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint16_t start_element_address;
	uint16_t num_elements;
	uint32_t allocation_length;
	uint8_t  dvcid;
	uint8_t element_type_code;
	uint8_t voltag;
	uint16_t num_elements_read;
	int num_elements_avail;
	int bytes_read;
	uint8_t *res_buffer;
	int first_element_address = -1;
	struct element_status_data *sdata;
	int buffer_offset;

	start_element_address = be16toh(*(uint16_t *)(&cdb[2]));
	num_elements = be16toh(*(uint16_t *)(&cdb[4]));
	allocation_length = READ_24(cdb[7], cdb[8], cdb[9]);
	dvcid = READ_BIT(cdb[6], 0);
	voltag = READ_BIT(cdb[1], 4);
	element_type_code = READ_NIBBLE_LOW(cdb[1]);

	debug_info("start element address %d num elements %d allocation length %d dvcid %d curdata %d voltag %d element_type_code %d\n", start_element_address, num_elements, allocation_length, dvcid, curdata, voltag, element_type_code);

	num_elements_read = 0;

	if (allocation_length < sizeof(struct element_status_data))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (!ctio->data_ptr)
	{
		return -1;
	}

	bzero(ctio->data_ptr, allocation_length);

	res_buffer = ctio->data_ptr;
	buffer_offset = sizeof(struct element_status_data);

	/* A reservation conflict shall occur if the curdata is zero and this
	command is received by an initiator other than the one holding a 
	logical unit or element reservation */
	num_elements_avail = 0;
	bytes_read = 0;

	if (element_type_code == ALL_ELEMENTS || element_type_code == MEDIUM_TRANSPORT_ELEMENT)
	{
		read_element_status(mchanger, res_buffer, &buffer_offset, start_element_address, num_elements, &num_elements_read, allocation_length, voltag, dvcid, &bytes_read, &num_elements_avail, MEDIUM_TRANSPORT_ELEMENT, &first_element_address);
	}

	if (element_type_code == ALL_ELEMENTS || element_type_code == DATA_TRANSFER_ELEMENT)
	{
		read_element_status(mchanger, res_buffer, &buffer_offset, start_element_address, num_elements, &num_elements_read, allocation_length, voltag, dvcid, &bytes_read, &num_elements_avail, DATA_TRANSFER_ELEMENT, &first_element_address);
	}

	if (element_type_code == ALL_ELEMENTS || element_type_code == IMPORT_EXPORT_ELEMENT)
	{
		read_element_status(mchanger, res_buffer, &buffer_offset, start_element_address, num_elements, &num_elements_read, allocation_length, voltag, dvcid, &bytes_read, &num_elements_avail, IMPORT_EXPORT_ELEMENT, &first_element_address);
	}

	if (element_type_code == ALL_ELEMENTS || element_type_code == STORAGE_ELEMENT)
	{
		read_element_status(mchanger, res_buffer, &buffer_offset, start_element_address, num_elements, &num_elements_read, allocation_length, voltag, dvcid, &bytes_read, &num_elements_avail, STORAGE_ELEMENT, &first_element_address);
	}

	/* Now reset the res_buffer, to fill in the element_status_data */
	sdata = (struct element_status_data *)ctio->data_ptr;
	bzero(sdata, sizeof(*sdata));
	sdata->first_element_address_reported = htobe16(first_element_address & 0xFFFF);
	sdata->number_of_elements = htobe16(num_elements_avail);
	sdata->byte_count_of_report = htobe32(bytes_read);
	if (ctio->dxfer_len > (bytes_read + 8))
	{
		ctio->dxfer_len = bytes_read+8;
	}
	return 0;
}

static void
copy_current_element_address_assignment_page(struct mchanger *mchanger, uint8_t *buffer, int min_len)
{
	memcpy(buffer, &mchanger->assignment_page, min_len);
}

static void 
copy_current_transport_geometry_descriptor_page(struct mchanger *mchanger, uint8_t *buffer, int min_len)
{
	memcpy(buffer, &mchanger->geometry_page, min_len);
}

static void
copy_current_device_capabilities_page(struct mchanger *mchanger, uint8_t *buffer, int min_len)
{
	memcpy(buffer, &mchanger->devcap_page, min_len);
}


static int
mode_sense_current_values(struct mchanger *mchanger, uint8_t *buffer, uint16_t allocation_length, uint8_t dbd, uint8_t page_code, int *start_offset)
{
	int offset = *start_offset;
	int avail = 0;
	int min_len;

	/* Current values would be the current page data */
	if (page_code == ALL_PAGES || page_code == ELEMENT_ADDRESS_ASSIGNMENT_PAGE)
	{
		min_len = min_t(int, sizeof(struct element_address_assignment_page), allocation_length - offset);
		if (min_len > 0)
		{
			copy_current_element_address_assignment_page(mchanger, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct element_address_assignment_page);
	}
	if (page_code == ALL_PAGES || page_code == TRANSPORT_GEOMETRY_DESCRIPTOR_PAGE)
	{
		min_len = min_t(int, sizeof(struct transport_geometry_descriptor_page), allocation_length - offset);
		if (min_len > 0)
		{
			copy_current_transport_geometry_descriptor_page(mchanger,buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct transport_geometry_descriptor_page);
	}

	if (page_code == ALL_PAGES || page_code == DEVICE_CAPABILITIES_PAGE)
	{
		min_len = min_t(int, sizeof(struct device_capabilities_page), allocation_length - offset);
		if (min_len > 0)
		{
			copy_current_device_capabilities_page(mchanger, buffer+offset, min_len);
			offset += min_len;
		}
		avail += sizeof(struct device_capabilities_page);
	}

	*start_offset = offset;
	return avail;
}

static int
mode_sense_changeable_values(struct mchanger *mchanger, uint8_t *buffer, uint16_t allocation_length, uint8_t dbd, uint8_t page_code, int *start_offset)
{
	return mode_sense_current_values(mchanger, buffer, allocation_length, dbd, page_code, start_offset);
}

static int
mode_sense_default_values(struct mchanger *mchanger, uint8_t *buffer, uint16_t allocation_length, uint8_t dbd, uint8_t page_code, int *start_offset)
{
	/* Return the current values itself */
	return mode_sense_current_values(mchanger, buffer, allocation_length, dbd, page_code, start_offset);
}

static int
mode_sense_saved_values(struct mchanger *mchanger, uint8_t *buffer, uint16_t allocation_length, uint8_t dbd, uint8_t page_code, int *start_offset)
{
	/* Return the current values itself */
	return mode_sense_current_values(mchanger, buffer, allocation_length, dbd, page_code, start_offset);
}

static int
mchanger_copy_supported_log_page_info(struct mchanger *mchanger, uint8_t *buffer, uint16_t allocation_length)
{
	struct scsi_log_page *page = (struct scsi_log_page *)buffer;
	uint8_t num_pages = 0;

	if (allocation_length < (sizeof(struct scsi_log_page)+mchanger->log_info.num_pages))
		return -1;

	bzero(page, sizeof(*page));
	page->page_code = 0x00;
	page->page_length = mchanger->log_info.num_pages;

	for (num_pages = 0; num_pages < mchanger->log_info.num_pages; num_pages++)
	{
		page->page_data[num_pages] = mchanger->log_info.page_code[num_pages];
	}

	return (mchanger->log_info.num_pages + sizeof(struct scsi_log_page));
}

static int
mchanger_cmd_log_sense6(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t sp;
	uint8_t page_code;
	uint16_t allocation_length;
	uint16_t page_length;

	sp = READ_BIT(cdb[1], 0);
	page_code = (cdb[2] & 0x3F); /* ??? check the mask */

	allocation_length = be16toh(*(uint16_t *)(&cdb[7]));

	if (sp)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	if (allocation_length < sizeof(struct scsi_log_page))
	{
		/* Minimum size needed by us */
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (!ctio->data_ptr) {
		/* Memory allocation failure */
		debug_warn("Unable to allocate for allocation length %d\n", allocation_length);
		return -1;
	}

	bzero(ctio->data_ptr, allocation_length);

	page_length = 0;
	switch (page_code) {
	case 0x00:
		page_length = mchanger_copy_supported_log_page_info(mchanger, ctio->data_ptr, allocation_length);
		break;
	default:
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);
	}
	ctio->dxfer_len = page_length;
	return 0;
}

int
mchanger_cmd_mode_sense6(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t dbd;
	uint8_t pc, page_code;
	uint8_t allocation_length;
	int avail;
	int offset;
	struct mode_parameter_header6 *header;

	dbd = READ_BIT(cdb[1], 3);
	pc = cdb[2] >> 6;
	page_code = (cdb[2] & 0x3F); /* ??? check the mask */

	allocation_length = cdb[4];
	debug_info("dbd %d page_code %x allocation_length %d\n", dbd, page_code, allocation_length);
	if (allocation_length < (sizeof(struct mode_parameter_header6) - offsetof(struct mode_parameter_header6, medium_type)))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (!ctio->data_ptr)
	{
		debug_warn("Cannot allocate for allocation_length %d\n", allocation_length);
		return -1;
	}

	bzero(ctio->data_ptr, allocation_length);

	offset = min_t(int, allocation_length, sizeof(struct mode_parameter_header6));
 	avail = sizeof(struct mode_parameter_header6);
	switch (pc)
	{
		case MODE_SENSE_CURRENT_VALUES:
			avail += mode_sense_current_values(mchanger, ctio->data_ptr, allocation_length, dbd, page_code, &offset);
			break;
		case MODE_SENSE_CHANGEABLE_VALUES:
			avail += mode_sense_changeable_values(mchanger, ctio->data_ptr, allocation_length, dbd, page_code, &offset);
			break;
		case MODE_SENSE_DEFAULT_VALUES:
			avail += mode_sense_default_values(mchanger, ctio->data_ptr, allocation_length, dbd, page_code, &offset);
			break;
		case MODE_SENSE_SAVED_VALUES:
			avail += mode_sense_saved_values(mchanger, ctio->data_ptr, allocation_length, dbd, page_code, &offset);
			break;
	}

	/* Now fillin the mode parameter heade values */
	header = (struct mode_parameter_header6 *)ctio->data_ptr;
	header->mode_data_length = avail - offsetof(struct mode_parameter_header6, medium_type);
	ctio->dxfer_len = offset;
	return 0;
}

static int
mchanger_cmd_mode_sense10(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t dbd;
	uint8_t pc, page_code;
	uint16_t allocation_length;
	int offset;
	int avail;
	struct mode_parameter_header10 *header;

	dbd = READ_BIT(cdb[1], 3);
	pc = cdb[2] >> 6;
	page_code = (cdb[2] & 0x3F); /* ??? check the mask */

	allocation_length = be16toh(*(uint16_t *)(&cdb[7]));
	debug_info("dbd %d page_code %x allocation_length %d\n", dbd, page_code, allocation_length);
	if (allocation_length < (sizeof(struct mode_parameter_header10) - offsetof(struct mode_parameter_header10, medium_type)))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	ctio_allocate_buffer(ctio, allocation_length, Q_WAITOK);
	if (!ctio->data_ptr)
	{
		debug_warn("Cannot allocate for allocation_length %d\n", allocation_length);
		return -1;
	}

	bzero(ctio->data_ptr, allocation_length);
	offset = min_t(int, allocation_length, sizeof(struct mode_parameter_header10));
 	avail = sizeof(struct mode_parameter_header10);
	switch (pc) {
	case MODE_SENSE_CURRENT_VALUES:
		avail = mode_sense_current_values(mchanger, ctio->data_ptr, allocation_length, dbd, page_code, &offset);
		break;
	case MODE_SENSE_CHANGEABLE_VALUES:
		avail = mode_sense_changeable_values(mchanger, ctio->data_ptr, allocation_length, dbd, page_code, &offset);
		break;
	case MODE_SENSE_DEFAULT_VALUES:
		avail = mode_sense_default_values(mchanger, ctio->data_ptr, allocation_length, dbd, page_code, &offset);
		break;
	case MODE_SENSE_SAVED_VALUES:
		avail = mode_sense_saved_values(mchanger, ctio->data_ptr, allocation_length, dbd, page_code, &offset);
		break;
	}

	header = (struct mode_parameter_header10 *)ctio->data_ptr;
	header->mode_data_length = htobe16(avail - offsetof(struct mode_parameter_header10, medium_type));
	ctio->dxfer_len = offset;
	return 0;
}

static int
mchanger_cmd_prevent_allow_medium_removal(struct mchanger *mchanger, struct qsio_scsiio *ctio)
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
mchanger_cmd_reserve(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t element;
	uint8_t *cdb = ctio->cdb;
	struct tdevice *tdevice;
	struct reservation *reservation;

	element = cdb[1] & 0x01;

	if (element)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	tdevice = &mchanger->tdevice;
	reservation = &tdevice->reservation;

	tdevice_reservation_lock(tdevice);
	if (mchanger_cmd_access_ok(mchanger, ctio) != 0) {
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
mchanger_cmd_release(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t element;
	uint8_t *cdb = ctio->cdb;
	struct tdevice *tdevice;
	struct reservation *reservation;

	element = cdb[1] & 0x01;

	if (element)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	tdevice = &mchanger->tdevice;
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
mchanger_cmd_persistent_reserve_in(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	uint8_t service_action;
	uint16_t allocation_length;
	int retval;

	service_action = (cdb[1] & 0x1F);
	allocation_length = be16toh(*(uint16_t *)(&cdb[7]));

	if (allocation_length < 8)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		return 0;
	}

	if (service_action == SERVICE_ACTION_READ_KEYS)
	{
		retval = persistent_reservation_read_keys(ctio, allocation_length, &mchanger->tdevice.reservation);
	}
	else if (service_action == SERVICE_ACTION_READ_RESERVATIONS)
	{
		retval = persistent_reservation_read_reservations(ctio, allocation_length, &mchanger->tdevice.reservation);
	}
	else if (service_action == SERVICE_ACTION_READ_CAPABILITIES)
	{
		retval = persistent_reservation_read_capabilities(ctio, allocation_length);
	}
	else if (service_action == SERVICE_ACTION_READ_FULL)
	{
		retval = persistent_reservation_read_full(ctio, allocation_length, &mchanger->tdevice.reservation);
	} 
	else
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
		retval = 0;
	}
	return retval;
}

static int
mchanger_cmd_persistent_reserve_out(struct mchanger *mchanger, struct qsio_scsiio *ctio)
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

	if (parameter_list_length != 24)
	{
		ctio_free_data(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, PARAMETER_LIST_LENGTH_ERROR_ASC, PARAMETER_LIST_LENGTH_ERROR_ASCQ);
		return 0;
	}

	switch(service_action)
	{
		case SERVICE_ACTION_REGISTER:
			retval = persistent_reservation_handle_register(&mchanger->tdevice, ctio);
			break;
		case SERVICE_ACTION_REGISTER_IGNORE:
			retval = persistent_reservation_handle_register_and_ignore(&mchanger->tdevice, ctio);
			break;
		case SERVICE_ACTION_RESERVE:
			retval = persistent_reservation_handle_reserve(&mchanger->tdevice, ctio);
			break;
		case SERVICE_ACTION_RELEASE:
			retval = persistent_reservation_handle_release(&mchanger->tdevice, ctio);
			break;
		case SERVICE_ACTION_CLEAR:
			retval = persistent_reservation_handle_clear(&mchanger->tdevice, ctio);
			break;
		case SERVICE_ACTION_PREEMPT:
			retval = persistent_reservation_handle_preempt(&mchanger->tdevice, ctio, 0);
			break;
		case SERVICE_ACTION_PREEMPT_ABORT:
			retval = persistent_reservation_handle_preempt(&mchanger->tdevice, ctio, 1);
			break;
		default:
		{
			ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_CDB_ASC, INVALID_FIELD_IN_CDB_ASCQ);  
			retval = 0;
		}
	}

	ctio_free_data(ctio);
	return retval;
}

static uint8_t
get_mchanger_element_invert(struct mchanger_element *element)
{
	struct element_descriptor *edesc = &element->edesc;

	return (edesc->common.invert & ELEMENT_DESCRIPTOR_INVERT_MASK);

}

int mchanger_cmd_access_ok(struct mchanger *mchanger, struct qsio_scsiio *ctio)
{
	uint8_t *cdb = ctio->cdb;
	struct reservation *reservation = &mchanger->tdevice.reservation;
	uint8_t write_excl = 0;
	uint8_t excl_access = 0;
	uint8_t write_excl_ro = 0;
	uint8_t excl_access_ro = 0;
	uint8_t registered = 0;
	struct registration *tmp;

	if (device_reserved(ctio, &mchanger->tdevice.reservation) == 0)
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
			case MODE_SENSE_6:
			case MODE_SENSE_10:
			case PERSISTENT_RESERVE_IN:
			case PERSISTENT_RESERVE_OUT:
			case TEST_UNIT_READY:
			case RESERVE:
			case EXCHANGE_MEDIUM:
			case POSITION_TO_ELEMENT:
			case INITIALIZE_ELEMENT_STATUS:
			case INITIALIZE_ELEMENT_STATUS_WITH_RANGE:
			case INITIALIZE_ELEMENT_STATUS_WITH_RANGEP:
				return -1;
			case READ_ELEMENT_STATUS:
			{
				uint8_t curdata = (cdb[6] >> 1) & 0x1;
				if (curdata)
					return 0;
				return -1;
			}
			case PREVENT_ALLOW:
			{
				uint8_t prevent = (cdb[4] & 0x3);
				if (prevent)
					return -1;
				return 0;
			} 
			case INQUIRY:
			case LOG_SENSE:
			case REPORT_LUNS:
			case RELEASE:
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
		case TEST_UNIT_READY:
			if (write_excl || excl_access)
				return -1;
			if ((write_excl_ro || excl_access_ro) && !registered)
				return -1;
			return 0;

		case READ_ELEMENT_STATUS:
		{
			uint8_t curdata = (cdb[6] >> 1) & 0x1;

			if (curdata)
				return 0;
			if (write_excl || excl_access)
				return -1;
			if ((write_excl_ro || excl_access_ro) && !registered)
				return -1;
			return 0;
		}
		case PREVENT_ALLOW:
		{
			uint8_t prevent = (cdb[4] & 0x3);
			if (!prevent)
				return 0;
			if (write_excl || excl_access)
				return -1;
			if ((write_excl_ro || excl_access_ro) && !registered)
				return -1;
			return 0;
		}
		case INQUIRY:
		case PERSISTENT_RESERVE_IN:
		case PERSISTENT_RESERVE_OUT:
		case REPORT_LUNS:
			return 0;
		case RELEASE:
		case RESERVE:
			return -1;
		default:
			return -1;
	}
	return -1;
}

int
mchanger_check_cmd(void *changer, uint8_t op)
{
	switch (op)
	{
		case TEST_UNIT_READY:
		case INQUIRY:
		case READ_ELEMENT_STATUS:
		case POSITION_TO_ELEMENT:
		case INITIALIZE_ELEMENT_STATUS:
		case INITIALIZE_ELEMENT_STATUS_WITH_RANGE:
		case INITIALIZE_ELEMENT_STATUS_WITH_RANGEP:
		case MODE_SENSE_6:
		case MODE_SENSE_10:
		case LOG_SENSE:
		case MOVE_MEDIUM:
		case EXCHANGE_MEDIUM:
		case REPORT_LUNS:
		case REQUEST_SENSE:
		case PREVENT_ALLOW:
		case RESERVE:
		case RELEASE:
		case PERSISTENT_RESERVE_IN:
		case PERSISTENT_RESERVE_OUT:
			return 0;
	}
	debug_info("Invalid cmd %x\n", op);
	return -1;
}

void
mchanger_proc_cmd(void *changer, void *iop)
{
	struct mchanger *mchanger = changer;
	struct qsio_scsiio *ctio = iop;
	uint8_t *cdb = ctio->cdb;
	struct initiator_state *istate;
	struct sense_info *sinfo;
	int retval;

	mchanger_lock(mchanger);
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

	if (mchanger_cmd_access_ok(mchanger, ctio) != 0) {
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		ctio_free_data(ctio);
		goto out;
	}

	switch(cdb[0]) {
	case TEST_UNIT_READY:
		retval = mchanger_cmd_test_unit_ready(mchanger, ctio);	
		break;
	case INQUIRY:
		retval = mchanger_cmd_inquiry(mchanger, ctio);
		break;
	case READ_ELEMENT_STATUS:
		retval = mchanger_cmd_read_element_status(mchanger, ctio);
		break;
	case POSITION_TO_ELEMENT:
	case INITIALIZE_ELEMENT_STATUS:
	case INITIALIZE_ELEMENT_STATUS_WITH_RANGE:
	case INITIALIZE_ELEMENT_STATUS_WITH_RANGEP:
		retval = 0;
		break;
	case MODE_SENSE_6:
		retval = mchanger_cmd_mode_sense6(mchanger, ctio);
		break;
	case MODE_SENSE_10:
		retval = mchanger_cmd_mode_sense10(mchanger, ctio);
		break;
	case LOG_SENSE:
		retval = mchanger_cmd_log_sense6(mchanger, ctio);
		break;
	case MOVE_MEDIUM:
		retval = mchanger_cmd_move_medium(mchanger, ctio);
		break;
	case EXCHANGE_MEDIUM:
		retval = mchanger_cmd_exchange_medium(mchanger, ctio);
		break;
	case REPORT_LUNS:
		retval = mchanger_cmd_report_luns(mchanger, ctio);
		break;
	case REQUEST_SENSE:
		retval = mchanger_cmd_request_sense(mchanger, ctio);
		break;
	case PREVENT_ALLOW:
		retval = mchanger_cmd_prevent_allow_medium_removal(mchanger, ctio);
		break;
	case RESERVE:
		retval = mchanger_cmd_reserve(mchanger, ctio);
		break;
	case RELEASE:
		retval = mchanger_cmd_release(mchanger, ctio);
		break;
	case PERSISTENT_RESERVE_IN:
		retval = mchanger_cmd_persistent_reserve_in(mchanger, ctio);
		break;
	case PERSISTENT_RESERVE_OUT:
		retval = mchanger_cmd_persistent_reserve_out(mchanger, ctio);
		break;
	default:
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_COMMAND_OPERATION_CODE_ASC, INVALID_COMMAND_OPERATION_CODE_ASCQ);
		retval = 0;
		break;
	}

	if (unlikely(retval != 0))
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_HARDWARE_ERROR, 0, INTERNAL_TARGET_FAILURE_ASC, INTERNAL_TARGET_FAILURE_ASCQ);

out:
	device_send_ccb(ctio);
	mchanger_unlock(mchanger);
}

int
mchanger_reload_export_vcartridge(struct mchanger *mchanger, struct vcartridge *vcartridge)
{
	struct raw_tape *raw_tape;
	struct tape *iter, *tape = NULL;
	struct mchanger_element *element;

	mchanger_lock(mchanger);
	LIST_FOREACH(iter, &mchanger->export_list, t_list) {
		if (iter->tape_id != vcartridge->tape_id)
			continue;
		tape = iter;
		break;
	}

	if (!tape) {
		mchanger_unlock(mchanger);
		return -1;
	}

	element = get_free_storage_element(mchanger);
	if (!element) {
		mchanger_unlock(mchanger);
		return -1;
	}

	raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));
	raw_tape->vstatus &= ~MEDIA_STATUS_EXPORTED;
	if (tape_write_metadata(tape) != 0) {
		raw_tape->vstatus |= MEDIA_STATUS_EXPORTED;
		mchanger_unlock(mchanger);
		return -1;
	}

	mchanger_remove_export_tape(mchanger, tape);
	element->element_data = tape;
	update_mchanger_element_flags(element, get_mchanger_element_flags(element) | ELEMENT_DESCRIPTOR_ACCESS_MASK | ELEMENT_DESCRIPTOR_FULL_MASK);
	if (element->type == IMPORT_EXPORT_ELEMENT) {
		update_mchanger_element_flags(element, get_mchanger_element_flags(element) | IE_MASK_IMPEXP);
		mchanger_unit_attention_ie_accessed(mchanger);
	}
	else
		mchanger_unit_attention_medium_changed(mchanger);
	update_mchanger_element_pvoltag(element);
	vcartridge->vstatus = raw_tape->vstatus;
	mchanger_unlock(mchanger);
	return 0;
}

int
mchanger_reset_stats(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo)
{
	struct tdrive *tdrive;

	if (!deviceinfo->target_id)
		return 0;

	tdrive = mchanger_locate_tdrive(mchanger, deviceinfo->target_id);
	if (!tdrive)
		return -1;

	return tdrive_reset_stats(tdrive, deviceinfo);
}

int
mchanger_get_info(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo)
{
	struct tdrive *tdrive;

	if (!deviceinfo->target_id)
		return 0;

	tdrive = mchanger_locate_tdrive(mchanger, deviceinfo->target_id);
	if (!tdrive)
		return -1;

	return tdrive_get_info(tdrive, deviceinfo);
}

static void
mchanger_check_for_exported_volumes(struct mchanger *mchanger, struct vcartridge *vcartridge)
{
	struct mchanger_element *element;
	struct tape *tape;
	struct raw_tape *raw_tape; 
	uint8_t flags;

	STAILQ_FOREACH(element, &mchanger->ielem_list, me_list) {
		tape = element_vcartridge(element);
		if (!tape)
			continue;
		flags = get_mchanger_element_flags(element);
		if (flags & IE_MASK_IMPEXP)
			continue;
		raw_tape = (struct raw_tape *)(vm_pg_address(tape->metadata));
		raw_tape->vstatus |= MEDIA_STATUS_EXPORTED;
		if (tape_write_metadata(tape) != 0)
			continue;
		element->element_data = NULL;
		update_mchanger_element_flags(element, get_mchanger_element_flags(element) & ~ELEMENT_DESCRIPTOR_FULL_MASK);
		update_mchanger_element_flags(element, get_mchanger_element_flags(element) & ~IE_MASK_IMPEXP);
		update_mchanger_element_pvoltag(element);
		mchanger_unit_attention_medium_changed(mchanger);
		mchanger_insert_export_tape(mchanger, tape);
		if (tape->tape_id == vcartridge->tape_id)
			vcartridge->vstatus = raw_tape->vstatus;
	}
}

int
mchanger_vcartridge_info(struct mchanger *mchanger, struct vcartridge *vcartridge)
{
	struct mchanger_element *element;
	struct mchanger_element_list *element_list = NULL;
	struct tdrive *tdrive;
	struct tape *tape;

	mchanger_lock(mchanger);
	while ((element_list = mchanger_elem_list(mchanger, element_list)) != NULL) {
		STAILQ_FOREACH(element, element_list, me_list) {
			if (element->type == DATA_TRANSFER_ELEMENT) {
				tdrive = element->element_data;
				tape = tdrive->tape;
				if (tape && tape->tape_id == vcartridge->tape_id) {
					tape_get_info(tape, vcartridge);
					vcartridge->elem_type = element->type;
					vcartridge->elem_address = element->address;
					goto out;
				}
			}
			else if (element->type == STORAGE_ELEMENT || element->type == IMPORT_EXPORT_ELEMENT) {
				tape = element->element_data;
				if (tape && tape->tape_id == vcartridge->tape_id) {
					tape_get_info(tape, vcartridge);
					vcartridge->elem_type = element->type;
					vcartridge->elem_address = element->address;
					goto out;
				}
			}
		}
	}
	LIST_FOREACH(tape, &mchanger->export_list, t_list) {
		if (tape->tape_id != vcartridge->tape_id)
			continue;
		tape_get_info(tape, vcartridge);
		break;
	}
out:
	mchanger_check_for_exported_volumes(mchanger, vcartridge);
	mchanger_unlock(mchanger);
	return 0;
}

int
mchanger_delete_vcartridge(struct mchanger *mchanger, struct vcartridge *vcartridge)
{
	struct mchanger_element *element;
	struct mchanger_element_list *element_list = NULL;
	struct tape *tape;
	int retval = -1; 

	mchanger_lock(mchanger);
	while ((element_list = mchanger_elem_list(mchanger, element_list)) != NULL) {
		STAILQ_FOREACH(element, element_list, me_list) {
			if (element->type == DATA_TRANSFER_ELEMENT) {
				struct tdrive *tdrive = element->element_data;
				struct tape *tape = tdrive->tape;

				if (!tape || tape->tape_id != vcartridge->tape_id)
					continue;

				retval = tdrive_delete_vcartridge(tdrive, vcartridge);
				if (unlikely(retval != 0)) {
					debug_warn("mchanger_delete_vcartridge: tdrive delete vcartridge failed\n");
					goto out;
				}

				update_mchanger_element_pvoltag(element);
				update_mchanger_element_flags(element, get_mchanger_element_flags(element) & ~ELEMENT_DESCRIPTOR_FULL_MASK);
				mchanger_unit_attention_medium_changed(mchanger);
				goto out;
			}
			else if (element->type == STORAGE_ELEMENT || element->type == IMPORT_EXPORT_ELEMENT)
			{
				tape = element->element_data;
				if (!tape || tape->tape_id != vcartridge->tape_id)
					continue;
				tape_flush_buffers(tape);
				tape_free(tape, vcartridge->free_alloc);
				element->element_data = NULL;
				update_mchanger_element_pvoltag(element);
				update_mchanger_element_flags(element, get_mchanger_element_flags(element) & ~ELEMENT_DESCRIPTOR_FULL_MASK);
				if (element->type == IMPORT_EXPORT_ELEMENT) {
					update_mchanger_element_flags(element, get_mchanger_element_flags(element) & ~IE_MASK_IMPEXP);
					mchanger_unit_attention_ie_accessed(mchanger);
				}
				else
					mchanger_unit_attention_medium_changed(mchanger);
				retval = 0;
				goto out;
			}
		}
	}

	LIST_FOREACH(tape, &mchanger->export_list, t_list) {
		if (tape->tape_id != vcartridge->tape_id)
			continue;
		LIST_REMOVE(tape, t_list);
		tape_free(tape, vcartridge->free_alloc);
		retval = 0;
		break;
	}
out:
	mchanger_unlock(mchanger);
	return retval;
}

void
mchanger_reset(struct mchanger *mchanger, uint64_t i_prt[], uint64_t t_prt[], uint8_t init_int)
{
	struct reservation *reservation = &mchanger->tdevice.reservation;

	mchanger_lock(mchanger);
	if (reservation->is_reserved && reservation->type == RESERVATION_TYPE_RESERVE) {
		reservation->is_reserved = 0;
		reservation->type = 0;
	}
	mchanger_unlock(mchanger);
}

int
mchanger_device_identification(struct mchanger *mchanger, uint8_t *buffer, int length)
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

	page->device_type = T_CHANGER;
	page->page_code = DEVICE_IDENTIFICATION_PAGE;

	page_length = mchanger->unit_identifier.identifier_length + sizeof(struct device_identifier);
	page->page_length = page_length;
 
	done += sizeof(struct device_identification_page);

	idlength = mchanger->unit_identifier.identifier_length + sizeof(struct device_identifier);
	if (done + idlength > length)
	{
		return done;
	}

	unit_identifier = (struct logical_unit_identifier *)(buffer+done);
	memcpy(unit_identifier, &mchanger->unit_identifier, idlength);
	done += idlength;
	return done;
}

int
mchanger_serial_number(struct mchanger *mchanger, uint8_t *buffer, int length)
{
	struct serial_number_page *page = (struct serial_number_page *) buffer;
	int min_len;

	if (length < sizeof(struct vital_product_page))
		return -1;

	bzero(page, sizeof(struct vital_product_page));
	page->device_type = T_CHANGER; /* peripheral qualifier */
	page->page_code = UNIT_SERIAL_NUMBER_PAGE;
	page->page_length = mchanger->serial_len;

	min_len = min_t(int, mchanger->serial_len, length - sizeof(struct vital_product_page));
	if (min_len)
		memcpy(page->serial_number, mchanger->unit_identifier.serial_number, min_len);

	return (min_len+sizeof(struct vital_product_page));
}

struct tdrive *
mchanger_locate_tdrive(struct mchanger *mchanger, int target_id)
{
	struct tdrive *tdrive, *ret = NULL;
	struct mchanger_element *element;

	mchanger_lock(mchanger);
	STAILQ_FOREACH(element, &mchanger->delem_list, me_list) {
		tdrive = (struct tdrive *)(element->element_data);
		if (tdrive->tdevice.target_id != target_id)
			continue;
		ret = tdrive;
		break;
	}
	mchanger_unlock(mchanger);
	return ret;
}

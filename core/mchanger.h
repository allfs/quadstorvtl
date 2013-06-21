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

#ifndef QUADSTOR_MCHANGER_H_
#define QUADSTOR_MCHANGER_H_

#include "tdrive.h"

#define MIN_ALLOC_LEN_READ_ELEMENT_STATUS	8

#define ALL_ELEMENTS			0x00
#define MEDIUM_TRANSPORT_ELEMENT	0x01
#define STORAGE_ELEMENT			0x02
#define IMPORT_EXPORT_ELEMENT		0x03
#define DATA_TRANSFER_ELEMENT		0x04

#define IE_MASK_IMPEXP		0x02
#define IE_MASK_INENAB		0x20
#define IE_MASK_EXENAB		0x10

#define ELEMENT_DESCRIPTOR_SVALID_MASK		0x80
#define ELEMENT_DESCRIPTOR_INVERT_MASK		0x40
#define ELEMENT_DESCRIPTOR_ACCESS_MASK		0x08
#define ELEMENT_DESCRIPTOR_EXCEPT_MASK		0x04
#define ELEMENT_DESCRIPTOR_RSVD_MASK		0x02
#define ELEMENT_DESCRIPTOR_FULL_MASK		0x01

#define DEFAULT_MT_START_ADDRESS		0
#define DEFAULT_DT_START_ADDRESS		256	
#define DEFAULT_IE_START_ADDRESS		768
#define DEFAULT_ST_START_ADDRESS		1024

#define MODE_SENSE_CURRENT_VALUES				0x00
#define MODE_SENSE_CHANGEABLE_VALUES				0x01
#define MODE_SENSE_DEFAULT_VALUES				0x02
#define MODE_SENSE_SAVED_VALUES					0x03
struct element_status_data {
	uint16_t first_element_address_reported;
	uint16_t number_of_elements;
	uint32_t byte_count_of_report; /* contains the reserved field*/
};

struct element_status_page {
	uint8_t element_type_code;
	uint8_t voltag; /* avoltag and pvoltag */
	uint16_t element_descriptor_length; 
	uint32_t byte_count_of_descriptor_data; /* contains the reserved byte */
};

struct identifier {
	uint8_t  code_set; /* code set */
	uint8_t identifier_type;
	uint8_t rsvd2;
	uint8_t identifier_length;
};

struct voltag {
	uint8_t  pvoltag[36];
};

struct element_descriptor_common {
	uint16_t element_address;
	uint8_t  flags; /* access, except, full fields */
	uint8_t  rsvd; /* reserved */
	uint8_t  asc; /* additional sense code */
	uint8_t  ascq; /* additional sense code qualifier */
	uint8_t  rsvd1[3]; /* Three bytes reserved */
	uint8_t  invert;  /* svalid, invert */
	uint16_t source_storage_element_address;
};

struct element_descriptor {
	struct element_descriptor_common common; /* The common part for all */
	struct voltag voltag;
	struct identifier identifier;
	uint8_t   idbuffer[128];
};

struct mchanger_element {
	int type;
	int slottype;
	uint16_t address;
	void *element_data; /* The element data*/
	uint8_t serialnumber[32];
	STAILQ_ENTRY(mchanger_element) me_list;
	struct element_descriptor edesc; /* The element descriptor */
	uint32_t  edesc_pad; /* remove this when edesc is aligned on 8 bytes */
};

STAILQ_HEAD(mchanger_element_list, mchanger_element);

#define ROTATE_NOT_POSSIBLE				0x00
#define ROTATE_POSSIBLE					0x01

/* Masks for the storage capabilities for cartridges for the different elems */
#define STORAGE_CAPABILITY_MT_MASK			0x01
#define STORAGE_CAPABILITY_ST_MASK			0x02
#define STORAGE_CAPABILITY_IE_MASK			0x04
#define STORAGE_CAPABILITY_DT_MASK			0x08
#define STORAGE_CAPABILITY_ALL			0x0F

/* The medium changer pages as per SMC */
struct element_address_assignment_page {
	uint8_t page_code; /*page_code,Page saveable or not */
	uint8_t page_length;
	uint16_t first_medium_transport_element_address;
	uint16_t number_of_medium_transport_elements;
	uint16_t first_storage_element_address;
	uint16_t number_of_storage_elements;
	uint16_t first_import_export_element_address;
	uint16_t number_of_import_export_elements;
	uint16_t first_data_transfer_element_address;
	uint16_t number_of_data_transfer_elements;
	uint16_t reserved;
};

/* ??? This needs to be fixed for multiple transport elements */
struct transport_geometry_descriptor_page {
	uint8_t page_code; /* page saveable, page code = 0x1e */
	uint8_t page_length;
	uint8_t rotate;
	uint8_t member_number;
};

struct device_capabilities_page {
	uint8_t page_code; /* page saveable, page code = 0x1f */
	uint8_t page_length;
	uint8_t st_cap; /* the storage capabilities */
	uint8_t rsvd1; /* first reserved byte */
	uint8_t mt_mvcmd; /* mt element support for move medium */
	uint8_t st_mvcmd; /* st element support for move medium */
	uint8_t ie_mvcmd; /* ie element support for move medium */
	uint8_t dt_mvcmd; /* dt element support for move medium */
	uint32_t rsvd2; /* reserved fields of 4 bytes */
	uint8_t mt_excmd; /* mt element support for exchange medium cmd */
	uint8_t st_excmd; /* st element support for exchange medium cmd */
	uint8_t ie_excmd; /* ie element support for exchange medium cmd */
	uint8_t dt_excmd; /* dt element support for exchange medium cmd */
	uint32_t rsvd3; /* reserved fields of 4 bytes */
};

struct mchanger_handlers {
	void (*init_inquiry_data)(struct mchanger *);	
	int (*evpd_inquiry)(struct mchanger *, struct qsio_scsiio *, uint8_t , uint16_t); 
};

struct mchanger {
	struct tdevice tdevice;
	unsigned long flags;
	int changers;
	int first_changer;
	int last_changer;
	int virt_changer;

	int slots;
	int ieports;
	int drives;

	int cur_slots; 
	int cur_drives;
	int make;
	int vtype;
	int exg_possible;
	int initialized;
	int devid_len;
	int  num_elements;

	struct inquiry_data inquiry;
	struct mchanger_element_list melem_list;
	struct mchanger_element_list selem_list;
	struct mchanger_element_list ielem_list;
	struct mchanger_element_list delem_list;
	struct qs_devq *devq;
	struct mchanger_handlers handlers;

	/* Here start the mchanger pages */
	struct element_address_assignment_page assignment_page;
	struct transport_geometry_descriptor_page geometry_page;
	struct device_capabilities_page devcap_page;
	uint8_t supports_evpd;
	uint8_t supports_devid;
	uint8_t serial_len;
	uint8_t serial_as_avoltag;
	struct logical_unit_identifier unit_identifier;
	struct page_info evpd_info;
	struct page_info log_info;

	sx_t *mchanger_lock;
	BSD_LIST_HEAD(, tape) export_list;
};

#define mchanger_lock(mchgr)						\
do {									\
	debug_check(sx_xlocked_check((mchgr)->mchanger_lock));		\
	sx_xlock((mchgr)->mchanger_lock);				\
} while (0)

#define mchanger_unlock(mchgr)						\
do {									\
	debug_check(!sx_xlocked((mchgr)->mchanger_lock));		\
	sx_xunlock((mchgr)->mchanger_lock);				\
} while (0)

struct mchanger * mchanger_new(struct vdeviceinfo *deviceinfo);
void mchanger_free(struct mchanger *mchanger, int delete);
void mchanger_proc_cmd(void *changer, void *iop);
int mchanger_check_cmd(void *changer, uint8_t op);
struct vdeviceinfo;
int mchanger_mod_device(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo);
void mchanger_reset(struct mchanger *mchanger, uint64_t i_prt[], uint64_t t_prt[], uint8_t init_int);

/* Mchanger element operations */
struct mchanger_element * mchanger_add_element(struct mchanger *mchanger, int type, void *element);
int mchanger_delete_vcartridge(struct mchanger *mchanger, struct vcartridge *vcartridge);
int mchanger_vcartridge_info(struct mchanger *mchanger, struct vcartridge *vcartridge);
int mchanger_reload_export_vcartridge(struct mchanger *mchanger, struct vcartridge *vcartridge);
int mchanger_copy_vital_product_page_info(struct mchanger *mchanger, uint8_t *buffer, uint16_t allocation_length);
int mchanger_get_info(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo);

/* Tape vcartridge opterations */
struct tdrive * mchanger_add_tdrive(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo);
int mchanger_load_vcartridge(struct mchanger *mchanger, struct vcartridge *vinfo);
int mchanger_new_vcartridge(struct mchanger *mchanger, struct vcartridge *vinfo);
int mchanger_check_mnt_busy(struct mchanger *mchanger, struct tape *tape);

/* SMC Commands */
int mchanger_cmd_inquiry(struct mchanger *mchanger, struct qsio_scsiio *ctio);
int mchanger_cmd_test_unit_ready(struct mchanger *mchanger, struct qsio_scsiio *ctio);
int mchanger_cmd_exchange_medium(struct mchanger *mchanger, struct qsio_scsiio *ctio);
int mchanger_cmd_move_medium(struct mchanger *mchanger, struct qsio_scsiio *ctio);
int mchanger_cmd_initialize_element_status(struct mchanger *mchanger, struct qsio_scsiio *ctio);
int mchanger_cmd_position_to_element(struct mchanger *mchanger, struct qsio_scsiio *ctio);
int mchanger_cmd_read_element_status(struct mchanger *mchanger, struct qsio_scsiio *ctio);
int mchanger_cmd_mode_sense6(struct mchanger *mchanger, struct qsio_scsiio *ctio);
int mchanger_cmd_report_luns(struct mchanger *mchanger, struct qsio_scsiio *ctio);
int mchanger_cmd_access_ok(struct mchanger *mchanger, struct qsio_scsiio *ctio);

/* Need to implement mandatory send_diagnostic */

void vibmtl_init_handlers(struct mchanger *mchanger);
void vqtl_init_handlers(struct mchanger *mchanger);
void vadic_init_handlers(struct mchanger *mchanger);
void vhptl_init_handlers(struct mchanger *mchanger);
struct tape * element_vcartridge(struct mchanger_element *element);

static inline void
update_element_descriptor_address(struct mchanger_element *element)
{
	struct element_descriptor *edesc = &element->edesc;

	/* ??? Shouldnt we reduce these redundant information */
	edesc->common.element_address = htobe16(element->address);
} 

static inline void
update_element_descriptor_source_storage_address(struct mchanger_element *element, struct mchanger_element *source)
{
	struct element_descriptor *edesc = &element->edesc;

	edesc->common.source_storage_element_address = 0;
	edesc->common.invert &= ~ELEMENT_DESCRIPTOR_SVALID_MASK; /* note that common.invert is the byte for updating svalid */

	if (source->type != STORAGE_ELEMENT)
	{
		struct element_descriptor *source_edesc = &source->edesc;
		if (source_edesc->common.invert & ELEMENT_DESCRIPTOR_SVALID_MASK)
		{
			edesc->common.source_storage_element_address = source_edesc->common.source_storage_element_address;
			edesc->common.invert |= ELEMENT_DESCRIPTOR_SVALID_MASK; /* note that common.invert is the byte for updating svalid */
		}
		return;
	}
 
	edesc->common.source_storage_element_address = htobe16(source->address);
	edesc->common.invert |= ELEMENT_DESCRIPTOR_SVALID_MASK; /* note that common.invert is the byte for updating svalid */
}

static inline void
update_mapped_move_pvoltag(struct mchanger_element *destination, struct mchanger_element *source)
{
	memcpy(&destination->edesc.voltag.pvoltag, &source->edesc.voltag.pvoltag, sizeof(destination->edesc.voltag.pvoltag));
	bzero(&source->edesc.voltag.pvoltag, sizeof(source->edesc.voltag.pvoltag));
}

static inline void
update_mchanger_element_pvoltag(struct mchanger_element *element)
{
	struct element_descriptor *edesc = &element->edesc;
	struct tape *vcartridge = element_vcartridge(element);
	int min_len;

	if (!vcartridge)
	{
		bzero(edesc->voltag.pvoltag, sizeof(edesc->voltag.pvoltag));
		return;
	}
	else
	{
		sys_memset(edesc->voltag.pvoltag, ' ', 32);
		bzero(edesc->voltag.pvoltag+32, 4);
		min_len = min_t(int, strlen(vcartridge->label), 32);
		memcpy(edesc->voltag.pvoltag, vcartridge->label, min_len);
	}
}

static inline void
update_mchanger_element_flags(struct mchanger_element *element, uint8_t flags)
{
	struct element_descriptor *edesc = &element->edesc;

	edesc->common.flags = flags;
}

static inline uint8_t
get_mchanger_element_flags(struct mchanger_element *element)
{
	struct element_descriptor *edesc = &element->edesc;

	return edesc->common.flags;
}

void mchanger_init_address_assignment_page(struct mchanger *mchanger);
void mchanger_unit_attention_medium_changed(struct mchanger *mchanger);
void mchanger_unit_attention_mode_changed(struct mchanger *mchanger);
void mchanger_unit_attention_ie_accessed(struct mchanger *mchanger);
struct mchanger_element * mchanger_get_element(struct mchanger *mchanger, uint16_t address);
struct mchanger_element * mchanger_get_drive_element(struct mchanger *mchanger, struct tdrive *tdrive);
struct mchanger_element * mchanger_get_element_type(struct mchanger *mchanger, int type, uint16_t address);
int mchanger_device_identification(struct mchanger *mchanger, uint8_t *buffer, int length);
int mchanger_serial_number(struct mchanger *mchanger, uint8_t *buffer, int length);
void mchanger_move_update(struct mchanger_element *destination, struct mchanger_element *source);
void mchanger_exchange_update(struct mchanger_element *second_destination, struct mchanger_element *first_destination, struct mchanger_element *source);

void mchanger_unlock_element(struct mchanger *mchanger, uint32_t address);
void mchanger_lock_element(struct mchanger *mchanger, uint32_t address);

uint16_t get_first_element_address(struct mchanger *mchanger, int type);
void mchanger_cbs_disable(struct mchanger *mchanger);
void mchanger_cbs_remove(struct mchanger *mchanger);
void mchanger_init_tape_metadata(struct tdevice *tdevice, struct tape *tape);
struct tdrive * mchanger_locate_tdrive(struct mchanger *mchanger, int target_id);

#endif /* MCHANGER_H_ */

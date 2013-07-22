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

#ifndef QUADSTOR_TDRIVE_H_
#define QUADSTOR_TDRIVE_H_

#include "tdevice.h"
#include "tape.h"
#include "scsidefs.h"
#include "reservation.h"
#include "../common/commondefs.h"

/* SPACE cmd code defintions */
#define SPACE_CODE_BLOCKS			0x00
#define SPACE_CODE_FILEMARKS			0x01
#define SPACE_CODE_SEQUENTIAL_FILEMARKS		0x02
#define SPACE_CODE_END_OF_DATA			0x03
#define SPACE_CODE_SETMARKS			0x04
#define SPACE_CODE_SEQUENTIAL_SETMARKS		0x05

#define LOCATE_TYPE_BLOCK			0x00
#define LOCATE_TYPE_FILE			0x01
#define LOCATE_TYPE_EOD				0x03

#define TDRIVE_MIN_BLOCK_SIZE		1
#define TDRIVE_MAX_BLOCK_SIZE		(8 * 1024 * 1024)
#define TDRIVE_MAX_PENDING_CMDS		64
#define TDRIVE_WRITE_CACHE_SIZE		(32 * 1024 * 1024)

struct read_attribute {
	uint16_t identifier;
	uint8_t  format;
	uint16_t length;
	uint8_t  value[0];
} __attribute__ ((__packed__));

#define SERVICE_ACTION_READ_ATTRIBUTES		0x00
#define SERVICE_ACTION_READ_ATTRIBUTE_LIST	0x01
#define SERVICE_ACTION_READ_VOLUME_LIST		0x02
#define SERVICE_ACTION_READ_PARTITION_LIST	0x03

#define ATTRIBUTE_REMAINING_CAPACITY	0x0000
#define ATTRIBUTE_MAXIMUM_CAPACITY	0x0001
#define ATTRIBUTE_MEDIUM_TYPE		0x0408

#define TWOS_COMPLEMENT(x) \
	-((~(x & 0x7FFFFF) & 0x7FFFFF) + 1)

#define set_ctio_buffered(cto)           ((cto)->ccb_h.flags |= QSIO_BUFFERED)
#define ctio_buffered(cto)               ((cto)->ccb_h.flags & QSIO_BUFFERED)


struct set_encryption_status {
	uint8_t scope;
	uint8_t rdmc;
	uint8_t encryption_mode;
	uint8_t decryption_mode;
	uint8_t algorithm_index;
	uint8_t key_format;
	uint8_t rsvd[8];
	uint16_t key_length;
} __attribute__ ((__packed__));

struct encryption_status {
	uint8_t scope;
	uint8_t encryption_mode;
	uint8_t decryption_mode;
	uint8_t algorithm_index;
	uint8_t key_instance_counter;
	uint8_t rdmd;
} __attribute__ ((__packed__));


struct data_compression_page {
	uint8_t page_code;
	uint8_t page_length;
	uint8_t dcc; /* Data compression enabled(dce), dcc (capable) */
	uint8_t red; /* red, dde */ 
	uint32_t compression_algorithm; /* The compression algorithm */ 
	uint32_t decompression_algorithm; /* decompression algo */ 
	uint32_t rsvd;
} __attribute__ ((__packed__));

struct medium_partition_page {
	uint8_t page_code;
	uint8_t page_length;
	uint8_t max_addl_partitions;
	uint8_t addl_partitions_defined;
	uint8_t fdp;
	uint8_t medium_fmt_recognition;
	uint8_t partition_units;
	uint8_t reserved;
	uint16_t partition_size[MAX_TAPE_PARTITIONS];
} __attribute__ ((__packed__));

	
struct vendor_specific_page {
	uint8_t device_type;
	uint8_t page_code;
	uint8_t rsvd;
	uint8_t page_length;
	uint8_t data[0];
} __attribute__ ((__packed__));

struct device_configuration_page {
	uint8_t page_code; /* Logical unit capable of saving the page */
	uint8_t page_length;
	uint8_t active_format; /* Change Active Partition , cap, active format etc*/
	uint8_t active_partition; /* The current active partition */
	uint8_t write_buffer_full_ratio;
	uint8_t read_buffer_empty_ratio;
	uint16_t write_delay_time;
	uint8_t rew; /* Data buffer recovery, rew, rbo etc */
	uint8_t gap_size; /* Interblock gap size */
	uint8_t sew;
	uint8_t buffer_size_at_early_warning[3]; /* Buffer size to reduce, includes eod defined, eeg etc */
	uint8_t select_data_compression_algorithm;
	uint8_t asocwp; /* Associated write protect, persistent, permanent */
} __attribute__ ((__packed__));

struct device_configuration_ext_page {
	uint8_t page_code;
	uint8_t sub_page_code;
	uint16_t page_length;
	uint8_t tarpf;
	uint8_t write_mode;
	uint16_t pews;
	uint8_t vcelbre;
	uint8_t reserved[23];
} __attribute__ ((__packed__));

struct seqaccess_device_page {
	/* counters are in bytes */
	uint64_t writes_from_app;
	uint64_t writes_to_media;
	uint64_t read_from_media;
	uint64_t read_to_app;
	uint8_t cleaning_required;
} __attribute__ ((__packed__));

struct tapealert_log_page {

};

#define TDRIVE_GRANULARITY		0	
#define READ_BLOCK_LIMITS_CMDLEN	6
#define READ_POSITION_SHORT_CMDLEN	20
#define READ_POSITION_LONG_CMDLEN	32

#define READ_POSITION_SHORT		0x00
#define READ_POSITION_LONG		0x06
#define READ_POSITION_EXTENDED		0x08

struct read_position_short {
	uint8_t pos_info; /* bop, eop */
	uint8_t partition_number;
	uint16_t reserved;
	uint32_t first_block_location;
	uint32_t last_block_location;
	uint8_t reserved1;
	uint8_t blocks_in_buffer[3];
	uint32_t bytes_in_buffer;
} __attribute__ ((__packed__));

struct read_position_long {
	uint8_t pos_info; /* bop, eop */
	uint8_t rsvd[3];
	uint32_t partition_number;
	uint64_t block_number;
	uint64_t file_number;
	uint64_t set_number;
} __attribute__ ((__packed__));

struct read_position_extended {
	uint8_t pos_info;
	uint8_t partition_number;
	uint16_t additional_length;
	uint8_t reserved;
	uint8_t blocks_in_buffer[3];
	uint64_t first_block_location;
	uint64_t last_block_location;
	uint64_t bytes_in_buffer; 
} __attribute__ ((__packed__));

struct drive_parameters {
	uint8_t  granularity;
	uint8_t  max_block_size[3];
	uint8_t  min_block_size[2];
} __attribute__ ((__packed__));

struct command_support {
	uint8_t device_type;
	uint8_t support;
	uint8_t version;
	uint16_t rsvd;
	uint8_t cdb_size;
} __attribute__ ((__packed__));

#define TAPE_FLAGS_LOADED	0x01
#define TAPE_FLAGS_UNLOADED	~(TAPE_FLAGS_LOADED)

#define DEFAULT_DENSITY_CODE	0x00
#define DEFAULT_BLOCK_LENGTH	(256 * 1024)	

struct tdrive;
struct tape;

struct tdrive_handlers {
	void (*init_inquiry_data)(struct tdrive *);	
	int (*evpd_inquiry)(struct tdrive *, struct qsio_scsiio *, uint8_t , uint16_t); 
	int (*load_tape)(struct tdrive *, struct tape *);
	void (*unload_tape)(struct tdrive *);
	void (*additional_request_sense)(struct tdrive *tdrive, struct qsio_scsiio *ctio);
	uint16_t (*additional_log_sense)(struct tdrive *tdrive, uint8_t page_code, uint8_t *buffer, uint16_t buffer_len, uint16_t parameter_pointer); 
	int (*valid_medium)(struct tdrive *, int voltype);
	int (*report_density)(struct tdrive *, struct qsio_scsiio *); 
};

struct rw_error_parameter {
	uint8_t parameter_code;
	uint8_t parameter_flags;
	uint8_t parameter_length;
	uint32_t value;
} __attribute__ ((__packed__));

struct density_header {
	uint16_t avail_len;
	uint16_t rsvd;
} __attribute__ ((__packed__));

struct density_descriptor {
	uint8_t pdensity_code;
	uint8_t sdensity_code;
	uint8_t wrtok;
	uint16_t reserved;
	uint8_t bits_per_mm[3];
	uint16_t media_width;
	uint16_t tracks;
	uint32_t capacity;
	uint8_t organization[8];
	uint8_t density_name[8];
	uint8_t description[20];
	SLIST_ENTRY(density_descriptor) d_list;
} __attribute__ ((__packed__));

#define TDRIVE_STATS_ADD(tdrv,count,val)				\
do {									\
	atomic64_add(val, (atomic64_t *)&tdrv->stats.count);		\
} while (0)

struct tdrive {
	struct tdevice tdevice;
	struct qs_devq *write_devq;
	int make;
	int worm;
	struct inquiry_data inquiry;
	struct tape *tape;
	struct mchanger *mchanger;
	SLIST_HEAD(, density_descriptor) density_list;
	struct mode_header mode_header;
	uint16_t  mode_header_pad; /* alignment */
	struct mode_parameter_block_descriptor block_descriptor;
	struct device_configuration_page configuration_page; 
	struct device_configuration_ext_page configuration_ext_page; 
	struct data_compression_page compression_page;
	struct medium_partition_page partition_page;
	struct disconnect_reconnect_page disreconn_page;
	struct rw_error_recovery_page rw_recovery_page; 

	uint8_t supports_evpd;
	uint8_t supports_devid; /* Support Device Identifiers, everthing should */
	uint8_t erase_from_bot; /* Set if this tape erases the entire medium on receiving an erase command */
	uint8_t add_sense_len;
	uint8_t serial_len;

	sx_t *tdrive_lock;
	struct tdrive_handlers handlers;
	struct tdrive_stats stats;
	struct logical_unit_identifier unit_identifier;
	struct page_info evpd_info;
	struct page_info log_info;

	int flags;

	BSD_LIST_HEAD(, tape) media_list;
};

enum {
	TDRIVE_FLAGS_TAPE_LOADED,
	TDRIVE_FLAGS_FORMAT_INVALID,
};


#define tdrive_lock(tdrv)						\
do {									\
	debug_check(sx_xlocked_check((tdrv)->tdrive_lock));		\
	sx_xlock((tdrv)->tdrive_lock);				\
} while (0)

#define tdrive_unlock(tdrv)						\
do {									\
	debug_check(!sx_xlocked((tdrv)->tdrive_lock));		\
	sx_xunlock((tdrv)->tdrive_lock);				\
} while (0)

#define TDRIVE_COMPRESSION_ENABLED(tdrive) ((tdrive->compression_page.dcc & 0x80))
#define TDRIVE_GET_BUFFERED_MODE(tdrive) (((tdrive->mode_header.wp >> 4) & 0x07))
#define TDRIVE_SET_BUFFERED_MODE(tdrive) (tdrive->mode_header.wp |= (1 << 4));
#define TDRIVE_SET_NON_BUFFERED_MODE(tdrive) (tdrive->mode_header.wp &= ~(0x7 << 4));

static inline uint32_t
tdrive_get_block_length(struct tdrive *tdrive)
{
	uint8_t *blen = tdrive->block_descriptor.block_length;

	return READ_24(blen[0], blen[1], blen[2]);
}

static inline int
ctio_write_length(struct qsio_scsiio *ctio, struct tdevice *tdevice, uint32_t *block_size, uint32_t *num_blocks, int *dxfer_len)
{
	struct tdrive *tdrive = (struct tdrive *)(tdevice);
	uint8_t *cdb = ctio->cdb;
	uint8_t fixed;
	uint32_t transfer_length;

	switch (cdb[0]) {
		case WRITE_6:
			fixed = READ_BIT(cdb[1], 0);
			transfer_length = READ_24(cdb[2], cdb[3], cdb[4]);
			if (!transfer_length) {
				*block_size = 0;
				*num_blocks = 0;
				*dxfer_len = 0;
				return 0;
			}

			/* Setup the data buffers */
			if (fixed)
			{
				*block_size = tdrive_get_block_length(tdrive);
				*num_blocks = transfer_length;
			}
			else
			{
				*block_size = transfer_length;
				*num_blocks = 1;
			}
			break;
		default:
			return -1;
	}
	*dxfer_len = (*block_size * *num_blocks);
	return 0;
}

struct attribute_value {
	uint16_t identifier;
	uint8_t  format;
	uint16_t length;
	uint8_t  value[0];
} __attribute__ ((__packed__));

/* tape drive related functions */
struct tdrive * tdrive_new(struct mchanger *mchanger, struct vdeviceinfo *deviceinfo);
void tdrive_free(struct tdrive *drive, int delete);
void tdrive_proc_cmd(void *drive, void *iop);
void  __tdrive_proc_cmd(struct tdrive *tdrive, struct qsio_scsiio *ctio);
int tdrive_check_cmd(void *drive, uint8_t op);
void tdrive_proc_write_cmd(void *drive, void *iop);
int tdrive_load_tape(struct tdrive *drive, struct tape *tape);
int tdrive_unload_tape(struct tdrive *drive, struct qsio_scsiio *ctio);
int tdrive_config_worm(struct tdrive *tdrive, int enable);
int tdrive_config_compression_page(struct tdrive *tdrive, int enable);
struct vdeviceinfo;
int tdrive_copy_vital_product_page_info(struct tdrive *tdrive, uint8_t *buffer, uint16_t allocation_length);


int tdrive_new_vcartridge(struct tdrive *tdrive, struct vcartridge *vinfo);
int tdrive_load_vcartridge(struct tdrive *tdrive, struct vcartridge *vinfo);

/* exported routines */
int tdrive_compression_enabled(struct tdrive *tdrive);
int tdrive_read_position(struct tdrive *tdrive, struct tl_entryinfo *entryinfo);
int tdrive_delete_vcartridge(struct tdrive *tdrive, struct vcartridge *vcartridge);
int tdrive_vcartridge_info(struct tdrive *tdrive, struct vcartridge *vcartridge);
int tdrive_reset_stats(struct tdrive *tdrive, struct vdeviceinfo *deviceinfo);
int tdrive_get_info(struct tdrive *tdrive, struct vdeviceinfo *deviceinfo);

/* handler routines */
void vultrium_init_handlers(struct tdrive *tdrive);
void vsdlt_init_handlers(struct tdrive *tdrive);

int tdrive_media_valid(struct tdrive *tdrive, int voltype);
void tdrive_reset(struct tdrive *tdrive, uint64_t i_prt[], uint64_t t_prt[], uint8_t init_int);

int tdrive_device_identification(struct tdrive *tdrive, uint8_t *buffer, int length);
int tdrive_serial_number(struct tdrive *tdrive, uint8_t *buffer, int length);
void tdrive_update_mode_header(struct tdrive *tdrive, struct mode_parameter_header6 *header);
void tdrive_update_mode_header10(struct tdrive *tdrive, struct mode_parameter_header10 *header);
int tdrive_cmd_access_ok(struct tdrive *tdrive, struct qsio_scsiio *ctio);
int tdrive_load(struct tdrive *tdrive, struct vdeviceinfo *deviceinfo);
int tdrive_dd_exec(struct tdrive *tdrive);
void tdrive_queue_ctio(void *drive, struct qsio_scsiio *ctio);
int tdrive_check_sense(struct tdrive *tdrive, struct qsio_scsiio *ctio, uint8_t cmd);
void tdrive_empty_write_queue(struct tdrive *tdrive);
int __tdrive_load_tape(struct tdrive *tdrive, struct tape *tape);
void tdrive_cbs_disable(struct tdrive *tdrive);
void tdrive_cbs_remove(struct tdrive *tdrive);
void tdrive_init_tape_metadata(struct tdevice *tdevice, struct tape *tape);

#endif /* TDRIVE_H_ */

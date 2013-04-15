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

#ifndef PHYSLIB_H_
#define PHYSLIB_H_
#if defined(FREEBSD)
#include <sys/disk.h>
#include <libgeom.h>
#endif
#include <commondefs.h>

struct device_vspecific_id {
	uint8_t vspecific_id[128];
};

 
struct device_unknown_id {
	uint8_t unknown_id[128];
};

struct device_t10_id {
	uint8_t vendor[8];
	uint8_t product[16];
	uint8_t serialnumber[104];
};

struct device_naa_id {
	uint8_t naa_id[16];
};

struct device_eui_id {
	uint8_t eui_id[8];
};

struct device_id {
	uint32_t idflags;
	struct device_t10_id t10_id;
	struct device_naa_id naa_id;
	struct device_eui_id eui_id;
	struct device_unknown_id unknown_id;
	struct device_vspecific_id vspecific_id;
	uint32_t  avoltag_valid;
	uint8_t   serialnumber[32];
};

struct element_info {
	uint8_t estatus;
	uint8_t element_type;
	uint8_t mstatus;
	uint8_t flags;
	uint16_t element_address;
	uint32_t vtagflags;
	int last_slot;
	uint8_t desc[180];
};

#define ELEMENT_PVOLTAG(desc) ((((char *)(&desc))+12))
#define ID_FLAGS_T10			0x01
#define ID_FLAGS_EUI			0x02
#define ID_FLAGS_NAA			0x04
#define ID_FLAGS_VSPECIFIC		0x08
#define ID_FLAGS_UNKNOWN		0x10


struct physdevice {
	uint8_t type;
	uint8_t multipath;
	char vendor[8];
	char product[16];
	char revision[4];
	int serial_len;
	char serialnumber[32];
	char devname[256];
	char mdevname[512];
	uint32_t idflags;
	int online;
	struct device_t10_id t10_id;
	struct device_naa_id naa_id;
	struct device_eui_id eui_id;
	struct device_unknown_id unknown_id;
	struct device_vspecific_id vspecific_id;
};

struct physdisk {
	struct physdevice info;
	uint64_t  size;
	uint64_t  used;
	uint64_t  reserved;
	uint32_t  bid; /* Used only for the client apis */
	char group_name[TDISK_MAX_NAME_LEN];
	char mrid[TL_RID_MAX];
	uint32_t group_id;
	int8_t raiddisk;
	int8_t unmap;
	int8_t controllerdisk;
	int8_t ignore;
	int8_t write_cache;
	int partid;
	int group_flags;
#ifdef FREEBSD
	char ident[DISK_IDENT_SIZE + 8];
#endif
	TAILQ_ENTRY(physdisk) q_entry;
};

struct mode_header6 {
	uint8_t mode_data_length;
	uint8_t medium_type;
	uint8_t device_specific_parameter;
	uint8_t block_descriptor_length;
} __attribute__ ((__packed__));

struct block_descriptor {
	uint8_t density;
	uint8_t blocks[3];
	uint8_t rsvd;
	uint8_t block_length[3];
} __attribute__ ((__packed__));

TAILQ_HEAD(d_list, physdisk);

struct identify_header {
	uint8_t code_set;
	uint8_t identifier_type;
	uint8_t rsvd;
	uint8_t identifier_length;
};

struct device_events {
	uint32_t event;
	uint32_t state[16];
};

enum {
	STATE_HW_FAILURE = 0x1,
};

enum {
	STATE_ILLEGAL_REQUEST = 0x1,
};

enum {
	STATE_UA_MEDIUM_CHANGED = 0x1,
	STATE_UA_BUS_RESET = 0x02,
	STATE_UA_MODE_PARAMETERS_CHANGED = 0x03,
	STATE_UA_IMPORT_EXPORT_ACCESSED = 0x04,
	STATE_UA_MICROCODE = 0x05,
	STATE_UA_OTHER = 0x06,
};

struct sense_info {
	uint8_t sense_valid;
	uint8_t response;
	uint8_t sense_key;
	uint32_t information;
	uint8_t asc;
	uint8_t ascq;
};

#ifdef FREEBSD
#define SG_SCAN_PROG	"/quadstor/bin/cam"
#define SG_SCAN_PROG_ALL	"/quadstor/bin/cam"
#else
#define SG_SCAN_PROG	"/usr/bin/sg_map -i -sd"
#define SG_SCAN_PROG_ALL	"/usr/bin/sg_map"
#define DMSETUP_PROG	"/quadstor/sbin/dmsetup_mod"
#define SG_MAP_PROG	"/usr/bin/sg_map"
#endif


int tl_common_parse_physdisk(FILE *fp, struct d_list *dhead);
int tl_common_scan_physdisk(void);
int dump_disk(FILE *fp, struct physdisk *disk, uint32_t bid);
int dump_disk2(char *ptr, struct physdisk *disk, uint32_t bid);
struct physdisk * disk_configured(struct physdisk *disk, struct d_list *dlist);
struct physdisk * tl_common_find_raiddisk(char *name);
struct physdisk * tl_common_find_disk(char *name);
int tl_common_sync_physdisk(struct physdisk *disk);
struct physdisk * tl_common_find_physdisk2(struct physdisk *disk);

void disk_free_all(struct d_list *head);

int do_test_unit_ready(char *devname, struct sense_info *sense_info);
int do_unit_serial_number(char *devname, char *serialnumber, int *serial_len);
int device_ids_match(struct physdevice *device, struct device_id *device_id);
void parse_sense_buffer(uint8_t *sense, struct sense_info *sense_info);
void device_get_alias(char *devname, char *alias);

enum {
	SCAN_STATUS_DISK	= 0x01,
};

int device_equal(struct physdevice *device, struct physdevice *olddevice);

struct scsi_request {
	char *device;
	uint8_t *cdb;
	uint8_t *datain;
	uint8_t *dataout;
	uint8_t *sense;
	int fdflags;
	int cdb_len;
	int datain_len;
	int dataout_len;
	int sense_len;
	int resid;
	int timeout;
	uint8_t scsi_status;
};

static inline void set_scsi_request(struct scsi_request *request, char *device, int fdflags, uint8_t *cmd, int cmd_len, uint8_t *datain, int datain_len, uint8_t *dataout, int dataout_len, uint8_t *sense, int sense_len, int timeout)
{
	memset(request, 0, sizeof(*request));
	request->device = device;
	request->fdflags = fdflags;
	request->cdb = cmd;
	request->cdb_len = cmd_len;
	if (datain_len)
	{
		request->datain = datain;
		request->datain_len = datain_len;
	}
	if (dataout_len)
	{
		request->dataout = dataout;
		request->dataout_len = dataout_len;
	}
	if (sense_len)
	{
		request->sense = sense;
		request->sense_len = sense_len;
	}
	request->timeout = timeout;
}

int send_scsi_request(struct scsi_request *request);
int read_from_device(char *devpath, char *buf, int len, int offset);
int get_bdevname(char *devname, char *b_devname);
int is_ignore_dev(char *devname);
#endif 

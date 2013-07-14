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

#ifndef QS_COMMONDEFS_H_
#define QS_COMMONDEFS_H_ 1

#define TL_MAX_DISKS	512
#define TL_MAX_POOLS	512	
#define TL_DEV_NAME	"vtiodev"
#define TL_DEV		"/dev/vtiodev"
#define MIN_PHYSDISK_SIZE	(1ULL << 32)


/* Limits */

#define TL_RID_MAX	40
struct mdaemon_info {
	pid_t daemon_pid;
	char sys_rid[TL_RID_MAX];
};

#define TDISK_NAME_LEN		36
#define TL_NAME_LEN		36
#define TL_MAX_NAME_LEN		40
#define TDISK_MAX_NAME_LEN	40

struct vcartridge {
	char label[40];
	char group_name[TDISK_MAX_NAME_LEN];
	uint64_t size;
	uint64_t used; /* use percentage */
	uint32_t vstatus;
	uint32_t group_id;
	int tl_id;
	uint32_t tape_id;
	int worm; /* worm support */
	uint8_t type;
	uint8_t elem_type;
	uint16_t elem_address;
	uint32_t free_alloc;
	uint32_t loaderror;
	TAILQ_ENTRY(vcartridge) q_entry;
};

struct tdrive_stats {
	uint8_t  compression_enabled;
	uint32_t read_errors_corrected;
	uint32_t write_errors_corrected;
	uint32_t read_errors;
	uint32_t write_errors;
	uint32_t read_errors_since;
	uint32_t load_count;
	uint64_t write_ticks;
	uint64_t read_ticks;
	uint32_t write_errors_since;
	uint64_t write_bytes_processed;
	uint64_t read_bytes_processed;
	uint64_t bytes_read_from_tape;
	uint64_t bytes_written_to_tape;
	uint64_t compressed_bytes_read;
	uint64_t compressed_bytes_written; 
};

struct vdeviceinfo {
	int tl_id;
	int iscsi_tid;
	int vhba_id;
	int slots;
	int ieports;
	int drives;
	int make;
	int vtype;
	char name[40];
	int type;
	uint8_t tape_loaded;
	uint8_t free_alloc;
	uint8_t isnew;
	uint8_t mod_type; /* modification type */
	uint8_t tape_label[40];
	uint32_t tape_id;
	uint32_t target_id;
	char serialnumber[40];
	struct tdrive_stats stats;
};

struct iscsiconf {
	uint16_t tl_id;
	uint16_t target_id;
	char iqn[256];
	char IncomingUser[36];
	char IncomingPasswd[36];
	char OutgoingUser[36];
	char OutgoingPasswd[36];
	uint32_t MaxConnections;
	uint32_t MaxSessions;
	uint32_t InitialR2T;
	uint32_t ImmediateData;
	uint32_t MaxRecvDataSegmentLength;
	uint32_t MaxXmitDataSegmentLength;
	uint32_t MaxBurstLength;
	uint32_t FirstBurstLength;
	uint32_t NOPInterval;
	uint32_t NOPTimeout;
	int noauth;
	char uname[32];
	char passwd[32];
};

/* The library types */
enum {
	LIBRARY_TYPE_UNSUPPORTED	= 0x00,
	LIBRARY_TYPE_VADIC_SCALAR24	= 0x01,
	LIBRARY_TYPE_VADIC_SCALAR100	= 0x02,
	LIBRARY_TYPE_VADIC_SCALARi2000	= 0x03,
	LIBRARY_TYPE_VHP_ESL9000	= 0x04,
	LIBRARY_TYPE_VHP_ESLSERIES	= 0x05,
	LIBRARY_TYPE_VHP_EMLSERIES	= 0x06,
	LIBRARY_TYPE_VIBM_3583		= 0x07,
	LIBRARY_TYPE_VIBM_3584		= 0x08,
	LIBRARY_TYPE_VIBM_TS3100	= 0x09,
	LIBRARY_TYPE_VHP_MSLSERIES	= 0x0A,
	LIBRARY_TYPE_VHP_MSL6000	= 0x0B,
	LIBRARY_TYPE_VOVL_NEOSERIES	= 0x0C,
	LIBRARY_TYPE_VQUANTUM_M2500	= 0x0D,
};

#define LIBRARY_NAME_VADIC_SCALAR24	"ADIC Scalar 24"
#define LIBRARY_NAME_VADIC_SCALAR100	"ADIC Scalar 100"
#define LIBRARY_NAME_VADIC_SCALARi2000	"ADIC Scalar i2000"
#define LIBRARY_NAME_VHP_ESL9000	"HP StorageWorks ESL9000"
#define LIBRARY_NAME_VHP_ESLSERIES	"HP StorageWorks ESL E-Series"
#define LIBRARY_NAME_VHP_EMLSERIES	"HP StorageWorks EML E-Series"
#define LIBRARY_NAME_VIBM_3583		"IBM 3583 Ultrium Scalable Library"
#define LIBRARY_NAME_VIBM_3584		"IBM 3584 Ultra Scalable Library"
#define LIBRARY_NAME_VIBM_TS3100	"IBM IBM System Storage TS3100"
#define LIBRARY_NAME_VQUANTUM_M2500	"Quantum ATL M2500"
#define LIBRARY_NAME_VHP_MSLSERIES	" HP StorageWorks MSL 2024/4048/8096"
#define LIBRARY_NAME_VHP_MSL6000	" HP StorageWorks MSL 6000"
#define LIBRARY_NAME_VOVL_NEOSERIES	" Overland NEO 2000/4000/8000 Series"

/* The tape drive types */
enum {
	DRIVE_TYPE_UNSUPPORTED		= 0x00,
	DRIVE_TYPE_VHP_DLTVS80		= 0x01,
	DRIVE_TYPE_VHP_DLTVS160		= 0x02,
	DRIVE_TYPE_VHP_SDLT220		= 0x03,
	DRIVE_TYPE_VHP_SDLT320		= 0x04,
	DRIVE_TYPE_VHP_SDLT600		= 0x05,
	DRIVE_TYPE_VQUANTUM_SDLT220	= 0x06,
	DRIVE_TYPE_VQUANTUM_SDLT320	= 0x07,
	DRIVE_TYPE_VQUANTUM_SDLT600	= 0x08,
	DRIVE_TYPE_VHP_ULT232		= 0x09,
	DRIVE_TYPE_VHP_ULT448		= 0x0A,
	DRIVE_TYPE_VHP_ULT460		= 0x0B,
	DRIVE_TYPE_VHP_ULT960		= 0x0C,
	DRIVE_TYPE_VHP_ULT1840		= 0x0D,
	DRIVE_TYPE_VHP_ULT3280		= 0x0E,
	DRIVE_TYPE_VHP_ULT6250		= 0x0F,
	DRIVE_TYPE_VIBM_3580ULT1	= 0x10,
	DRIVE_TYPE_VIBM_3580ULT2	= 0x11,
	DRIVE_TYPE_VIBM_3580ULT3	= 0x12,
	DRIVE_TYPE_VIBM_3580ULT4	= 0x13,
	DRIVE_TYPE_VIBM_3580ULT5	= 0x14,
	DRIVE_TYPE_VIBM_3580ULT6	= 0x15,
};

#define DRIVE_NAME_VHP_DLTVS80		"HP StorageWorks DLT VS80"
#define DRIVE_NAME_VHP_DLTVS160		"HP StorageWorks DLT VS160"
#define DRIVE_NAME_VHP_SDLT220		"HP StorageWorks SDLT 220"
#define DRIVE_NAME_VHP_SDLT320		"HP StorageWorks SDLT 320"
#define DRIVE_NAME_VHP_SDLT600		"HP StorageWorks SDLT 600"
#define DRIVE_NAME_VHP_ULT232		"HP StorageWorks Ultrium 232"
#define DRIVE_NAME_VHP_ULT448		"HP StorageWorks Ultrium 448"
#define DRIVE_NAME_VHP_ULT460		"HP StorageWorks Ultrium 460"
#define DRIVE_NAME_VHP_ULT960		"HP StorageWorks Ultrium 960"
#define DRIVE_NAME_VHP_ULT1840		"HP StorageWorks Ultrium 1840"
#define DRIVE_NAME_VHP_ULT3280		"HP StoreEver Ultrium 3280"
#define DRIVE_NAME_VHP_ULT6250		"HP StoreEver Ultrium 6250"
#define DRIVE_NAME_VQUANTUM_SDLT220	"Quantum SDLT 220"
#define DRIVE_NAME_VQUANTUM_SDLT320	"Quantum SDLT 320"
#define DRIVE_NAME_VQUANTUM_SDLT600	"Quantum SDLT 600"
#define DRIVE_NAME_VIBM_3580ULT1	"IBM 3580 Ultrium1"
#define DRIVE_NAME_VIBM_3580ULT2	"IBM 3580 Ultrium2"
#define DRIVE_NAME_VIBM_3580ULT3	"IBM 3580 Ultrium3"
#define DRIVE_NAME_VIBM_3580ULT4	"IBM 3580 Ultrium4"
#define DRIVE_NAME_VIBM_3580ULT5	"IBM 3580 Ultrium5"
#define DRIVE_NAME_VIBM_3580ULT6	"IBM 3580 Ultrium6"


enum {
	VOL_TYPE_UNKNOWN	= 0x00,
	VOL_TYPE_CLEANING	= 0x01,
	VOL_TYPE_DIAGNOSTICS	= 0x02,
	VOL_TYPE_DLT_4		= 0x03,
	VOL_TYPE_VSTAPE		= 0x04,
	VOL_TYPE_SDLT_1		= 0x05,
	VOL_TYPE_SDLT_2		= 0x06,
	VOL_TYPE_SDLT_3		= 0x07,
	VOL_TYPE_LTO_1		= 0x08,
	VOL_TYPE_LTO_2		= 0x09,
	VOL_TYPE_LTO_3		= 0x0A,
	VOL_TYPE_LTO_4		= 0x0B,
	VOL_TYPE_LTO_5		= 0x0C,
	VOL_TYPE_LTO_6		= 0x0D,
};

#define SIZE_DLT4_TAPE		40
#define SIZE_VSTAPE_TAPE	80
#define SIZE_SDLT_1_TAPE	110 /* GB */ 
#define SIZE_SDLT_2_TAPE	160 /* GB */
#define SIZE_SDLT_3_TAPE	320 /* GB */
#define SIZE_LTO_1_TAPE		100
#define SIZE_LTO_2_TAPE		200
#define SIZE_LTO_3_TAPE		400
#define SIZE_LTO_4_TAPE		800
#define SIZE_LTO_5_TAPE		1500
#define SIZE_LTO_6_TAPE		2500

#define VOL_NAME_LTO_1		"LTO 1 100GB"
#define VOL_NAME_LTO_2		"LTO 2 200GB"
#define VOL_NAME_LTO_3		"LTO 3 400GB"
#define VOL_NAME_LTO_4		"LTO 4 800GB"
#define VOL_NAME_LTO_5		"LTO 5 1500GB"
#define VOL_NAME_LTO_6		"LTO 6 2500GB"
#define VOL_NAME_DLT_4		"DLT IV 40GB"
#define VOL_NAME_SDLT_1		"SuperDLT I 110GB"
#define VOL_NAME_SDLT_2		"SuperDLT I 160GB"
#define VOL_NAME_SDLT_3		"SuperDLT II 300GB"
#define VOL_NAME_VSTAPE		"VSTape 80GB"
#define VOL_NAME_CLEANING	"Cleaning Cartridge"
#define VOL_NAME_DIAG		"Diagnostics Cartridge"

#define DRIVE_MOD_TYPE_NONE		0x00
#define DRIVE_MOD_TYPE_COMPRESSION	0x01

#define TL_MAX_DEVICES		1024
#define TL_MAX_DRIVES		7
#define MAX_DINFO_DEVICES	((TL_MAX_DEVICES * TL_DEVICES_PER_BUS) + 1)
#define TL_DEVICES_PER_BUS	((1 + TL_MAX_DRIVES))

static inline int
slottype_from_drivetype(int drivetype)
{
	switch (drivetype)
	{
		case DRIVE_TYPE_VHP_DLTVS80:
			return VOL_TYPE_DLT_4;
		case DRIVE_TYPE_VHP_DLTVS160:
			return VOL_TYPE_VSTAPE;
		case DRIVE_TYPE_VHP_SDLT220:
			return VOL_TYPE_SDLT_1;
		case DRIVE_TYPE_VHP_SDLT320:
			return VOL_TYPE_SDLT_2;
		case DRIVE_TYPE_VHP_SDLT600:
			return VOL_TYPE_SDLT_3;
		case DRIVE_TYPE_VHP_ULT232:
			return VOL_TYPE_LTO_1;
		case DRIVE_TYPE_VHP_ULT448:
			return VOL_TYPE_LTO_2;
		case DRIVE_TYPE_VHP_ULT460:
			return VOL_TYPE_LTO_2;
		case DRIVE_TYPE_VHP_ULT960:
			return VOL_TYPE_LTO_3;
		case DRIVE_TYPE_VHP_ULT1840:
			return VOL_TYPE_LTO_4;
		case DRIVE_TYPE_VHP_ULT3280:
			return VOL_TYPE_LTO_5;
		case DRIVE_TYPE_VHP_ULT6250:
			return VOL_TYPE_LTO_6;
		case DRIVE_TYPE_VIBM_3580ULT1:
			return VOL_TYPE_LTO_1;
		case DRIVE_TYPE_VIBM_3580ULT2:
			return VOL_TYPE_LTO_2;
		case DRIVE_TYPE_VIBM_3580ULT3:
			return VOL_TYPE_LTO_3;
		case DRIVE_TYPE_VIBM_3580ULT4:
			return VOL_TYPE_LTO_4;
		case DRIVE_TYPE_VIBM_3580ULT5:
			return VOL_TYPE_LTO_5;
		case DRIVE_TYPE_VIBM_3580ULT6:
			return VOL_TYPE_LTO_6;
		case DRIVE_TYPE_VQUANTUM_SDLT220:
			return VOL_TYPE_SDLT_1; 
		case DRIVE_TYPE_VQUANTUM_SDLT320:
			return VOL_TYPE_SDLT_2; 
		case DRIVE_TYPE_VQUANTUM_SDLT600:
			return VOL_TYPE_SDLT_3; 
	}
	return -1;
}

struct bint_stats {
	uint64_t pad1;
	uint64_t pad2;
	uint64_t pad3;
	uint64_t pad4;
	uint64_t pad5;
	uint64_t pad6;
	uint64_t pad7;
	uint64_t pad8;
	uint64_t pad9;
	uint64_t pad10;
};

#define V2_DISK		0x1
#define RID_SET		0x4

struct raw_bdevint {
	uint8_t flags;
	uint32_t bid;
	uint32_t sector_shift;
	uint64_t usize;
	uint64_t free;
	uint64_t b_start;
	uint64_t b_end;
	uint8_t magic[8];
	uint8_t vendor[8];
	uint8_t product[16];
	uint8_t serialnumber[32];
	uint32_t group_id;
	int32_t group_flags;
	uint8_t write_cache;
	uint8_t quad_prod[4];
	uint8_t pad2[3];
	uint64_t pad3;
	uint64_t pad4;
	uint64_t pad5;
	uint64_t pad6;
	uint64_t pad7;
	uint64_t pad8;
	uint64_t pad9;
	uint64_t pad10;
	struct bint_stats stats;
	char mrid[TL_RID_MAX];
	char group_name[TDISK_MAX_NAME_LEN];
} __attribute__ ((__packed__));

struct group_conf {
	char name[TDISK_MAX_NAME_LEN];
	uint32_t group_id;
	int worm;
};

#define DEFAULT_GROUP_NAME	"Default"

struct bdev_info {
	uint32_t bid;
	char devpath[256];
	uint64_t size;
	uint64_t usize;
	uint64_t free;
	uint64_t reserved;
	uint8_t  isnew;
	uint8_t  ismaster;
	uint8_t  unmap;
	uint8_t  write_cache;
	uint8_t enable_comp;
	uint8_t free_alloc;
	uint8_t  vendor[8];
	uint8_t  product[16];
	uint8_t  serialnumber[32];
	uint32_t max_index_groups;
	uint32_t group_id;
	struct bint_stats stats;
	char rid[TL_RID_MAX];
	char errmsg[256];
};

struct tl_entryinfo {
	uint32_t tl_id;
	uint32_t count;
	uint32_t size;
	uint32_t flags;
	uint32_t error;
};

struct fc_rule_config {
	uint64_t wwpn[2];
	uint32_t target_id;
	int rule;
};

enum {
	FC_RULE_ALLOW,
	FC_RULE_DISALLOW,
};

#define MIRROR_RECV_TIMEOUT_MAX			120
#define MIRROR_CONNECT_TIMEOUT_MAX		70
#define MIRROR_SEND_TIMEOUT_MAX			120
#define MIRROR_SYNC_TIMEOUT_MAX			90
#define MIRROR_SYNC_RECV_TIMEOUT_MAX		90
#define MIRROR_SYNC_SEND_TIMEOUT_MAX		90
#define CLIENT_SEND_TIMEOUT_MAX			90
#define CLIENT_CONNECT_TIMEOUT_MAX		90
#define CONTROLLER_RECV_TIMEOUT_MAX		70
#define CONTROLLER_CONNECT_TIMEOUT_MAX		70
#define NODE_SYNC_TIMEOUT_MAX			60
#define HA_CHECK_TIMEOUT_MAX			60
#define HA_PING_TIMEOUT_MAX			60

#define MIRROR_RECV_TIMEOUT_MIN			20
#define MIRROR_CONNECT_TIMEOUT_MIN		5
#define MIRROR_SEND_TIMEOUT_MIN			20
#define MIRROR_SYNC_TIMEOUT_MIN			15	
#define MIRROR_SYNC_RECV_TIMEOUT_MIN		15
#define MIRROR_SYNC_SEND_TIMEOUT_MIN		15
#define CLIENT_SEND_TIMEOUT_MIN			15
#define CLIENT_CONNECT_TIMEOUT_MIN		5
#define CONTROLLER_RECV_TIMEOUT_MIN		15
#define CONTROLLER_CONNECT_TIMEOUT_MIN		5
#define NODE_SYNC_TIMEOUT_MIN			20
#define HA_CHECK_TIMEOUT_MIN			5
#define HA_PING_TIMEOUT_MIN			2

enum {
	WRITE_CACHE_DEFAULT,
	WRITE_CACHE_FLUSH,
	WRITE_CACHE_FUA,
};

enum {
	GROUP_FLAGS_MASTER,
	GROUP_FLAGS_WORM,
	GROUP_FLAGS_UNMAP_ENABLED,
	GROUP_FLAGS_UNMAP,
};

enum {
	DRIVE_MOD_WORM		= 0x01,
};

enum {
	MOD_OP_ENABLE		= 0x01,
	MOD_OP_DISABLE		= 0x02,
};

struct drive_info {
	uint16_t target_id;
	uint16_t make;
	char name[40];
	char serialnumber[40];
} __attribute__ ((__packed__));

struct vtl_info {
	char name[TL_MAX_NAME_LEN];
	char serialnumber[40];
	uint16_t tl_id;
	uint16_t slots;
	uint8_t type;
	uint8_t ieports;
	uint8_t drives;
	uint8_t pad;
	struct drive_info drive_info[TL_MAX_DRIVES];
} __attribute__ ((__packed__));

struct raw_partition {
	uint64_t size;
	uint64_t tmaps_block;
};

#define MAX_TAPE_PARTITIONS	4

#define TAPE_FLAGS_V2		0x1

struct raw_tape {
	uint16_t csum;
	uint32_t  vstatus;
	uint16_t  flags;
	uint64_t size;
	uint64_t set_size; /* SET CAPACITY */
	char label[40];
	uint32_t tape_id;
	uint16_t device_type;
	uint16_t group_id;
	uint16_t make;
	uint16_t worm;
	struct vtl_info vtl_info;
	uint64_t pad1[4];
	struct raw_partition raw_partitions[MAX_TAPE_PARTITIONS];
} __attribute__ ((__packed__));

#define MAX_VTAPES		60000
#define BDEV_META_RESERVED	262144
#define BDEV_META_OFFSET	BDEV_META_RESERVED
#define BINT_UNIT_SHIFT		26
#define BINT_UNIT_SIZE		(1ULL << BINT_UNIT_SHIFT)
#define BINT_UNIT_MASK		(~(BINT_UNIT_SIZE - 1))
#define BINT_RESERVED_SEGMENTS	4
#define BINT_RESERVED_SIZE	(BINT_UNIT_SIZE * BINT_RESERVED_SEGMENTS)

#define VTAPES_OFFSET		(BDEV_META_OFFSET + LBA_SIZE)
#define DEFAULT_IE_PORTS	0x04

enum {
	MEDIA_STATUS_ACTIVE = 0x01,
	MEDIA_STATUS_EXPORTED = 0x02,
	MEDIA_STATUS_FOREIGN = 0x04,
	MEDIA_STATUS_UNKNOWN = 0x08,
	MEDIA_STATUS_BLANK = 0x10,
	MEDIA_STATUS_CLEANING = 0x20,
	MEDIA_STATUS_REUSE = 0x40,
	MEDIA_STATUS_SCAN  = 0x80,
	MEDIA_STATUS_DIAGNOSTICS = 0x100,
};

#endif /* COMMONDEFS_H_ */

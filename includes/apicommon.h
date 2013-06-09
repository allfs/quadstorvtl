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

#ifndef APICOMMON_H_
#define APICOMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/un.h> /* for unix domain sockets */
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <assert.h>
#include "messages.h"
#if defined(LINUX)
#include <queue.h>
#include <mntent.h>
#include <scsi/scsi.h>
#include <scsi/sg.h>
#include <linux/netlink.h>
#include <byteswap.h>
#include <endian.h>
#include <linux/mtio.h>
#include "linuxuserdefs.h"
#elif defined(FREEBSD)
#include <sys/queue.h>
#include <sys/disk.h>
#include <sys/endian.h>
#include <sys/mtio.h>
#include <sys/disk.h>
#include <uuid.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <camlib.h>
#include <camlib.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_sa.h>
#include <cam/scsi/scsi_message.h>
#include "bsduserdefs.h"
#else
#error "Unsupported arch"
#endif
#include "../common/commondefs.h"
#include "../common/ioctldefs.h"
#include "vdevice.h"
#include "physlib.h"

enum {
	LOG_TYPE_INFORMATION	= 0x01,
	LOG_TYPE_WARNING	= 0X02,
	Log_TYPE_ERROR		= 0x03,
};

#define MDAEMON_NAME		"mdaemon"
#define MDAEMON_PORT		9999
#define MDAEMON_BACKLOG		64
#ifdef FREEBSD
#define MDAEMON_PATH		"/quadstor/.mdaemon"
#else
#define MDAEMON_PATH		"QUADSTOR_ABSTRACT_NAMESPACE"
#endif
#define IETADM_PATH		"/quadstor/bin/ietadm"
#define QUADSTOR_CONFIG_FILE "/quadstor/etc/quadstor.conf"

struct tl_msg {
	int msg_id;
	int msg_len;
	int msg_resp;
	char *msg_data;
} __attribute__ ((__packed__));

struct tl_comm {
	int sockfd;
	char hname[256];
	int  ai_family;
};

#define ISCSI_UNAME_LEN		30
#define ISCSI_PASSWD_LEN	30

#define HEADER_DIGEST_NONE	0x00
#define HEADER_DIGEST_CRC	0x01

#define DATA_DIGEST_NONE	0x00
#define DATA_DIGEST_CRC		0x01
#define TL_MAX_ISCSI_CONN	16

struct tdriveconf {
	struct vdevice vdevice;
	int host_id; /* Linux specific */
	int type;
	int tape_loaded;
	char tape_label[40];
	int compression_enabled;
	uint32_t libid;
	uint32_t driveid;
	struct physdrive *tdrive;
	TAILQ_ENTRY(tdriveconf) q_entry;
};

TAILQ_HEAD(dlist, tdriveconf);

enum {
	VTL_OP_MASKBLANK	= 0x01,
	VTL_OP_DOVERIFY		= 0x02,
	VTL_OP_DDENABLE		= 0x03,
	DRIVE_OP_DDENABLE	= 0x04,
	DRIVE_OP_DOVERIFY	= 0x05,
	DRIVE_OP_MAP		= 0x06,
	DRIVE_OP_UNMAP		= 0x07,
	DRIVE_OP_ATTACH_DELETE	= 0x08,
	VTL_OP_REPENABLE	= 0x09,
};

struct vtlconf {
	struct vdevice vdevice;
	int host_id;
	int type;
	int slots;
	int ieports;
	int drives;
	struct dlist drive_list;
};

struct tl_blkdevinfo {
	/* The next four fields are filled up on start up */
	uint32_t bid;
	uint32_t group_id;
	struct physdisk disk;
	char devname[256];
	dev_t b_dev;
	int offline;
	TAILQ_ENTRY(tl_blkdevinfo) q_entry;
	TAILQ_ENTRY(tl_blkdevinfo) g_entry;
	struct group_info *group;
	struct vlist vol_list;
};

TAILQ_HEAD(blist, tl_blkdevinfo);

struct group_info {
	char name[TL_MAX_NAME_LEN];
	char mrid[TL_RID_MAX];
	uint32_t group_id;
	int disks;
	int worm;
	TAILQ_ENTRY(group_info) q_entry;
	TAILQ_HEAD(, tl_blkdevinfo) bdev_list;
};

TAILQ_HEAD(group_list, group_info);

#define VIRT_EQUIVALENT(vtlconf)	(((struct tl_vtlconf *)(vtlconf->virt_vdevice->device)))

#define TL_MSG_ID          "msgid:"
#define TL_MSG_LEN         "msglen:"
#define TL_MSG_RESP         "msgresp:"
#define TL_NEWLINE_SEQ     "\n"

/* error code */
#define TL_ENOMEM          -1 
#define TL_MSG_INVALID	   -2

enum {
	SEVERITY_CRITICAL	= 0x01,
	SEVERITY_ERROR,
	SEVERITY_WARNING,
	SEVERITY_INFORMATION
};

#define SEVERITY_MSG_CRITICAL		"Critical"
#define SEVERITY_MSG_ERROR		"Error"
#define SEVERITY_MSG_WARNING		"Warning"
#define SEVERITY_MSG_INFORMATION	"Information"

/* API prototypes */
struct tl_comm * tl_msg_make_connection(void);
void tl_msg_free_message(struct tl_msg *msg);
void tl_msg_free_data(struct tl_msg *msg);
void tl_msg_free_connection(struct tl_comm *tl_comm);
void tl_msg_close_connection(struct tl_comm *tl_comm);
int tl_msg_send_message(struct tl_comm *tl_comm, struct tl_msg *msg);
struct tl_msg * tl_msg_recv_message(struct tl_comm *comm); 
struct tl_msg * tl_msg_recv_message2(struct tl_comm *comm);

void group_list_free(struct group_list *group_list);
int tl_common_parse_group(FILE *fp, struct group_list *group_list);
int tl_common_parse_vtlconf(FILE *conffp, int doioctl);
struct tl_list *tl_common_parse_volconf(FILE *conffp);
struct tl_list * tl_common_parse_driveconf(FILE *conffp);
struct tl_list * tl_common_get_drivelist(FILE *fp);
int tl_ioctl2(char *dev, unsigned long int request, void *arg);
int tl_ioctl(unsigned long int request, void *arg);
int tl_ioctl_void(unsigned long int request);
int usage_percentage(uint64_t size, uint64_t used);
struct vcartridge * parse_vcartridge(FILE *fp);
int get_voltype(int drivetype);

static inline void *
alloc_buffer(int buffer_len)
{
	void *ret;

	ret = malloc(buffer_len);
	if (!ret)
	{
		return NULL;
	}
	memset(ret, 0, buffer_len);
	return ret;
}

void get_data_str(double bytes, char *buf);
void get_transfer_rate(double bytes, long elapsed, char *buf);

#ifdef ENABLE_DEBUG
#define DEBUG_INFO(fmt,args...)		syslog(LOG_ERR, "info: "fmt, ##args)
#else
#define DEBUG_INFO(fmt,args...)
#endif

#ifdef ENABLE_ASSERT
#define DEBUG_BUG_ON(cond) do { if (((cond)) != 0) *(char *)(NULL) = 'A'; } while(0)
#else
#define DEBUG_BUG_ON(cond)
#endif

#define DEBUG_WARN(fmt,args...)		syslog(LOG_WARNING, "WARN: %s:%d "fmt, __FUNCTION__, __LINE__, ##args)
#define DEBUG_ERR(fmt,args...)		syslog(LOG_ERR, "ERROR: %s:%d "fmt, __FUNCTION__, __LINE__, ##args)
#define DEBUG_CRIT(fmt,args...)		syslog(LOG_ERR, "CRIT: %s:%d "fmt, __FUNCTION__, __LINE__, ##args)
#define DEBUG_INFO_NEW(fmt,args...)	syslog(LOG_ERR, "%s:%d "fmt, __FUNCTION__, __LINE__, ##args)
#define DEBUG_WARN_NEW(fmt,args...)	syslog(LOG_WARNING, "WARN: %s:%d "fmt, __FUNCTION__, __LINE__, ##args)

#define DEBUG_INFO_SERVER		DEBUG_INFO
#define DEBUG_WARN_SERVER		DEBUG_WARN_NEW
#define DEBUG_ERR_SERVER		DEBUG_WARN_NEW
#define DEBUG_CRIT_SERVER		DEBUG_WARN_NEW

struct fc_rule {
	int rule;
	int target_id;
	char wwpn[24];
	char wwpn1[24];
	char vtl[TL_MAX_NAME_LEN];
	TAILQ_ENTRY(fc_rule) q_entry;
};

TAILQ_HEAD(fc_rule_list, fc_rule);

struct fc_rule_spec {
	int rule;
	char wwpn[24];
	char wwpn1[24];
	char vtl[TL_MAX_NAME_LEN];
};

#define MEDIUM_TRANSPORT_ELEMENT	0x01
#define STORAGE_ELEMENT			0x02
#define IMPORT_EXPORT_ELEMENT		0x03
#define DATA_TRANSFER_ELEMENT		0x04

static inline char *
get_element_type_str(int type)
{
	switch (type) {
	case STORAGE_ELEMENT:
		return "Slot";
	case IMPORT_EXPORT_ELEMENT:
		return "Import/Export";
	case DATA_TRANSFER_ELEMENT:
		return "Drive";
	case MEDIUM_TRANSPORT_ELEMENT:
	default:
		return "Unknown";
	} 
}
#endif /* API_COMMON_H_ */

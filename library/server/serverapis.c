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

#include <apicommon.h>
#include <stdarg.h>
#include <tlsrvapi.h>
#include <netdb.h>
#include <sys/mount.h>
#include <pthread.h>
#include <vdevice.h>
#include <time.h>
#include <assert.h>
#include "diskinfo.h"
#include "sqlint.h" 
#include "ietadm.h"
#include "md5.h"

struct vcartridge *vcart_list[MAX_VTAPES];
struct group_info *group_list[TL_MAX_POOLS];
struct fc_rule_list fc_rule_list = TAILQ_HEAD_INITIALIZER(fc_rule_list);  
pthread_mutex_t pmap_lock = PTHREAD_MUTEX_INITIALIZER;
char default_group[TDISK_MAX_NAME_LEN];
uint64_t max_vcart_size;

int done_socket_init;
int done_server_init;
int done_init;
int enable_drive_compression;
pthread_mutex_t daemon_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t daemon_cond = PTHREAD_COND_INITIALIZER;
pthread_cond_t socket_cond = PTHREAD_COND_INITIALIZER;

struct vdevice *device_list[TL_MAX_DEVICES];
struct tl_blkdevinfo *bdev_list[TL_MAX_DISKS];
char sys_rid[TL_RID_MAX];
char sys_rid_stripped[TL_RID_MAX];
pthread_mutex_t bdev_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t device_lock = PTHREAD_MUTEX_INITIALIZER;

static void delete_volumes(struct vdevice *vdevice);
static int delete_vdevice(struct vdevice *vdevice, int free_alloc);

static int load_drive(struct vdevice *vdevice);
static int load_vtl(struct vdevice *vdevice);
static int check_blkdev_exists(char *devname);

extern struct d_list disk_list;
struct mdaemon_info mdaemon_info;

uint32_t
get_next_tape_id(void)
{
	int i, retval;

	for (i = 1; i < MAX_VTAPES; i++) {
		if (!vcart_list[i]) {
			retval = sql_virtvol_tapeid_unique(i);
			if (retval == 0)
				return i;
		}
	}
	return 0;
}

static int
target_name_valid(char *name)
{
	int i;
	int len = strlen(name);

	for (i = 0; i < len; i++) {
		if (!isalnum(name[i]) && name[i] != '_' && name[i] != '-')
			return 0;
	}
	return 1;
}

static struct tl_blkdevinfo *
group_get_master(struct group_info *group_info)
{
	struct tl_blkdevinfo *blkdev;

	TAILQ_FOREACH(blkdev, &group_info->bdev_list, g_entry) {
		if (blkdev->ismaster)
			return blkdev;
	}
	return NULL;
}

void
bdev_group_insert(struct group_info *group_info, struct tl_blkdevinfo *blkdev)
{
	blkdev->group = group_info;
	blkdev->group_id = group_info->group_id;
	TAILQ_INSERT_TAIL(&group_info->bdev_list, blkdev, g_entry);
}

void
bdev_add(struct group_info *group_info, struct tl_blkdevinfo *blkdev)
{
	blkdev->group = group_info;
	blkdev->group_id = group_info->group_id;
	bdev_list[blkdev->bid] = blkdev;
	TAILQ_INSERT_TAIL(&group_info->bdev_list, blkdev, g_entry);
	blkdev->offline = 0;
}

void
bdev_remove(struct tl_blkdevinfo *blkdev)
{
	struct group_info *group_info = blkdev->group;

	if (group_info) {
		TAILQ_REMOVE(&group_info->bdev_list, blkdev, g_entry); 
		blkdev->group = NULL;
	}
	bdev_list[blkdev->bid] = NULL;
}

static int
group_name_exists(char *groupname)
{
	struct group_info *group_info;
	int i;

	for (i = 0; i < TL_MAX_POOLS; i++) {
		group_info = group_list[i];
		if (!group_info)
			continue;
		if (strcasecmp(group_info->name, groupname) == 0) 
			return 1;
	}
	return 0;
}

int
group_get_disk_count(struct group_info *group_info)
{
	struct tl_blkdevinfo *blkdev;
	int count = 0;

	TAILQ_FOREACH(blkdev, &group_info->bdev_list, g_entry) {
		count++;
	}
	return count;
}

struct group_info * 
find_group(uint32_t group_id)
{
	struct group_info *group_info;
	int i;

	for (i = 0; i < TL_MAX_POOLS; i++) {
		group_info = group_list[i];
		if (!group_info)
			continue;
		if (group_info->group_id == group_id)
			return group_info;
	}
	return NULL;
}

#ifdef FREEBSD
int
gen_rid(char *rid)
{
	char *tmp;
	uint32_t status;
	uuid_t uuid;

	uuid_create(&uuid, &status);
	if (status != uuid_s_ok)
		return -1;
 
	uuid_to_string(&uuid, &tmp, &status);
	if (status != uuid_s_ok)
		return -1;
	strcpy(rid, tmp);
	return 0;
}
#else
int
gen_rid(char *rid)
{
	char buf[256];
	FILE *fp;

	fp = popen("/usr/bin/uuidgen", "r");
	if (!fp) {
		DEBUG_WARN_NEW("Failed to run /usr/bin/uuidgen program\n");
		return -1;
	}

	fgets(buf, sizeof(buf), fp);
	pclose(fp);
	if (!strlen(buf)) {
		DEBUG_WARN_NEW("Failed to generate uuid string\n");
		return -1;
	}

	if (buf[strlen(buf) - 1] == '\n')
		buf[strlen(buf) - 1] = 0;

	if (strlen(buf) != 36) {
		DEBUG_WARN_NEW("Invalid uuid string %s. Invalid length %d\n", buf, (int)strlen(buf));
		return -1;
	}
	strcpy(rid, buf);
	return 0;
}
#endif

int
sync_blkdev(struct tl_blkdevinfo *blkdev)
{
	struct physdisk disk;
	int retval;

	memset(&disk, 0, sizeof(struct physdisk));
	memcpy(&disk, &blkdev->disk, offsetof(struct physdisk, q_entry));

	retval = tl_common_sync_physdisk(&disk);
	if (retval != 0) {
		DEBUG_ERR_SERVER("Unable to locate disk. Disk offline ???\n");
		blkdev->offline = 1;
		return -1;
	}

	memcpy(&blkdev->disk, &disk, offsetof(struct physdisk, q_entry));
	retval = is_ignore_dev(blkdev->disk.info.devname);
	if (retval)
		goto err;

	strcpy(blkdev->devname, blkdev->disk.info.devname);

	return 0;
err:
	return -1;
}

static void
tl_server_msg_invalid(struct tl_comm *comm, struct tl_msg *msg)
{
	int msg_len = strlen(MSG_STR_INVALID_MSG);

	tl_msg_free_data(msg);
	msg->msg_len = msg_len;
	msg->msg_resp = MSG_RESP_ERROR;

	msg->msg_data = malloc(msg_len+1);
	if (!msg->msg_data)
	{
		msg->msg_len = 0;
		tl_msg_send_message(comm, msg);
		tl_msg_free_message(msg);
		tl_msg_close_connection(comm);
		return;
	}
	strcpy(msg->msg_data, MSG_STR_INVALID_MSG);
	tl_msg_send_message(comm, msg);
	tl_msg_free_message(msg);
	tl_msg_close_connection(comm);
}

static void
tl_server_send_message(struct tl_comm *comm, struct tl_msg *msg, char *new_msg)
{
	int msg_len = 0;

	if (strlen(new_msg) > 0)
	{
		msg_len = strlen(new_msg);
	}

	tl_msg_free_data(msg);

	if (msg_len)
	{
		msg->msg_data = malloc(msg_len + 1);
		if (!msg->msg_data)
		{
			msg->msg_len = 0;
			tl_msg_send_message(comm, msg);
			tl_msg_free_message(msg);
			tl_msg_close_connection(comm);
			return;
		}
	}

	if (msg_len > 0)
	{
		strcpy(msg->msg_data, new_msg);
		msg->msg_len = msg_len;
	}
	tl_msg_send_message(comm, msg);
	tl_msg_free_message(msg);
	tl_msg_close_connection(comm);
}

void
tl_server_msg_failure(struct tl_comm *comm, struct tl_msg *msg)
{
	int msg_len = strlen(MSG_STR_COMMAND_FAILED);

	tl_msg_free_data(msg);
	msg->msg_len = msg_len;
	msg->msg_resp = MSG_RESP_ERROR;

	if (msg_len)
	{
		msg->msg_data = malloc(msg_len + 1);
		if (!msg->msg_data)
		{
			msg->msg_len = 0;
			tl_msg_send_message(comm, msg);
			tl_msg_free_message(msg);
			tl_msg_close_connection(comm);
			return;
		}
	}
	strcpy(msg->msg_data, MSG_STR_COMMAND_FAILED); 
	tl_msg_send_message(comm, msg);
	tl_msg_free_message(msg);
	tl_msg_close_connection(comm);
	return;
}

void
tl_server_msg_failure2(struct tl_comm *comm, struct tl_msg *msg, char *newmsg)
{
	int msg_len = strlen(newmsg);

	tl_msg_free_data(msg);
	msg->msg_len = msg_len;
	msg->msg_resp = MSG_RESP_ERROR;

	if (msg_len)
	{
		msg->msg_data = malloc(msg_len + 1);
		if (!msg->msg_data)
		{
			msg->msg_len = 0;
			tl_msg_send_message(comm, msg);
			tl_msg_free_message(msg);
			tl_msg_close_connection(comm);
			return;
		}
	}

	strcpy(msg->msg_data, newmsg); 
	tl_msg_send_message(comm, msg);
	tl_msg_free_message(msg);
	tl_msg_close_connection(comm);
}

void
tl_server_msg_success(struct tl_comm *comm, struct tl_msg *msg)
{
	tl_msg_free_data(msg);
	msg->msg_resp = MSG_RESP_OK;
	tl_msg_send_message(comm, msg);
	tl_msg_free_message(msg);
	tl_msg_close_connection(comm);
}

static int 
add_new_vcartridge(struct vcartridge *vinfo, char *errmsg)
{
	int retval;
	PGconn *conn;

	conn = pgsql_begin();
	if (!conn) {
		return -1;
	}

	retval = sql_add_vcartridge(conn, vinfo);
	if (retval != 0) {
		sprintf(errmsg, "Adding VCartridge information to DB failed\n");
		goto rollback;
	}

	retval = tl_ioctl(TLTARGIOCNEWVCARTRIDGE, vinfo);
	if (retval != 0) {
		sprintf(errmsg, "Addition of a new VCartridge failed\n");
		goto rollback;
	}

	retval = pgsql_commit(conn);
	if (retval != 0) {
		tl_ioctl(TLTARGIOCDELETEVCARTRIDGE, vinfo);
		return -1;
	}

	return 0;
rollback:
	pgsql_rollback(conn);
	return -1;
} 

static int
check_blkdev_exists(char *devname)
{
	struct tl_blkdevinfo *blkdev;
	int i;

	pthread_mutex_lock(&bdev_lock);
	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		if (strcmp(blkdev->devname, devname) == 0)
		{
			pthread_mutex_unlock(&bdev_lock);
			return 1;
		}
	}
	pthread_mutex_unlock(&bdev_lock);
	return 0;
}

static int
get_next_group_id(void)
{
	int i;

	for (i = 1; i < TL_MAX_POOLS; i++) {
		if (group_list[i])
			continue;
		return i;
	}
	return 0;

}

uint32_t next_bid = 1;

static int
get_next_bid(void)
{
	int i;

again:
	for (i = next_bid; i < TL_MAX_DISKS; i++) {
		if (bdev_list[i])
			continue;
		next_bid = i+1;
		return i;
	}
	if (next_bid != 1) {
		next_bid = 1;
		goto again;
	}
	return 0;
}

struct tl_blkdevinfo *
blkdev_new(char *devname)
{
	struct tl_blkdevinfo *blkdev;
	int bid;

	bid = get_next_bid();
	if (!bid) {
		DEBUG_ERR("Unable to get bid\n");
		return NULL;
	}

	blkdev = malloc(sizeof(struct tl_blkdevinfo));
	if (!blkdev)
	{
		DEBUG_ERR("Out of memory\n");
		return NULL;
	}
	memset(blkdev, 0, sizeof(struct tl_blkdevinfo));
	TAILQ_INIT(&blkdev->vol_list);
	blkdev->bid = bid;
	return blkdev;
}

static inline int
vdevice_tl_id(struct vdevice *vdevice)
{
	return vdevice->tl_id;
}

struct scan_info {
	struct tl_comm *comm;
	struct tl_msg *msg;
	int force;
	uint32_t id;
};

static int
tl_server_disk_check(struct tl_comm *comm, struct tl_msg *msg)
{
	int error = 0;
	char sqlcmd[64];

	snprintf(sqlcmd, sizeof(sqlcmd), "UPDATE SYSINFO SET DCHECK='1'");
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	if (error != 0) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}
	tl_server_msg_success(comm, msg);
	return 0;
}

static int
__tl_server_delete_vtl_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	uint32_t tl_id;
	int retval;
	struct vdevice *vdevice;

	retval = sscanf(msg->msg_data, "tl_id: %u\n", &tl_id);
	if (retval != 1)
	{
		DEBUG_ERR("Invalid msg data %s\n", msg->msg_data);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	if (tl_id >= TL_MAX_DEVICES)
	{
		DEBUG_ERR("Invalid tl_id passed %u\n", tl_id);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vdevice = device_list[tl_id];
	if (!vdevice)
	{
		DEBUG_ERR("No vtl at specified tape library id %d\n", tl_id);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	retval = tl_server_remove_vtl_fc_rules(tl_id);
	if (retval != 0) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	retval = delete_vdevice(vdevice, 1);
	if (retval != 0)
	{
		DEBUG_ERR("Delete vdevice from kernel mod list failed\n");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	device_list[vdevice_tl_id(vdevice)] = NULL;
	delete_volumes(vdevice);
	free_vdevice(vdevice);
	tl_server_msg_success(comm, msg);
	return 0;
}

int
tl_server_delete_vtl_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	int retval;

	pthread_mutex_lock(&device_lock);
	retval = __tl_server_delete_vtl_conf(comm, msg);
	pthread_mutex_unlock(&device_lock);
	return retval;
}


static int
tl_server_delete_vol_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	int retval;
	int tl_id;
	uint32_t tape_id;
	char errmsg[256];
	struct vcartridge *vinfo = NULL;
	struct vdevice *vdevice;
	struct vcartridge *tmp;
	PGconn *conn;

	retval = sscanf(msg->msg_data, "tl_id: %d\ntape_id: %u\n", &tl_id, &tape_id);
	if (retval != 2) {
		DEBUG_WARN("Invalid data %s\n", msg->msg_data);
		tl_server_msg_invalid(comm, msg);
		return -1;
	}

	if (tl_id < 0 || tl_id >= TL_MAX_DEVICES || !device_list[tl_id]) {
		DEBUG_WARN("Invalid tl_id %d\n", tl_id);
		tl_server_msg_invalid(comm, msg);
		return -1;
	}

	vdevice = device_list[tl_id];

	TAILQ_FOREACH(tmp, &vdevice->vol_list, q_entry) { 
		if (tmp->tape_id != tape_id)
			continue;
		vinfo = tmp;
		break;
	}

	if (!vinfo) {
		snprintf(errmsg, sizeof(errmsg), "Unable to find the volume for delete\n");
		goto senderr;
	}

	conn = pgsql_begin();
	if (!conn) {
		snprintf(errmsg, sizeof(errmsg), "Unable to connect to db\n");
		goto senderr;
	}

	retval = sql_delete_vcartridge(conn, vinfo->label);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Unable to delete the volume from db\n");
		pgsql_rollback(conn);
		goto senderr;
	}

	if (!vinfo->loaderror) {
		vinfo->free_alloc = 1;
		retval = tl_ioctl(TLTARGIOCDELETEVCARTRIDGE, vinfo);
		if (retval != 0) {
			snprintf(errmsg, sizeof(errmsg), "delete volume failed ioctl\n");
			pgsql_rollback(conn);
			goto senderr;
		}
	}

	pgsql_commit(conn);
	TAILQ_REMOVE(&vdevice->vol_list, vinfo, q_entry);
	vcart_list[vinfo->tape_id] = NULL;
	free(vinfo);
	tl_server_msg_success(comm, msg);
	return 0;
senderr:
	tl_server_msg_failure2(comm, msg, errmsg);
	return -1;
}

void
blkdev_load_volumes(struct tl_blkdevinfo *blkdev)
{
	struct vcartridge *volume;
	struct vcartridge *next;
	int retval;

	volume = TAILQ_FIRST(&blkdev->vol_list);

	while (volume)
	{
		struct vdevice *vdevice;

		TAILQ_REMOVE(&blkdev->vol_list, volume, q_entry);
		next = TAILQ_NEXT(volume, q_entry);
		if (volume->tl_id < 0 || volume->tl_id >= TL_MAX_DEVICES || !device_list[volume->tl_id])
		{
			DEBUG_ERR("VCartridge has invalid tl_id %d\n", volume->tl_id);

			free(volume);
			volume = next;
			continue;
		}

		vdevice = device_list[volume->tl_id];
		retval = tl_ioctl(TLTARGIOCLOADVCARTRIDGE, volume);
		TAILQ_INSERT_TAIL(&vdevice->vol_list, volume, q_entry);
		if (retval != 0) {
			DEBUG_ERR("Loading of volume failed");
			volume->loaderror = 1;
		}
		volume = next;
	}
}

static int
tl_server_process_lists(void)
{
	struct tl_blkdevinfo *blkdev;
	int retval;
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++)
	{
		struct vdevice *vdevice = device_list[i];

		if (!device_list[i])
		{
			continue;
		}

		if (vdevice->type == T_CHANGER)
			retval = load_vtl(vdevice);
		else
			retval = load_drive(vdevice);

		if (retval != 0) {
			DEBUG_ERR("Unable to load back VTL/Drive\n");
			return -1;
		}
	}

	pthread_mutex_lock(&bdev_lock);
	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		if (blkdev->offline)
			continue;
		blkdev_load_volumes(blkdev);
	}
	pthread_mutex_unlock(&bdev_lock);

	return 0;
}

int
get_config_value(char *path, char *name, char *value)
{
	FILE *fp;
	char buf[256];
	char *tmp;

	fp = fopen(path, "r");
	if (!fp)
		return -1;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		tmp = strchr(buf, '#');
		if (tmp)
			*tmp = 0;
		tmp = strchr(buf, '\n');
		if (tmp)
			*tmp = 0;
		tmp = strchr(buf, '=');
		if (!tmp)
			continue;

		*tmp = 0;
		tmp++;

		if (strcasecmp(buf, name) == 0) {
			strcpy(value, tmp);
			break;
		}
	}
	fclose(fp);

	return 0;
}

static void 
check_max_vcart_size(void)
{
	char buf[256];
	int tmp_size;

	buf[0] = 0;
	max_vcart_size = 0;
	get_config_value(QUADSTOR_CONFIG_FILE, "MaxVCartSize", buf);
	if (buf[0]) {
		tmp_size = atoi(buf);
		if (tmp_size >= 1 && tmp_size <= 1600)
			max_vcart_size = tmp_size;
	}
}

int
load_quadstor_conf(void)
{
	char buf[256];

	strcpy(default_group, DEFAULT_GROUP_NAME);

	buf[0] = 0;
	get_config_value(QUADSTOR_CONFIG_FILE, "DefaultPool", buf);
	if (buf[0]) {
		if (group_name_exists(buf)) {
			DEBUG_WARN_SERVER("Default pool name %s already in use\n", buf);
			return 0;
		}
		if (strlen(buf) >= TDISK_MAX_NAME_LEN) {
			DEBUG_WARN_SERVER("Default pool name %s length exceeds maximum %d\n", buf, TDISK_MAX_NAME_LEN - 1);
			return 0;
		}
		strcpy(default_group, buf);
	}
	return 0;
}

int
load_configured_groups(void)
{
	struct group_info *group_info, *group_none;
	struct group_conf group_conf;
	int error = 0, retval, i;

	error = sql_query_groups(group_list);
	if (error != 0) {
		DEBUG_ERR_SERVER("sql_query_groups failed\n");
		return -1;
	}

	load_quadstor_conf();

	group_none = alloc_buffer(sizeof(*group_none));
	if (!group_none) {
		DEBUG_ERR_SERVER("Memory allocation failure\n");
		return -1;
	}

	group_none->group_id = 0;
	strcpy(group_none->name, default_group);
	TAILQ_INIT(&group_none->bdev_list);
	strcpy(group_conf.name, group_none->name);
	group_conf.group_id = group_none->group_id;
	retval = tl_ioctl(TLTARGIOCADDGROUP, &group_conf);
	if (retval != 0)
		error = -1;

	for (i = 1; i < TL_MAX_POOLS; i++) {
		group_info = group_list[i];
		if (!group_info)
			continue;
		DEBUG_BUG_ON(!group_info->group_id);
		strcpy(group_conf.name, group_info->name);
		group_conf.group_id = group_info->group_id;
		group_conf.worm = group_info->worm;
		retval = tl_ioctl(TLTARGIOCADDGROUP, &group_conf);
		if (retval != 0)
			error = -1;
	}

	DEBUG_BUG_ON(group_list[0]);
	group_list[0] = group_none; 
	return error;
}

static inline int
char_to_int(char tmp)
{
        if (tmp >= '0' && tmp <= '9')
                return (tmp - '0');
        else
                return ((tmp - 'a') + 10);
}

static uint64_t 
char_to_wwpn(char *arr) 
{
	int val1, val2;
	int i, j;
	uint8_t wwpn[8];

	if (!strlen(arr))
		return 0ULL;

	for (i = 0, j = 0; i < 24; i+=3, j++) {
		val1 = char_to_int(arr[i]);
		val2 = char_to_int(arr[i+1]);
		wwpn[j] = (val1 << 4) | val2;
	}

	return (uint64_t)wwpn[0] << 56 | (uint64_t)wwpn[1] << 48 | (uint64_t)wwpn[2] << 40 | (uint64_t)wwpn[3] << 32 | (uint64_t)wwpn[4] << 24 | (uint64_t)wwpn[5] << 16 | (uint64_t)wwpn[6] <<  8 | (uint64_t)wwpn[7];
}

void
fc_rule_config_fill(struct fc_rule *fc_rule, struct fc_rule_config *fc_rule_config)
{
	memset(fc_rule_config, 0, sizeof(*fc_rule_config));
	fc_rule_config->target_id = fc_rule->target_id;
	fc_rule_config->wwpn[0] = char_to_wwpn(fc_rule->wwpn);
	fc_rule_config->wwpn[1] = char_to_wwpn(fc_rule->wwpn1);
	fc_rule_config->rule = fc_rule->rule;
}

int
load_fc_rules(void)
{
	int error;
	struct fc_rule *fc_rule;
	struct fc_rule_config fc_rule_config;
	struct vdevice *vdevice;

	error = sql_query_fc_rules(&fc_rule_list);
	if (error != 0)
		return -1;

	TAILQ_FOREACH(fc_rule, &fc_rule_list, q_entry) {
		if (fc_rule->target_id >= 0) {
			vdevice = find_vdevice(fc_rule->target_id, 0);
			if (!vdevice)
				continue;
			strcpy(fc_rule->vtl, vdevice->name);
		}
		fc_rule_config_fill(fc_rule, &fc_rule_config);
		error = tl_ioctl(TLTARGIOCADDFCRULE, &fc_rule_config);
		if (error != 0)
			return error;
	}
	return 0;
}

static int
load_blkdev(struct tl_blkdevinfo *blkdev)
{
	struct bdev_info binfo;
	struct group_info *group_info;
	int error;

	memset(&binfo, 0, sizeof(struct bdev_info));
	binfo.bid = blkdev->bid;
	strcpy(binfo.devpath, blkdev->devname);
	memcpy(binfo.vendor, blkdev->disk.info.vendor, sizeof(binfo.vendor));
	memcpy(binfo.product, blkdev->disk.info.product, sizeof(binfo.product));
	memcpy(binfo.serialnumber, blkdev->disk.info.serialnumber, sizeof(binfo.serialnumber));
	binfo.serial_len = blkdev->disk.info.serial_len;
	binfo.isnew = 0;
	error = gen_rid(binfo.rid);
	if (error != 0)
		return -1;

	error = tl_ioctl(TLTARGIOCNEWBLKDEV, &binfo);
	if (error != 0) {
		DEBUG_ERR_SERVER("Load disk ioctl failed\n");
		return -1;
	}

	group_info = find_group(binfo.group_id);
	if (!group_info)
		return -1;

	bdev_group_insert(group_info, blkdev);

	blkdev->ismaster = binfo.ismaster; 
	if (binfo.ismaster)
		sql_query_volumes(blkdev);

	blkdev->offline = 0;
	return 0;
}

static int
load_configured_disks(void)
{
	struct tl_blkdevinfo *blkdev;
	int error, i;

	pthread_mutex_lock(&bdev_lock);
	error = sql_query_blkdevs(bdev_list);
	if (error != 0)
	{
		DEBUG_ERR("sql_query_blkdevs failed\n");
		goto err;
	}

	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		error = sync_blkdev(blkdev);
		if (error != 0)
		{
			blkdev->offline = 1;
			DEBUG_ERR("sync_blkdev failed\n");
			continue;
		}
		load_blkdev(blkdev);
	}
	pthread_mutex_unlock(&bdev_lock);

	return 0;
err:
	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		bdev_remove(blkdev);
		free(blkdev);
	}
	pthread_mutex_unlock(&bdev_lock);
	return -1;
}

static int
load_configured_devices(void)
{
	int retval;

	retval = sql_query_vdevice(device_list);
	if (retval != 0)
	{
		DEBUG_ERR_SERVER("Query vdevices failed");
		return -1;
	}
	return 0;
}

static int
query_disk_check(void)
{
	struct tl_blkdevinfo *blkdev;
	struct vdevice *vdevice;
	struct vcartridge *vinfo;
	char sqlcmd[64];
	PGconn *conn;
	PGresult *res;
	int nrows, check, error = 0;
	int i;

	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		if (blkdev->offline)
			return 0;
	}

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		vdevice = device_list[i];
		if (!vdevice)
			continue;
		TAILQ_FOREACH(vinfo, &vdevice->vol_list, q_entry) {
			if (vinfo->loaderror)
				return 0;
		}
	}

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT DCHECK FROM SYSINFO");

	res = pgsql_exec_query(sqlcmd, &conn);
	if (!res)
		return 0;

	nrows = PQntuples(res);
	if (!nrows) {
		PQclear(res);
		PQfinish(conn);
		return 0;
	}

	check = atoi(PQgetvalue(res, 0, 0));
	PQclear(res);
	PQfinish(conn);
	if (!check)
		return 0;


	snprintf(sqlcmd, sizeof(sqlcmd), "UPDATE SYSINFO SET DCHECK='0'");
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	if (error != 0) {
		DEBUG_WARN("Failed to reset disk check\n");
		return 0;
	}
	return 1;
}

static int
sys_rid_init(void)
{
	char sqlcmd[128];
	int error = 0, retval;

	retval = gen_rid(sys_rid);
	if (retval != 0)
		return retval;

	snprintf(sqlcmd, sizeof(sqlcmd), "INSERT INTO SYSINFO (SYS_RID) VALUES('%s')", sys_rid);
	pgsql_exec_query2(sqlcmd, 0, &error, NULL, NULL);
	if (error != 0)
	{
		DEBUG_ERR("sys_rid_init: Failed to initialize system rid cmd is %s\n", sqlcmd);
		return -1;
	}
	return 0;
}

static void
sys_rid_strip(void)
{
	int i, c, j;

	bzero(sys_rid_stripped, sizeof(sys_rid_stripped));
	j = 0;
	for (i = 0; i < TL_RID_MAX; i++) {
		c = sys_rid[i];
		if (!c)
			break;
		if (c == '-')
			continue;
		if (c >= '0' && c <= '9') {
			sys_rid_stripped[j++] = c;
			continue;
		}
		sys_rid_stripped[j++] = c - 0x20;
	}
}

static int
sys_rid_load(void)
{
	char sqlcmd[64];
	int nrows;
	int retval;
	PGconn *conn;
	PGresult *res;

	snprintf(sqlcmd, sizeof(sqlcmd), "SELECT SYS_RID FROM SYSINFO");
	res = pgsql_exec_query(sqlcmd, &conn);
	if (res == NULL)
	{
		DEBUG_ERR("sys_rid_load: error occurred in executing sqlcmd %s\n", sqlcmd); 
		return -1;
	}

	nrows = PQntuples(res);
	if (nrows > 1) {
		DEBUG_ERR("sys_rid_load: Invalid nrows %d\n", nrows);
		PQclear(res);
		PQfinish(conn);
		return -1;
	}

	if (nrows == 1)
		strcpy(sys_rid, PQgetvalue(res, 0, 0));
	PQclear(res);
	PQfinish(conn);
	if (!sys_rid[0]) {
		retval = sys_rid_init();
		if (retval != 0)
			return retval;
	}
	sys_rid_strip();
	return 0;
}

int
tl_server_register_pid(void)
{
	int retval;

	mdaemon_info.daemon_pid = getpid();
	retval = tl_ioctl(TLTARGIOCDAEMONSETINFO, &mdaemon_info);

	if (retval != 0)
	{
		DEBUG_ERR("Failed to register our pid\n");
		return -1;
	}
	return 0;
}

static void
vhba_add_device(int vhba_id)
{
#ifdef LINUX
	char cmd[128];
	struct stat stbuf;

	if (vhba_id < 0)
		return;

	if (stat("/proc/scsi/scsi", &stbuf) == 0)
		snprintf(cmd, sizeof(cmd), "echo \"scsi add-single-device %d 0 0 0\" > /proc/scsi/scsi", vhba_id);
	else
		snprintf(cmd, sizeof(cmd), "echo \"0 0 0\" > /sys/class/scsi_host/host%d/scan", vhba_id);
	system(cmd);
#endif
}

static void
vhba_remove_device(struct vdevice *vdevice)
{
#ifdef LINUX
	char cmd[128];
	struct stat stbuf;
#endif

	if (vdevice->vhba_id < 0)
		return;

#ifdef LINUX
	if (stat("/proc/scsi/scsi", &stbuf) == 0)
		snprintf(cmd, sizeof(cmd), "echo \"scsi remove-single-device %d 0 0 0\" > /proc/scsi/scsi", vdevice->vhba_id);
	else
		snprintf(cmd, sizeof(cmd), "echo 1 > /sys/class/scsi_device/%d:0:0:0/device/delete", vdevice->vhba_id);
	system(cmd);
#endif
	vdevice->vhba_id = -1;
}

static int
attach_device(struct vdevice *vdevice)
{
	struct vtlconf *vtlconf;
	struct tdriveconf *dconf;

	ietadm_add_target(vdevice);
	if (vdevice->type != T_CHANGER) {
		vhba_add_device(vdevice->vhba_id);
		return 0;
	}
	vtlconf = (struct vtlconf *)(vdevice);
	TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) {
		vhba_add_device(dconf->vdevice.vhba_id);
		ietadm_add_target(&dconf->vdevice);
	}
	vhba_add_device(vdevice->vhba_id);
	return 0;
}

static int
attach_devices(void)
{
	struct vdevice *vdevice;
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		vdevice = device_list[i];
		if (!vdevice)
			continue;
		attach_device(vdevice);
	}
	return 0;
}

void
tl_server_load_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	pthread_mutex_lock(&daemon_lock);
	if (!done_server_init)
		pthread_cond_wait(&daemon_cond, &daemon_lock);
	pthread_mutex_unlock(&daemon_lock);

	if (done_init) {
		tl_server_msg_success(comm, msg);
		return;
	}

	attach_devices();
	done_init = 1;
	ietadm_qload_done();
	tl_server_msg_success(comm, msg);
}

uint64_t
get_size_spec(int voltype)
{
	if (max_vcart_size)
		return (max_vcart_size * 1024 * 1024 * 1024);

	return get_vol_size_default(voltype);
}

int
vollabel_valid(char *label, int voltype)
{
	int length;

	length = strlen(label);
	switch(voltype)
	{
		case VOL_TYPE_LTO_1:
		case VOL_TYPE_LTO_2:
		case VOL_TYPE_LTO_3:
		case VOL_TYPE_LTO_4:
		case VOL_TYPE_LTO_5:
		case VOL_TYPE_LTO_6:
			return (length == 8);

		case VOL_TYPE_DLT_4:
		case VOL_TYPE_VSTAPE:
			return (length == 6);
		case VOL_TYPE_SDLT_1:
		case VOL_TYPE_SDLT_2:
		case VOL_TYPE_SDLT_3:
			return (length == 7);
	}
	return 0;
}

char *
get_voltag_suffix(int voltype, int worm)
{
	switch(voltype) {
	case VOL_TYPE_LTO_1:
		if (!worm)
			return "L1";
		else
			return "LR";
	case VOL_TYPE_LTO_2:
		if (!worm)
			return "L2";
		else
			return "LS";
	case VOL_TYPE_LTO_3:
		if (!worm)
			return "L3";
		else
			return "LT";
	case VOL_TYPE_LTO_4:
		if (!worm)
			return "L4";
		else
			return "LU";
	case VOL_TYPE_LTO_5:
		if (!worm)
			return "L5";
		else
			return "LV";
	case VOL_TYPE_LTO_6:
		if (!worm)
			return "L6";
		else
			return "LW";
	case VOL_TYPE_DLT_4:
	case VOL_TYPE_VSTAPE:
		return "";
	case VOL_TYPE_SDLT_1:
	case VOL_TYPE_SDLT_2:
	case VOL_TYPE_SDLT_3:
		return "S";
	}
	return "";
}

int
get_label_format(char *label, char *suffix, char *labelformat)
{
	int i;
	char range[8];
	char header[8];
	char trailer[8];
	int end;
	int rangelen;

	if (strlen(label) != 6)
	{
		return -1;
	}

	range[0] = 0;
	header[0] = 0;
	trailer[0] = 0;

	end = -1;
	for (i = 5; i >= 0; i--)
	{
 		if (label[i] < '0' || label[i] > '9')
		{
			if (end > 0)
			{
				strncpy(range, label+i+1, end - i);
				range[end - i] = 0;
				strncpy(header, label, i+1);
				header[i+1] = 0;
				strcpy(trailer, label+end+1);
				break;
 			}
			continue;
		}
		else if (end < 0)
		{
			end = i;
		}
	}

	if (end < 0)
	{
		return -1;
	}

	if (!range[0])
	{
		/* only numbers in the label prefix */
		strcpy(range, label);
	}

	rangelen = strlen(range);
	sprintf(labelformat, "%s%%0%dd%s%s", header, rangelen, trailer, suffix);
	return atoi(range);
}

int
vdevice_add_volumes(struct vdevice *vdevice, struct group_info *group_info, int voltype, int nvolumes, int worm, char *errmsg, char *label)
{
	int i;
	uint64_t size;
	int result = 0;
	char *suffix="";
	char labelfmt[24];
	int start = 0, use_free_slot = 0;;
	char buf[64];

	buf[0] = 0;
	get_config_value(QUADSTOR_CONFIG_FILE, "UseFreeSlot", buf);
	if (atoi(buf) == 1)
		use_free_slot = 1;

	size = get_size_spec(voltype);
	if (!size)
	{
		sprintf(errmsg, "Unable to get a size specification for the VCartridge(s) type");
		return -1;
	}

	if (nvolumes > 1)
	{
		suffix = get_voltag_suffix(voltype, worm);
		start = get_label_format(label, suffix, labelfmt);
		if (start < 0)
		{
			sprintf(errmsg, "Cannot get a numeric range from the specified barcode label prefix");
			return -1;
		}
	}

	for (i = 0; i < nvolumes; i++)
	{
		int retval;
		struct vcartridge *vinfo;
		char vollabel[40];

		memset(vollabel, 0, sizeof(vollabel));

		if (nvolumes == 1)
		{
			strcpy(vollabel, label);
			strcat(vollabel+strlen(label), suffix);
		}
		else
		{
			sprintf(vollabel, labelfmt, start);
			if (!vollabel_valid(vollabel, voltype))
			{
				sprintf(errmsg, "VCartridge label \"%s\" is not valid. Number of VCartridges added are %d", vollabel, i);
				result = -1;
				break;
			}
		}

		retval = sql_virtvol_label_unique(vollabel);
		if (retval != 0)
		{
			sprintf(errmsg, "VCartridge label \"%s\" is not unique. Number of VCartridges added are %d", vollabel, i);
			result = -1;
			break;
		}

		start++;

		vinfo = malloc(sizeof(struct vcartridge));
		if (!vinfo)
		{
			sprintf(errmsg, "Memory allocation failure. Number of VCartridges added are %d", i);
			return -1;
		}

		memset(vinfo, 0, sizeof(struct vcartridge));
		vinfo->tl_id = vdevice_tl_id(vdevice);
		vinfo->type = voltype;
		vinfo->size = size;
		vinfo->group_id = group_info->group_id;
		vinfo->worm = worm || group_info->worm;
		strcpy(vinfo->group_name, group_info->name);
		strcpy(vinfo->label, vollabel);
		vinfo->use_free_slot = use_free_slot;
		vinfo->tape_id = get_next_tape_id();
		if (!vinfo->tape_id) {
			sprintf(errmsg, "Reached maximum possible tape ids. A service restart might help. Number of VCartridges added are %d", i);
			free(vinfo);
			return -1;
		}

		retval = add_new_vcartridge(vinfo, errmsg);

		if (retval != 0) {
			char tmpstr[64];

			sprintf(tmpstr, ". Number of VCartridges added are %d", i);
			strcat(errmsg, tmpstr);

			free(vinfo);
			result = -1;
			break;
		}
		TAILQ_INSERT_TAIL(&vdevice->vol_list, vinfo, q_entry);
		vcart_list[vinfo->tape_id] = vinfo;
	}

	if (result != 0)
	{
		return -1;
	}

	return 0;
}

static int
__tl_server_add_vol_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	char label[50];
	struct group_info *group_info;
	uint32_t group_id;
	int tl_id;
	int nvolumes;
	int voltype;
	int retval;
	struct vdevice *vdevice;
	char errmsg[256];
	int worm;

	if (sscanf(msg->msg_data, "group_id: %u\nlabel: %s\ntl_id: %d\nvoltype: %d\nnvolumes: %d\nworm: %d\n", &group_id, label, &tl_id, &voltype, &nvolumes, &worm) != 6) {
		snprintf(errmsg, sizeof(errmsg), "Invalid Volume configuration msg_data");
		goto senderr;
	}

	if (tl_id < 0 || tl_id >= TL_MAX_DEVICES) {
		snprintf(errmsg, sizeof(errmsg), "Invalid vtl device specified");
		goto senderr;
	}

	group_info = find_group(group_id);
	if (!group_info) {
		snprintf(errmsg, sizeof(errmsg), "Cannot find pool with id %u\n", group_id);
		goto senderr;
	}

	vdevice = device_list[tl_id];
	if (!vdevice) {
		snprintf(errmsg, sizeof(errmsg), "Invalid vtl device specified");
		goto senderr;
	}

	check_max_vcart_size();
	retval = vdevice_add_volumes(vdevice, group_info, voltype, nvolumes, worm, errmsg, label);
	if (retval != 0)
		goto senderr;

	tl_server_msg_success(comm, msg);
	return 0;
senderr:
	tl_server_msg_failure2(comm, msg, errmsg);
	return -1;
}

int
tl_server_add_vol_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	int retval;

	pthread_mutex_lock(&device_lock);
	retval = __tl_server_add_vol_conf(comm, msg);
	pthread_mutex_unlock(&device_lock);
	return retval;
}

struct vcartridge *
find_volume(int tl_id, uint32_t tape_id)
{
	struct vdevice *vdevice;
	struct vcartridge *vinfo;

	if (tl_id < 0 || tl_id >= TL_MAX_DEVICES)
	{
		return NULL;
	}

	vdevice = device_list[tl_id];
	if (!vdevice)
	{
		return NULL;
	}

	TAILQ_FOREACH(vinfo, &vdevice->vol_list, q_entry) { 
		if (vinfo->tape_id == tape_id)
			return vinfo;
	}
	return NULL;
}

static int
tl_server_vtl_vol_info(struct tl_comm *comm, struct tl_msg *msg)
{
	char filename[256];
	FILE *fp = NULL;	
	struct vcartridge *volume;
	int retval;
	int tl_id;
	struct vdevice *vdevice;

	retval = sscanf(msg->msg_data, "tempfile:%s\ntl_id: %d\n", filename, &tl_id);
	if (retval != 2)
	{
		DEBUG_ERR("Invalid vtl_vol_info message\n");
		tl_server_msg_invalid(comm, msg);
		return -1;
	}

	if (tl_id  < 0 || tl_id >= TL_MAX_DEVICES || !device_list[tl_id])
	{
		DEBUG_ERR("Invalid tl_id %d\n", tl_id);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vdevice = device_list[tl_id];

	fp = fopen(filename, "w");

	if (!fp)
	{
		tl_server_msg_invalid(comm, msg);
		return -1;
	}

	TAILQ_FOREACH(volume, &vdevice->vol_list, q_entry) {
		/* Updates the used percentage of the volume */
		if (!volume->loaderror)
		{
			tl_ioctl(TLTARGIOCGETVCARTRIDGEINFO, volume);
		}
		dump_volume(fp, volume);
	}

	fclose(fp);
	tl_server_msg_success(comm, msg);
	return 0;
}

int
vdevice_get_tlid(void)
{
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		if (!device_list[i])
			return i;
	}
	return -1;
}

int 
vtl_name_exists(char *name)
{
	struct vdevice *vdevice;
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		vdevice = device_list[i];

		if (!vdevice)
			continue;

		if (strcasecmp(vdevice->name, name) == 0)
			return 1;
	}
	return 0;
}

static void
delete_volumes(struct vdevice *vdevice)
{
	struct vcartridge *vinfo;

	while ((vinfo = TAILQ_FIRST(&vdevice->vol_list))) {
		TAILQ_REMOVE(&vdevice->vol_list, vinfo, q_entry);
		vcart_list[vinfo->tape_id] = NULL;
		free(vinfo);
	}
}

static int
delete_vtl(struct vdevice *vdevice, int free_alloc)
{
	struct vtlconf *vtlconf = (struct vtlconf *)vdevice;
	struct vdeviceinfo dinfo;
	int retval;
	struct tdriveconf *dconf;

	TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) { 
		vhba_remove_device(&dconf->vdevice);
		if (!dconf->vdevice.iscsi_attached)
			continue;
		retval = ietadm_delete_target(dconf->vdevice.iscsi_tid);
		if (retval != 0) {
			DEBUG_ERR("Unable to delete iscsi target at id %d name %s\n", dconf->vdevice.iscsi_tid, dconf->vdevice.name);
			return -1;
		}
	}

	vhba_remove_device(vdevice);
	if (vdevice->iscsi_attached) {
		retval = ietadm_delete_target(vdevice->iscsi_tid);
		if (retval != 0) {
			DEBUG_ERR("Unable to delete iscsi target at id %d\n", vdevice->iscsi_tid);
			return -1;
		}
	}

	memset(&dinfo, 0, sizeof(struct vdeviceinfo));
	dinfo.tl_id = vdevice->tl_id;
	dinfo.iscsi_tid = vdevice->iscsi_tid;
	dinfo.vhba_id = vdevice->vhba_id;
	dinfo.free_alloc = free_alloc;
	retval = tl_ioctl(TLTARGIOCDELETEDEVICE, &dinfo);
	if (retval != 0) {
		DEBUG_ERR("Delete device ioctl failed for tl_id %d\n", vdevice->tl_id);
		return -1;
	}

	if (free_alloc)
		retval = sql_delete_vtl(vdevice->tl_id);
	return retval;
}

static int
delete_drive(struct vdevice *vdevice, int free_alloc)
{
	struct vdeviceinfo dinfo;
	int retval;

	vhba_remove_device(vdevice);
	if (vdevice->iscsi_attached) {
		retval = ietadm_delete_target(vdevice->iscsi_tid);
		if (retval != 0) {
			DEBUG_ERR("Unable to delete iscsi target at id %d\n", vdevice->iscsi_tid);
			return -1;
		}
	}

	memset(&dinfo, 0, sizeof(struct vdeviceinfo));
	dinfo.tl_id = vdevice->tl_id;
	dinfo.iscsi_tid = vdevice->iscsi_tid;
	dinfo.vhba_id = vdevice->vhba_id;
	dinfo.free_alloc = free_alloc;
	retval = tl_ioctl(TLTARGIOCDELETEDEVICE, &dinfo);
	if (retval != 0)
	{
		DEBUG_ERR("Error in deleting drive at tl_id %d\n", vdevice->tl_id);
		return -1;
	}

	if (free_alloc)
		retval = sql_delete_vtl(vdevice->tl_id);
	return retval;
}

static int
delete_vdevice(struct vdevice *vdevice, int free_alloc)
{
	if (vdevice->type == T_CHANGER)
	{
		return delete_vtl(vdevice, free_alloc);
	}
	else
	{
		return delete_drive(vdevice, free_alloc);
	}
	return 0;
}

static int
load_vtl(struct vdevice *vdevice)
{
	struct vtlconf *vtlconf = (struct vtlconf *)vdevice;
	struct vdeviceinfo dinfo;
	struct tdriveconf *dconf;
	int retval;

	memset(&dinfo, 0, sizeof(struct vdeviceinfo));

	strcpy(dinfo.name, vdevice->name);
	dinfo.type = T_CHANGER;
	dinfo.make = vtlconf->type;
	dinfo.tl_id = vdevice->tl_id;
	dinfo.iscsi_tid = -1;
	dinfo.vhba_id = -1;
	dinfo.slots = vtlconf->slots;
	dinfo.ieports = vtlconf->ieports;
	strcpy(dinfo.serialnumber, vdevice->serialnumber);
	strcpy(dinfo.sys_rid, sys_rid_stripped);

	retval = tl_ioctl(TLTARGIOCNEWDEVICE, &dinfo);
	if (retval != 0) {
		DEBUG_ERR("Medium Changer addition failed");
		return -1;
	}
	vdevice->iscsi_tid = dinfo.iscsi_tid;
	vdevice->vhba_id = dinfo.vhba_id;

	TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) { 
		struct vdevice *drive_vdevice = (struct vdevice *)dconf;

		memset(&dinfo, 0, sizeof(struct vdeviceinfo));
		strcpy(dinfo.name, drive_vdevice->name);
		dinfo.type = T_SEQUENTIAL;
		dinfo.make = dconf->type;
		dinfo.tl_id = vdevice->tl_id;
		dinfo.target_id = drive_vdevice->target_id;
		dinfo.iscsi_tid = -1;
		dinfo.vhba_id = -1;
		dinfo.enable_compression = enable_drive_compression;
		strcpy(dinfo.serialnumber, drive_vdevice->serialnumber);
		strcpy(dinfo.sys_rid, sys_rid_stripped);
		retval = tl_ioctl(TLTARGIOCNEWDEVICE, &dinfo);
		if (retval != 0) {
			return -1;
		}
		drive_vdevice->iscsi_tid = dinfo.iscsi_tid;
		drive_vdevice->vhba_id = dinfo.vhba_id;
	}

	return 0;
}

static int
vtl_add_drive(struct vtlconf *vtlconf, int drivetype, int target_id, char *errmsg)
{ 
	struct vdeviceinfo deviceinfo;
	struct tdriveconf *driveconf;
	char dname[40], serialnumber[40];
	int retval;

	snprintf(dname, sizeof(dname), "drive%d", target_id);
	serialnumber[0] = 0;
	driveconf = tdriveconf_new(vtlconf->vdevice.tl_id, target_id, dname, serialnumber);
	if (!driveconf)
		return -1;

	memset(&deviceinfo, 0, sizeof(struct vdeviceinfo));
	deviceinfo.type = T_SEQUENTIAL;
	deviceinfo.make = drivetype;
	deviceinfo.tl_id = vtlconf->vdevice.tl_id;
	deviceinfo.target_id = target_id;
	deviceinfo.iscsi_tid = -1;
	deviceinfo.vhba_id = -1;
	deviceinfo.enable_compression = enable_drive_compression;
	strcpy(deviceinfo.name, dname);
	strcpy(deviceinfo.serialnumber, serialnumber);
	strcpy(deviceinfo.sys_rid, sys_rid_stripped);

	retval = tl_ioctl(TLTARGIOCNEWDEVICE, &deviceinfo);
	if (retval != 0) {
		sprintf(errmsg, "VTL drive addition failed");
		return -1;
	}
	
	strcpy(driveconf->vdevice.serialnumber, deviceinfo.serialnumber);
	driveconf->vdevice.iscsi_tid = deviceinfo.iscsi_tid;
	driveconf->vdevice.vhba_id = deviceinfo.vhba_id;
	driveconf->type = drivetype;

	retval = sql_add_vtl_drive(vtlconf->vdevice.tl_id, driveconf);
	if (retval != 0) {
		DEBUG_ERR("sql_add_vtl_drive failed\n");
		return -1;
	}

	retval = ietadm_default_settings(&driveconf->vdevice, &vtlconf->vdevice);
	if (retval != 0) {
		return -1;
	}
	TAILQ_INSERT_TAIL(&vtlconf->drive_list, driveconf, q_entry); 
	return 0;
}

struct vtlconf * 
add_new_vtl(char *name, int vtltype, int slots, int ieports, char *errmsg)
{
	int retval, tl_id;
	struct vdeviceinfo dinfo;
	struct vdevice *vdevice;
	struct vtlconf *vtlconf;
	char serialnumber[40];

	tl_id = vdevice_get_tlid();
	if (tl_id < 0) {
		sprintf(errmsg, "Maximum VTL/Virtual Drives already configured\n");
		return NULL;		
	}

	retval = vtl_name_exists(name);
	if (retval) {
		sprintf(errmsg, "A VTL with name %s already exists", name);
		return NULL;
	}

	serialnumber[0] = 0;
	vtlconf = vtlconf_new(tl_id, name, serialnumber);
	if (!vtlconf) {
		sprintf(errmsg, "Memory allocation failure\n");
		return NULL;
	}
	vdevice = (struct vdevice *)vtlconf;

	memset(&dinfo, 0, sizeof(struct vdeviceinfo));
	strcpy(dinfo.name, name);
	strcpy(dinfo.serialnumber, serialnumber);
	dinfo.type = T_CHANGER;
	dinfo.make = vtltype;
	dinfo.tl_id = tl_id;
	dinfo.iscsi_tid = -1;
	dinfo.vhba_id = -1;
	dinfo.slots = slots;
	dinfo.ieports = ieports;
	strcpy(dinfo.sys_rid, sys_rid_stripped);

	retval = tl_ioctl(TLTARGIOCNEWDEVICE, &dinfo);
	if (retval != 0) {
		sprintf(errmsg, "Medium Changer addition failed");
		free(vtlconf);
		return NULL;
	}

	strcpy(vdevice->serialnumber, dinfo.serialnumber);
	vdevice->iscsi_tid = dinfo.iscsi_tid;
	vdevice->vhba_id = dinfo.vhba_id;
	vtlconf->drives = 0;
	vtlconf->slots = slots;
	vtlconf->ieports = ieports;
	vtlconf->type = vtltype;

	retval = sql_add_vtl(vtlconf);
	if (retval != 0) {
		DEBUG_ERR("sql_add_vtl failed\n");
		goto err;
	}

	retval  = ietadm_default_settings(vdevice, NULL);
	if (retval != 0) {
		goto err;
	}

	return vtlconf;
err:
	delete_vdevice(vdevice, 1);
	free(vtlconf);
	sprintf(errmsg, "Failed to add new VTL");
	return NULL;
}

static int
load_drive(struct vdevice *vdevice)
{
	struct tdriveconf *dconf = (struct tdriveconf *)vdevice;
	int retval;
	struct vdeviceinfo dinfo;

	memset(&dinfo, 0, sizeof(struct vdeviceinfo));
	dinfo.type = T_SEQUENTIAL;
	strcpy(dinfo.name, vdevice->name);

	dinfo.make = dconf->type;
	dinfo.tl_id = vdevice->tl_id;
	dinfo.target_id = vdevice->target_id;
	dinfo.enable_compression = enable_drive_compression;
	strcpy(dinfo.serialnumber, vdevice->serialnumber);
	strcpy(dinfo.sys_rid, sys_rid_stripped);

	retval = tl_ioctl(TLTARGIOCNEWDEVICE, &dinfo);
	if (retval != 0) {
		DEBUG_ERR("Virtual Tape Device load failed");
		return -1;
	}

	vdevice->iscsi_tid = dinfo.iscsi_tid;
	vdevice->vhba_id = dinfo.vhba_id;
	return 0;
}

static struct tdriveconf *
add_new_drive(char *name, int drivetype, char *errmsg)
{
	struct vdeviceinfo deviceinfo;
	struct tdriveconf *driveconf;
	struct vdevice *vdevice;
	int retval, tl_id;
	char serialnumber[40];
	int voltype;

	voltype = get_voltype(drivetype);
	if (voltype < 0) {
		sprintf(errmsg, "Drivetype not supported");
		return NULL;
	}

	tl_id = vdevice_get_tlid();
	if (tl_id < 0) {
		sprintf(errmsg, "Maximum VTL/Virtual Drives already configured\n");
		return NULL;
	}

	retval = vtl_name_exists(name);
	if (retval) {
		sprintf(errmsg, "A VTL with name %s already exists", name);
		return NULL;
	}

	serialnumber[0] = 0;
	driveconf = tdriveconf_new(tl_id, 0, name, serialnumber);
	if (!driveconf) {
		sprintf(errmsg, "Memory allocation failure");
		return NULL;
	}
	vdevice = (struct vdevice *)driveconf;
	driveconf->type = drivetype;

	memset(&deviceinfo, 0, sizeof(deviceinfo));
	deviceinfo.type = T_SEQUENTIAL;
	deviceinfo.make = drivetype;
	deviceinfo.tl_id = tl_id; 
	deviceinfo.iscsi_tid = -1;
	deviceinfo.vhba_id = -1;
	deviceinfo.enable_compression = enable_drive_compression;
	strcpy(deviceinfo.name, name);
	strcpy(deviceinfo.serialnumber, serialnumber);
	strcpy(deviceinfo.sys_rid, sys_rid_stripped);

	retval = tl_ioctl(TLTARGIOCNEWDEVICE, &deviceinfo);
	if (retval != 0) {
		free(driveconf);
		sprintf(errmsg, "Virtual Tape Device addition failed");
		return NULL;
	}

	strcpy(vdevice->serialnumber, deviceinfo.serialnumber);
	vdevice->iscsi_tid = deviceinfo.iscsi_tid;
	vdevice->vhba_id = deviceinfo.vhba_id;
	retval = sql_add_drive(driveconf);
	if (retval != 0) {
		sprintf(errmsg, "Failed to add drive information in DB");
		goto err;
	}

	retval = ietadm_default_settings(vdevice, NULL);
	if (retval != 0) {
		sprintf(errmsg, "Failed to configure iSCSI settings");
		goto err;
	}

	attach_device(vdevice);
	device_list[tl_id] = vdevice;
	return driveconf;

err:
	delete_vdevice(vdevice, 1);
	free(driveconf);
	return NULL;
}

struct tdriveconf *
find_driveconf(int tl_id, uint32_t target_id)
{
	struct vdevice *vdevice;
	struct tdriveconf *dconf;

	if (tl_id < 0 || tl_id >= TL_MAX_DEVICES || !device_list[tl_id])
	{
		return NULL;
	}

	vdevice = device_list[tl_id];

	if (target_id && vdevice->type == T_SEQUENTIAL)
	{
		return NULL;
	}

	if (!target_id && vdevice->type == T_CHANGER)
	{
		return NULL;
	}

	if (vdevice->type == T_CHANGER) {
		struct vtlconf *vtlconf = (struct vtlconf *)vdevice;

		TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) { 
			if (dconf->vdevice.target_id == target_id)
				return dconf;
		} 
		return NULL;
	}
	else
		return (struct tdriveconf *)vdevice;
}

static int
tl_server_add_drive_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	struct tdriveconf *driveconf;
	char name[50];
	int drivetype;
	char errmsg[256];

	if (sscanf(msg->msg_data, "name: %s\ndrivetype: %d\n", name, &drivetype) != 2) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	driveconf = add_new_drive(name, drivetype, errmsg);
	if (!driveconf) {
		tl_server_msg_failure2(comm, msg, errmsg);
		return -1;
	}

	msg->msg_resp = MSG_RESP_OK;
	snprintf(errmsg, sizeof(errmsg), "tl_id: %d\n", driveconf->vdevice.tl_id);
	tl_server_send_message(comm, msg, errmsg);
	return 0;
}

static int 
__tl_server_add_vtl_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	char tempfile[100];
	FILE *fp;
	char buf[100];
	struct vtlconf *vtlconf = NULL;
	char name[50];
	int vtltype;
	int slots;
	int retval;
	char errmsg[256];
	struct vdevice *vdevice = NULL;
	int target_id = 1;

	if (sscanf(msg->msg_data, "tempfile: %s\n", tempfile) != 1) {
		DEBUG_ERR("Invalid message msg_data %s\n", msg->msg_data);
		tl_server_msg_failure2(comm, msg, "Invalid message msg_data");
		return -1;
	}

	fp = fopen(tempfile, "r");
	if (!fp)
	{
		DEBUG_ERR("Unable to open tmp file %s\n", tempfile);
		tl_server_msg_failure(comm, msg);
		return -1;
	}	

	fgets(buf, sizeof(buf), fp);

	if (strcmp(buf, "<vtlconf>\n") != 0)
	{
		goto err;
	}

	if (fscanf(fp, "name: %s\n", name) != 1)
	{
		goto err;
	}

	if (fscanf(fp, "slots: %d\n", &slots) != 1)
	{
		goto err;
	}

	if (fscanf(fp, "type: %d\n", &vtltype) != 1)
	{
		goto err;
	}

	vtlconf = add_new_vtl(name, vtltype, slots, DEFAULT_IE_PORTS, errmsg);
	if (!vtlconf) {
		DEBUG_ERR("Adding new vtl failed\n");
		fclose(fp);
		tl_server_msg_failure2(comm, msg, errmsg);
		return -1;
	}

	vdevice = (struct vdevice *)vtlconf;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (strcmp(buf, "<drive>\n") == 0)
		{
			int drivetype;
			char dname[50];

			if (fscanf(fp, "name: %s\n", dname) != 1)
			{
				DEBUG_ERR("Error parsing drive name\n");
				goto err;
			}
			if (fscanf(fp, "type: %d\n", &drivetype) != 1)
			{
				DEBUG_ERR("Error parsing drive type\n");
				goto err;
			}

			retval = vtl_add_drive(vtlconf, drivetype, target_id, errmsg);
			if (retval != 0)
			{
				DEBUG_ERR("Error in adding vdrives of drivetype 0x%x count 1 errmsg is %s\n", drivetype, errmsg);
				goto err;
			}
			target_id++;
		}
	} 
	fclose(fp);

	msg->msg_resp = MSG_RESP_OK;

	attach_device(vdevice);
	device_list[vdevice->tl_id] = vdevice;
	snprintf(errmsg, sizeof(errmsg), "tl_id: %d\n", vdevice->tl_id);
	tl_server_send_message(comm, msg, errmsg);
	return 0;
err:
	if (vdevice)
	{
		delete_vdevice(vdevice, 1);
		free_vdevice(vdevice);
	}

	fclose(fp);
	tl_server_msg_failure2(comm, msg, "Failed to add new VTL");
	return -1;
}

static int
tl_server_add_vtl_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	int retval;

	pthread_mutex_lock(&device_lock);
	retval = __tl_server_add_vtl_conf(comm, msg);
	pthread_mutex_unlock(&device_lock);
	return retval;
}

static int
tl_server_list_disks(struct tl_comm *comm, struct tl_msg *msg)
{
	char filepath[256];
	FILE *fp;
	struct physdisk *disk;

	if (sscanf(msg->msg_data, "tempfile: %s\n", filepath) != 1)
	{
		DEBUG_ERR("Parsing file path failed");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	fp = fopen(filepath, "w");
	if (!fp)
	{
		DEBUG_ERR("Opening file path %s failed!\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	TAILQ_FOREACH(disk, &disk_list, q_entry) {
		fprintf(fp, "<disk>\n");
		dump_disk(fp, disk, 0);
		fprintf(fp, "</disk>\n");
	}
	fclose(fp);

	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tl_server_vtl_info(struct tl_comm *comm, struct tl_msg *msg)
{
	struct vdevice *vdevice;
	char filepath[256];
	FILE *fp;
	uint32_t tl_id;

	if (sscanf(msg->msg_data, "tempfile: %s\ntl_id: %u\n", filepath, &tl_id) != 2)
	{
		DEBUG_ERR("Parsing file path failed");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	if (tl_id >= TL_MAX_DEVICES)
	{
		DEBUG_ERR("Invalid tl_id passed for get_vtl_conf %u\n", tl_id);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	fp = fopen(filepath, "w");
	if (!fp)
	{
		DEBUG_ERR("Opening file path %s failed!\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vdevice = device_list[tl_id];

	if (!vdevice || vdevice->type != T_CHANGER)
	{
		fclose(fp);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	dump_vdevice(fp, vdevice, 1);
	fclose(fp);
	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tdrive_get_info(struct vtlconf *vtlconf, struct tdriveconf *driveconf)
{
	struct vdeviceinfo dinfo;
	int retval;

	driveconf->tape_label[0] = 0;

	memset(&dinfo, 0, sizeof(struct vdeviceinfo));
	if (vtlconf)
	{
		dinfo.tl_id = vtlconf->vdevice.tl_id;
		dinfo.target_id = driveconf->vdevice.target_id;
	}
	else
	{
		dinfo.tl_id = driveconf->vdevice.tl_id;
	}

	retval = tl_ioctl(TLTARGIOCGETDEVICEINFO, &dinfo);
	strcpy(driveconf->tape_label, "none");
	if (retval == 0)
	{
		if (dinfo.tape_label[0])
		{
			memcpy(driveconf->tape_label, dinfo.tape_label, sizeof(dinfo.tape_label));
		}
	}
	return retval;
}

static int
tl_server_vtl_drive_info(struct tl_comm *comm, struct tl_msg *msg)
{
	struct vdevice *vdevice;
	char filepath[256];
	FILE *fp;
	uint32_t tl_id;
	struct vtlconf *vtlconf;
	struct tdriveconf *dconf;

	if (sscanf(msg->msg_data, "tempfile: %s\ntl_id: %u\n", filepath, &tl_id) != 2)
	{
		DEBUG_ERR("Parsing file path failed");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	if (tl_id >= TL_MAX_DEVICES)
	{
		DEBUG_ERR("Invalid tl_id passed for get_vtl_conf %u\n", tl_id);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	fp = fopen(filepath, "w");
	if (!fp)
	{
		DEBUG_ERR("Opening file path %s failed!\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vdevice = device_list[tl_id];

	if (!vdevice || vdevice->type != T_CHANGER)
	{
		fclose(fp);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vtlconf = (struct vtlconf *)(vdevice);
	TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) { 
		tdrive_get_info(vtlconf, dconf);
	}

	dump_vdevice(fp, vdevice, 1);
	fclose(fp);
	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tl_server_get_vtl_conf(struct tl_comm *comm, struct tl_msg *msg)
{
	struct vdevice *vdevice;
	char filepath[256];
	FILE *fp;
	uint32_t tl_id;
	int retval;
	struct vdeviceinfo dinfo; 

	if (sscanf(msg->msg_data, "tempfile: %s\ntl_id: %u\n", filepath, &tl_id) != 2)
	{
		DEBUG_ERR("Parsing file path failed");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	if (tl_id >= TL_MAX_DEVICES)
	{
		DEBUG_ERR("Invalid tl_id passed for get_vtl_conf %u\n", tl_id);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	fp = fopen(filepath, "w");
	if (!fp)
	{
		DEBUG_ERR("Opening file path %s failed!\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vdevice = device_list[tl_id];

	if (!vdevice)
	{
		fclose(fp);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	if (vdevice->type == T_CHANGER)
	{
		struct vtlconf *vtlconf = (struct vtlconf *)(vdevice);
		struct tdriveconf *dconf;

		memset(&dinfo, 0, sizeof(struct vdeviceinfo));
		dinfo.tl_id = vtlconf->vdevice.tl_id;
		retval = tl_ioctl(TLTARGIOCGETDEVICEINFO, &dinfo);
		if (retval != 0)
		{
			fclose(fp);
			tl_server_msg_failure(comm, msg);
			return -1;
		}

		vtlconf->slots = dinfo.slots;
		vtlconf->drives = dinfo.drives;
		vtlconf->ieports = dinfo.ieports;

		TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) {
			tdrive_get_info(vtlconf, dconf);
		}
	}
	else
	{
		struct tdriveconf *dconf = (struct tdriveconf *)(vdevice);
		tdrive_get_info(NULL, dconf);
	}

	dump_vdevice(fp, vdevice, 1);
	fclose(fp);
	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tl_server_get_vtl_list(struct tl_comm *comm, struct tl_msg *msg)
{
	char filepath[256];
	FILE *fp;
	int i;

	if (sscanf(msg->msg_data, "tempfile: %s\n", filepath) != 1)
	{
		DEBUG_ERR("Parsing file path failed");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	fp = fopen(filepath, "w");
	if (!fp)
	{
		DEBUG_ERR("Opening file path %s failed!\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	for (i = 0; i < TL_MAX_DEVICES; i++)
	{
		struct vdevice *vdevice = device_list[i];

		if (!vdevice)
			continue;
		dump_vdevice(fp, vdevice, 0);
	}
	fclose(fp);
	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tl_server_get_configured_disks(struct tl_comm *comm, struct tl_msg *msg)
{
	char filepath[256];
	FILE *fp;
	struct tl_blkdevinfo *blkdev;
	int i;

	if (sscanf(msg->msg_data, "tempfile: %s\n", filepath) != 1)
	{
		DEBUG_ERR("Parsing file path failed");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	fp = fopen(filepath, "w");
	if (!fp)
	{
		DEBUG_ERR("Opening file path %s failed!\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	pthread_mutex_lock(&bdev_lock);
	for (i = 1; i < TL_MAX_DISKS; i++) {
		struct bdev_info binfo;

		blkdev = bdev_list[i];
		if (!blkdev)
			continue;

		fprintf(fp, "<disk>\n");
		memset(&binfo, 0, sizeof(struct bdev_info));
		binfo.bid = blkdev->bid;

		if (!blkdev->offline)
		{
			tl_ioctl(TLTARGIOCGETBLKDEV, &binfo);
			blkdev->disk.size = binfo.size;
			blkdev->disk.used = (binfo.size - binfo.free);
			strcpy(blkdev->disk.group_name, blkdev->group->name);
			blkdev->disk.info.online = 1;
		}
		else {
			blkdev->disk.info.online = 0;
		}

		dump_disk(fp, &blkdev->disk, blkdev->bid);
		fprintf(fp, "</disk>\n");
	}
	pthread_mutex_unlock(&bdev_lock);

	fclose(fp);

	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tl_server_delete_disk(struct tl_comm *comm, struct tl_msg *msg)
{
	uint64_t usize;
	struct tl_blkdevinfo *blkdev = NULL, *tmp;
	struct bdev_info binfo;
	int retval, i;
	char errmsg[256];
	char dev[512];

	if (sscanf(msg->msg_data, "dev: %[^\n]", dev) != 1) {
		DEBUG_ERR("Parsing vtl conf file path failed. Invalid msg_data is %s\n", msg->msg_data);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	pthread_mutex_lock(&bdev_lock);
	for (i = 1; i < TL_MAX_DISKS; i++) {
		tmp = bdev_list[i];
		if (!tmp)
			continue;
		if (strcmp(tmp->disk.info.devname, dev))
			continue;
		blkdev = tmp;
		break;
	}
	pthread_mutex_unlock(&bdev_lock);

	if (!blkdev) {
		snprintf(errmsg, sizeof(errmsg), "Unable to find disk at %s for deletion\n", dev);
		goto senderr;
	}

	memset(&binfo, 0, sizeof(struct bdev_info));
	binfo.bid = blkdev->bid;
	retval = tl_ioctl(TLTARGIOCGETBLKDEV, &binfo);
	if (retval < 0) {
		snprintf(errmsg, sizeof(errmsg), "Error in removing disk\n");
		goto senderr;
	}

	usize = binfo.usize;
	if (binfo.free != (usize - BINT_RESERVED_SIZE)) {
		snprintf(errmsg, sizeof(errmsg), "Cannot delete disk which has active virtual volumes\n");
		goto senderr;
	}

	memset(&binfo, 0, sizeof(struct bdev_info));
	binfo.bid = blkdev->bid;
	retval = tl_ioctl(TLTARGIOCDELBLKDEV, &binfo);
	if (retval != 0) {
		if (binfo.errmsg[0])
			strcpy(errmsg, binfo.errmsg);
		else
			snprintf(errmsg, sizeof(errmsg), "Unable to delete disk from kernel\n");
		goto senderr;
	}

	DEBUG_BUG_ON(!blkdev->bid);
	retval = sql_delete_blkdev(blkdev);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Unable to delete disk from database\n");
		goto senderr;
	}

	bdev_remove(blkdev);
	free(blkdev);
	msg->msg_resp = MSG_RESP_OK;
	tl_server_msg_success(comm, msg);
	return 0;
senderr:
	tl_server_msg_failure2(comm, msg, errmsg);
	return -1;
}

static int
is_quadstor_vdisk(uint8_t *vendor, uint8_t *product)
{
	if (strncmp((char *)vendor, "QUADSTOR", strlen("QUADSTOR")))
		return 0;
	else
		return 1;
}

static int
group_has_disks(struct group_info *group_info)
{
	struct tl_blkdevinfo *blkdev;
	int i;

	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		if (blkdev->offline && blkdev->db_group_id == group_info->group_id)
			return 1;
		if (!blkdev->offline && blkdev->group_id == group_info->group_id)
			return 1;
	}
	return 0;
}

static int
tl_server_add_disk(struct tl_comm *comm, struct tl_msg *msg)
{
	struct group_info *group_info;
	uint32_t group_id;
	struct physdisk *disk;
	int retval;
	struct tl_blkdevinfo *blkdev, *master;
	char errmsg[256];
	struct bdev_info binfo;
	PGconn *conn;
	char dev[512];

	if (sscanf(msg->msg_data, "group_id: %u\ndev: %[^\n]", &group_id, dev) != 2) {
		tl_server_msg_failure2(comm, msg, "Invalid msg msg_data");
		return -1;
	}

	group_info = find_group(group_id);
	if (!group_info) {
		snprintf(errmsg, sizeof(errmsg), "Cannot find pool with id %u\n", group_id);
		goto senderr;
	}

	master = group_get_master(group_info);
	if (master && master->offline) {
		snprintf(errmsg, sizeof(errmsg), "Cannot add disk when pool %s master disk is offline\n", group_info->name);
		goto senderr;
	}

	if (!master && group_has_disks(group_info)) {
		snprintf(errmsg, sizeof(errmsg), "Cannot add disk when pool %s master disk is offline\n", group_info->name);
		goto senderr;
	}

	disk = tl_common_find_disk(dev);
	if (!disk) {
		snprintf(errmsg, sizeof(errmsg), "Unable to find disk at %s for addition\n", dev);
		goto senderr;
	}

	retval = is_ignore_dev(disk->info.devname);
	if (retval) {
		snprintf(errmsg, sizeof(errmsg), "Cannot add a disk with mounted partitions dev is %s\n", disk->info.devname);
		goto senderr;
	}

	retval = dev_used_by_virt(disk->info.devname);
	if (retval) {
		snprintf(errmsg, sizeof(errmsg), "device %s seems to be used by quadstor virtualization software\n", disk->info.devname);
		goto senderr;
	}

	retval = check_blkdev_exists(disk->info.devname);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Disk at devpath %s already added\n", disk->info.devname);
		goto senderr;
	}

	blkdev = blkdev_new(disk->info.devname);
	if (!blkdev) {
		snprintf(errmsg, sizeof(errmsg), "Memory Allocation failure\n");
		goto senderr;
	}

	memcpy(&blkdev->disk, disk, offsetof(struct physdisk, q_entry));
	strcpy(blkdev->devname, disk->info.devname);

	conn = sql_add_blkdev(&blkdev->disk, blkdev->bid, group_id);
	if (!conn) {
		snprintf(errmsg, sizeof(errmsg), "Adding disk to database failed\n");
		free(blkdev);
		goto senderr;
	}

	memset(&binfo, 0, sizeof(struct bdev_info));
	binfo.bid = blkdev->bid;
	binfo.group_id = group_id;
	strcpy(binfo.devpath, blkdev->devname);
	memcpy(binfo.vendor, blkdev->disk.info.vendor, sizeof(binfo.vendor));
	memcpy(binfo.product, blkdev->disk.info.product, sizeof(binfo.product));
	memcpy(binfo.serialnumber, blkdev->disk.info.serialnumber, sizeof(binfo.serialnumber));
	binfo.serial_len = blkdev->disk.info.serial_len;
	binfo.isnew = 1;
	binfo.unmap = is_quadstor_vdisk(binfo.vendor, binfo.product);

	retval = tl_ioctl(TLTARGIOCNEWBLKDEV, &binfo);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Error adding new disk, ioctl failed\n");
		goto err;
	}

	blkdev->ismaster = binfo.ismaster;
	bdev_add(group_info, blkdev);
	msg->msg_resp = MSG_RESP_OK;
	tl_server_msg_success(comm, msg);

	retval = pgsql_commit(conn);
	if (retval != 0)
		tl_ioctl(TLTARGIOCDELBLKDEV, &binfo);

	return retval;

err:
	pgsql_rollback(conn);
	free(blkdev);
senderr:
	tl_server_msg_failure2(comm, msg, errmsg);
	return -1;
}

static int
tl_server_reload_export(struct tl_comm *comm, struct tl_msg *msg)
{
	struct vcartridge *vinfo;
	uint32_t tape_id;
	int tl_id, retval;

	if (sscanf(msg->msg_data, "tl_id: %d\ntape_id: %u\n", &tl_id, &tape_id) != 2) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vinfo = find_volume(tl_id, tape_id);
	if (!vinfo) {
		tl_server_msg_invalid(comm, msg);
		return -1;
	}

	retval = tl_ioctl(TLTARGIOCRELOADEXPORT, vinfo);
	if (retval != 0) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}
	tl_server_msg_success(comm, msg);
	return 0;
}

static void 
copy_file(int destfd, int srcfd)
{
	char buf[512];
	int retval;

	while (1)
	{
		retval = read(srcfd, buf, sizeof(buf));
		if (retval <= 0)
		{
			break;
		}

		retval = write(destfd, buf, retval);
		if (retval <= 0)
		{
			break;
		}
	}
}

static void
diag_dump_file(char *dirpath, char *src, char *dname)
{
	int srcfd;
	int destfd;
	char filepath[256];

	srcfd = open(src, O_RDONLY);
	if (srcfd < 0)
		return;

	snprintf(filepath, sizeof(filepath), "%s/%s", dirpath, dname);
	destfd = creat(filepath, S_IRWXU|S_IRWXG|S_IRWXO);
	if (destfd < 0)
	{
		close(srcfd);
		return;
	}
	copy_file(destfd, srcfd);
	close(destfd);
	close(srcfd);
	return;
}

static int
tl_server_run_diagnostics(struct tl_comm *comm, struct tl_msg *msg)
{
	char diagdir[256];
	char filepath[512];
	char cmd[256];
	FILE *fp;
	int fd;

	if (sscanf(msg->msg_data, "tempfile: %s\n", diagdir) != 1)
	{
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	snprintf(filepath, sizeof(filepath), "%s/scdiag.xml", diagdir);
	fd = creat(filepath, S_IRWXU|S_IRWXG|S_IRWXO);
	if (fd < 0)
	{ 
		DEBUG_ERR("Unable to open filepath %s\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}
	close(fd);

	fp = fopen(filepath, "w");
	if (!fp)
	{
		DEBUG_ERR("Unable to open filepath %s\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	fclose(fp);
	diag_dump_file(diagdir, "/proc/scsi/scsi", "procscsi");
	diag_dump_file(diagdir, "/var/log/messages", "varlog.log");
	snprintf(cmd, sizeof(cmd), "/quadstorvtl/bin/diaghelper %s", diagdir);
	system(cmd);
	tl_server_msg_success(comm, msg);
	return 0;
}

struct vdevice *
find_vdevice_by_name(char *name)
{
	struct vdevice *vdevice;
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		vdevice = device_list[i];

		if (!vdevice)
			continue;

		if (strcasecmp(vdevice->name, name) == 0)
			return vdevice;
	}
	return NULL;

}

struct vdevice *
find_vdevice(uint32_t tl_id, uint32_t target_id)
{
	struct vdevice *vdevice;
	struct vtlconf *vtlconf;
	struct tdriveconf *dconf;

	if (tl_id >= TL_MAX_DEVICES)
		return NULL;

	vdevice = device_list[tl_id];
	if (!vdevice || !target_id)
		return vdevice;

	if (vdevice->type != T_CHANGER)
		return NULL;

	vtlconf = (struct vtlconf *)(vdevice);
	TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) {
		if (dconf->vdevice.target_id == target_id)
			return ((struct vdevice *)(dconf));
	}
	return NULL;
}

static int
tl_server_reset_vdrive_stats(struct tl_comm *comm, struct tl_msg *msg)
{
	struct vdevice *vdevice;
	struct vdeviceinfo dinfo;
	uint32_t target_id, tl_id;
	int retval;

	if (sscanf(msg->msg_data, "tl_id: %u\ntarget_id: %u\n", &tl_id, &target_id) != 2) {
		DEBUG_WARN_SERVER("Invalid msg data");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vdevice = find_vdevice(tl_id, target_id);
	if (!vdevice) {
		DEBUG_WARN_SERVER("Invalid tl_id %u target_id %u passed\n", tl_id, target_id);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	memset(&dinfo, 0, sizeof(struct vdeviceinfo));
	dinfo.tl_id = tl_id;
	dinfo.target_id = target_id;

	retval = tl_ioctl(TLTARGIOCRESETSTATS, &dinfo);
	if (retval != 0) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tl_server_get_vdrive_stats(struct tl_comm *comm, struct tl_msg *msg)
{
	struct vdevice *vdevice;
	struct vdeviceinfo dinfo;
	uint32_t target_id, tl_id;
	int retval;

	if (sscanf(msg->msg_data, "tl_id: %u\ntarget_id: %u\n", &tl_id, &target_id) != 2) {
		DEBUG_WARN_SERVER("Invalid msg data");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vdevice = find_vdevice(tl_id, target_id);
	if (!vdevice) {
		DEBUG_WARN_SERVER("Invalid tl_id %u target_id %u passed\n", tl_id, target_id);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	memset(&dinfo, 0, sizeof(struct vdeviceinfo));
	dinfo.tl_id = tl_id;
	dinfo.target_id = target_id;

	retval = tl_ioctl(TLTARGIOCGETDEVICEINFO, &dinfo);
	if (retval != 0) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	free(msg->msg_data);
	msg->msg_data = malloc(sizeof(dinfo.stats));
	if (!msg->msg_data) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}
	memcpy(msg->msg_data, &dinfo.stats, sizeof(dinfo.stats));
	msg->msg_len = sizeof(dinfo.stats);
	msg->msg_resp = MSG_RESP_OK;
	tl_msg_send_message(comm, msg);
	tl_msg_free_message(msg);
	tl_msg_close_connection(comm);
	return 0;
}

static int
tl_server_get_iscsiconf(struct tl_comm *comm, struct tl_msg *msg)
{
	uint32_t target_id, tl_id;
	struct vdevice *vdevice;
	struct iscsiconf *iscsiconf = NULL;

	if (sscanf(msg->msg_data, "tl_id: %u\ntarget_id: %u\n", &tl_id, &target_id) != 2) {
		DEBUG_WARN_SERVER("Invalid msg data");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	vdevice = find_vdevice(tl_id, target_id);
	if (!vdevice) {
		DEBUG_WARN_SERVER("Invalid tl_id %u target_id %u passed\n", tl_id, target_id);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	iscsiconf = &vdevice->iscsiconf;
	free(msg->msg_data);
	msg->msg_data = malloc(sizeof(*iscsiconf));
	if (!msg->msg_data) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}
	memcpy(msg->msg_data, iscsiconf, sizeof(*iscsiconf));
	msg->msg_len = sizeof(*iscsiconf);
	msg->msg_resp = MSG_RESP_OK;
	tl_msg_send_message(comm, msg);
	tl_msg_free_message(msg);
	tl_msg_close_connection(comm);
	return 0;
}

static int
iqn_name_valid(char *name)
{
	int i;
	int len = strlen(name);

	for (i = 0; i < len; i++) {
		if (!isalnum(name[i]) && name[i] != '_' && name[i] != '-' && name[i] != '.')
			return 0;
	}
	return 1;
}

int
iqn_exists(char *iqn)
{
	struct vdevice *vdevice;
	struct vtlconf *vtlconf;
	struct tdriveconf *dconf;
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		vdevice = device_list[i];
		if (!vdevice)
			continue;
		if (strcasecmp(vdevice->iscsiconf.iqn, iqn) == 0)
			return 1;
		if (vdevice->type != T_CHANGER)
			continue;
		vtlconf = (struct vtlconf *)(vdevice);
		TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) {
			if (strcasecmp(dconf->vdevice.iscsiconf.iqn, iqn) == 0)
				return 1;
		}
	}
	return 0;
}

static int
tl_server_set_iscsiconf(struct tl_comm *comm, struct tl_msg *msg)
{
	struct vdevice *vdevice;
	struct iscsiconf *iscsiconf = NULL;
	struct iscsiconf newconf;
	int retval;
	char errmsg[512];

	if (msg->msg_len != sizeof(newconf)) {
		snprintf(errmsg, sizeof(errmsg), "Invalid msg data");
		goto senderr;
	}

	memcpy(&newconf, msg->msg_data, sizeof(newconf));
	vdevice = find_vdevice(newconf.tl_id, newconf.target_id);
	if (!vdevice) {
		sprintf(errmsg, "Invalid tl_id %u target_id %u passed\n", newconf.tl_id, newconf.target_id);
		goto senderr;
	}

	iscsiconf = &vdevice->iscsiconf;

	if (newconf.iqn[0] && strcmp(iscsiconf->iqn, newconf.iqn)) {
		if (!iqn_name_valid(newconf.iqn)) {
			snprintf(errmsg, sizeof(errmsg), "iqn %s is not valid\n", newconf.iqn);
			goto senderr;
		}

		if (iqn_exists(newconf.iqn)) {
			snprintf(errmsg, sizeof(errmsg), "IQN %s exists for another device\n", newconf.iqn);
			goto senderr;
		}
	}
	else {
		strcpy(newconf.iqn, iscsiconf->iqn);
	}

	retval = ietadm_mod_target(vdevice->iscsi_tid, &newconf, iscsiconf);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "ietadm update of new iSCSI settings failed\n");
		goto senderr;
	}

	memcpy(iscsiconf, &newconf, sizeof(newconf));

	retval = sql_update_iscsiconf(vdevice->tl_id, vdevice->target_id, iscsiconf);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Updating DB with new iSCSI settings failed\n");
		goto senderr;
	}

	tl_server_msg_success(comm, msg);
	return 0;
senderr:
	tl_server_msg_failure2(comm, msg, errmsg);
	return -1;
}

static int
tl_server_rescan_disks(struct tl_comm *comm, struct tl_msg *msg)
{
	int retval, i;
	struct tl_blkdevinfo *blkdev;

	retval = tl_common_scan_physdisk();
	if (retval != 0) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		retval = sync_blkdev(blkdev);
		if (retval != 0) {
			blkdev->offline = 1;
			continue;
		}

		if (blkdev->offline) {
			retval = load_blkdev(blkdev);
			if (retval == 0)
				blkdev_load_volumes(blkdev);
		}

	}
	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tl_server_load_drive(struct tl_comm *comm, struct tl_msg *msg)
{
	int retval;
	int load;
	struct vdevice *vdevice;
	struct vdeviceinfo dinfo;
	int tl_id;
	uint32_t tape_id;

	load = (msg->msg_id == MSG_ID_LOAD_DRIVE)  ? 1 : 0;

	retval = sscanf(msg->msg_data, "tl_id: %d\ntape_id: %u\n", &tl_id, &tape_id);
	if (retval != 2)
	{
		DEBUG_ERR("Invalid load drive message msg is %s\n", msg->msg_data);
		tl_server_msg_invalid(comm, msg);
		return -1;
	}

	if (tl_id < 0 || tl_id >= TL_MAX_DEVICES)
	{
		DEBUG_ERR("Invalid tl_id %d\n", tl_id);
		tl_server_msg_invalid(comm, msg);
		return -1;
	}

	vdevice = device_list[tl_id];
	if (!vdevice || vdevice->type != T_SEQUENTIAL)
	{
		tl_server_msg_invalid(comm, msg);
		return -1;
	}
	memset(&dinfo, 0, sizeof(struct vdeviceinfo));
	dinfo.type = T_SEQUENTIAL;
	dinfo.tl_id = vdevice->tl_id;
	dinfo.tape_id = tape_id;
	dinfo.target_id = vdevice->target_id;
	dinfo.mod_type = load;

	retval = tl_ioctl(TLTARGIOCLOADDRIVE, &dinfo);
	if (retval != 0)
	{
		tl_server_msg_failure(comm, msg);
		return -1;
	}
	tl_server_msg_success(comm, msg);
	return 0;
}

int mdaemon_exit;

static void
vtl_update_element_addresses(struct vdevice *vdevice)
{
	struct vcartridge *volume;
	PGconn *conn;
	int retval;

	conn = pgsql_begin();
	if (!conn)
		return;

	retval = sql_clear_slot_configuration(conn, vdevice->tl_id);
	if (retval != 0) {
		pgsql_rollback(conn);
		return;
	}

	TAILQ_FOREACH(volume, &vdevice->vol_list, q_entry) {
		/* Updates the used percentage of the volume */
		if (volume->loaderror)
			continue;
		retval = tl_ioctl(TLTARGIOCGETVCARTRIDGEINFO, volume);
		if (retval != 0)
			continue;
		sql_update_element_address(conn, volume->label, volume->elem_address);
	}
	pgsql_commit(conn);
}

static void
vdevice_update_element_addresses(void)
{
	struct vdevice *vdevice;
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		vdevice = device_list[i];

		if (!vdevice)
			continue;

		if (vdevice->type != T_CHANGER)
			continue;
		vtl_update_element_addresses(vdevice);
	}
}

static void
vdevice_reset_element_addresses(void)
{
	struct vdevice *vdevice;
	PGconn *conn;
	int i;

	conn = pgsql_begin();
	if (!conn)
		return;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		vdevice = device_list[i];

		if (!vdevice)
			continue;

		if (vdevice->type != T_CHANGER)
			continue;
		vtl_update_element_addresses(vdevice);
		sql_clear_slot_configuration(conn, vdevice->tl_id);
	}
	pgsql_commit(conn);
}

int
tl_server_unload(void)
{
	pthread_mutex_lock(&daemon_lock);
	if (mdaemon_exit)
	{
		pthread_mutex_unlock(&daemon_lock);
		return 0;
	}
	mdaemon_exit = 1;
	pthread_mutex_unlock(&daemon_lock);
	vdevice_update_element_addresses();
	tl_ioctl_void(TLTARGIOCUNLOAD);
	return 0;
}

static void
check_drive_compression(void)
{
	struct stat stbuf;

	if (stat("/dev/iodev", &stbuf) < 0)
		enable_drive_compression = 1;
}

static int
tl_server_fix_group_ids(void)
{
	struct tl_blkdevinfo *blkdev;
	int i, retval;

	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		if (blkdev->offline)
			continue;
		if (blkdev->db_group_id == blkdev->group_id)
			continue;
		retval = sql_update_blkdev_group_id(blkdev->bid, blkdev->group_id);
		if (retval != 0)
			return retval;
	}

	return 0;
}

void
tl_server_load(void)
{
	int retval;
	int check;

	check_drive_compression();

	tl_common_scan_physdisk();

	retval = sys_rid_load();
	if (retval != 0) {
		DEBUG_ERR_SERVER("Failed to load uuid information\n");
		exit(EXIT_FAILURE);
	}

	retval = tl_server_register_pid();
	if (retval != 0) {
		DEBUG_ERR_SERVER("Cannot register mdaemon pid\n");
		exit(EXIT_FAILURE);
	}

	retval = load_configured_groups();
	if (retval != 0) {
		DEBUG_ERR_SERVER("Getting configured pool list failed\n");
		exit(EXIT_FAILURE);
	}

	retval = load_configured_disks();
	if (retval != 0) {
		DEBUG_ERR_SERVER("Getting configure disks failed");
		exit(EXIT_FAILURE);
	}

	retval = load_configured_devices();
	if (retval != 0) {
		DEBUG_ERR_SERVER("Loading configured devices failed");
		exit(EXIT_FAILURE);
	}

	retval = load_fc_rules();
	if (retval != 0) {
		DEBUG_ERR_SERVER("Load configured fc rules failed\n");
		exit(EXIT_FAILURE);
	}

	retval = tl_server_process_lists();
	if (retval != 0) {
		DEBUG_ERR_SERVER("Processing device lists failed");
		exit(EXIT_FAILURE);
	}

	retval = tl_server_fix_group_ids();
	if (retval != 0) {
		DEBUG_ERR_SERVER("Cannot fix group ids\n");
		exit(EXIT_FAILURE);
	}

	check = query_disk_check();
	if (check) {
		retval = tl_ioctl_void(TLTARGIOCCHECKDISKS);
		if (retval != 0) {
			DEBUG_ERR_SERVER("Disk check failed");
			exit(EXIT_FAILURE);
		}
	}

	tl_ioctl_void(TLTARGIOCLOADDONE);
	vdevice_reset_element_addresses();
}

static void
tl_server_get_status(struct tl_comm *comm, struct tl_msg *msg)
{
	char respmsg[100];

	if (done_init)
		strcpy(respmsg, "Server initialized and running\n");
	else
		strcpy(respmsg, "Server initializing...\n");

	tl_server_send_message(comm, msg, respmsg);
}

static int
tl_server_add_group(struct tl_comm *comm, struct tl_msg *msg)
{
	PGconn *conn;
	char groupname[256];
	char errmsg[256];
	struct group_info *group_info = NULL;
	struct group_conf group_conf;
	int retval, worm;

	if (sscanf(msg->msg_data, "groupname: %s\nworm: %d\n", groupname, &worm) != 2) {
		snprintf(errmsg, sizeof(errmsg), "Invalid msg msg_data\n");
		goto senderr;
	}

	if (group_name_exists(groupname)) {
		snprintf(errmsg, sizeof(errmsg), "Pool name %s exists\n", groupname);
		goto senderr;
	}

	if (strlen(groupname) > TDISK_NAME_LEN) {
		snprintf(errmsg, sizeof(errmsg), "Pool name can be upto a maximum of %d characters", TDISK_NAME_LEN);
		goto senderr;
	}

	if (!target_name_valid(groupname)) {
		snprintf(errmsg, sizeof(errmsg), "Pool name can only contain alphabets, numbers, underscores and hyphens");
		goto senderr;
	}

	group_info = alloc_buffer(sizeof(*group_info));
	if (!group_info) {
		snprintf(errmsg, sizeof(errmsg), "Memory allocation error\n");
		goto senderr;
	}

	group_info->group_id  = get_next_group_id();
	if (!group_info->group_id) {
		snprintf(errmsg, sizeof(errmsg), "Cannot get group id\n");
		goto senderr;
	}

	conn = pgsql_begin();
	if (!conn) {
		snprintf(errmsg, sizeof(errmsg), "Unable to connect to db\n");
		goto senderr;
	}

	strcpy(group_info->name, groupname);
	group_info->worm = worm;
	TAILQ_INIT(&group_info->bdev_list);

	retval = sql_add_group(conn, group_info);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Cannot add pool to db\n");
		goto errrsp;
	}

	strcpy(group_conf.name, group_info->name);
	group_conf.group_id = group_info->group_id;
	group_conf.worm = group_info->worm;
	retval = tl_ioctl(TLTARGIOCADDGROUP, &group_conf);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Cannot add pool, ioctl failed\n");
		goto errrsp;
	}

	retval = pgsql_commit(conn);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Unable to commit transaction\n");
		goto senderr;
	}

	group_list[group_info->group_id] = group_info;

	tl_server_msg_success(comm, msg);
	return 0;
errrsp:
	pgsql_rollback(conn);
senderr:
	if (group_info)
		free(group_info);
	tl_server_msg_failure2(comm, msg, errmsg);
	return -1;
}

static int
__list_groups(char *filepath, int configured)
{
	struct group_info *group_info;
	FILE *fp;
	int i;

	fp = fopen(filepath, "w");
	if (!fp) {
		DEBUG_ERR_SERVER("Cannot open file %s\n", filepath);
		return -1;
	}

	for (i = 0; i < TL_MAX_POOLS; i++) {
		group_info = group_list[i];
		if (!group_info)
			continue;
		if (configured && TAILQ_EMPTY(&group_info->bdev_list))
			continue;

		fprintf(fp, "<group>\n");
		fprintf(fp, "group_id: %u\n", group_info->group_id);
		fprintf(fp, "name: %s\n", group_info->name);
		fprintf(fp, "worm: %d\n", group_info->worm);
		fprintf(fp, "disks: %d\n", group_get_disk_count(group_info));
		fprintf(fp, "</group>\n");
	}

	fclose(fp);
	return 0;
}

static int
tl_server_list_groups(struct tl_comm *comm, struct tl_msg *msg, int configured)
{
	char filepath[256];
	int retval;

	if (sscanf(msg->msg_data, "tempfile: %s\n", filepath) != 1)
	{
		DEBUG_ERR_SERVER("Invalid msg data");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	retval = __list_groups(filepath, configured);
	if (retval != 0) {
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tl_server_delete_group(struct tl_comm *comm, struct tl_msg *msg)
{
	char errmsg[256];
	struct group_info *group_info;
	struct group_conf group_conf;
	int retval;
	uint32_t group_id;

	if (sscanf(msg->msg_data, "group_id: %u\n", &group_id) != 1) {
		snprintf(errmsg, sizeof(errmsg), "Invalid msg msg_data\n");
		goto senderr;
	}

	if (!group_id) {
		snprintf(errmsg, sizeof(errmsg), "Cannot delete default group\n");
		goto senderr;
	}

	group_info = find_group(group_id);
	if (!group_info) {
		snprintf(errmsg, sizeof(errmsg), "Cannot find pool at group_id %u\n", group_id);
		goto senderr;
	}

	if (!TAILQ_EMPTY(&group_info->bdev_list)) {
		snprintf(errmsg, sizeof(errmsg), "Pool %s disk list not empty\n", group_info->name);
		goto senderr;
	}

	strcpy(group_conf.name, group_info->name);
	group_conf.group_id = group_info->group_id;
	retval = tl_ioctl(TLTARGIOCDELETEGROUP, &group_conf);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Cannot add pool, ioctl failed\n");
		goto senderr;
	}

	retval = sql_delete_group(group_id);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Cannot delete pool information from DB\n");
		goto senderr;
	}

	group_list[group_info->group_id] = NULL;
	free(group_info);
	tl_server_msg_success(comm, msg);
	return 0;
senderr:
	tl_server_msg_failure2(comm, msg, errmsg);
	return -1;
}

static int
tl_server_rename_pool(struct tl_comm *comm, struct tl_msg *msg)
{
	PGconn *conn;
	char errmsg[256];
	struct group_info *group_info;
	struct group_conf group_conf;
	char name[TDISK_MAX_NAME_LEN], newname[TDISK_MAX_NAME_LEN];
	uint32_t group_id;
	int retval;

	if (sscanf(msg->msg_data, "group_id:%u\ngroupname: %s\n", &group_id, newname) != 2) {
		snprintf(errmsg, sizeof(errmsg), "Invalid msg msg_data\n");
		goto senderr;
	}

	group_info = find_group(group_id);
	if (!group_info) {
		snprintf(errmsg, sizeof(errmsg), "Cannot find pool with id %u\n", group_id);
		goto senderr;
	}

	if (!group_info->group_id) {
		tl_server_msg_success(comm, msg);
		return 0;
	}

	if (strlen(newname) > TDISK_NAME_LEN) {
		snprintf(errmsg, sizeof(errmsg), "Pool name can be upto a maximum of %d characters", TDISK_NAME_LEN);
		goto senderr;
	}

	if (!target_name_valid(newname)) {
		snprintf(errmsg, sizeof(errmsg), "Pool name can only contain alphabets, numbers, underscores and hyphens");
		goto senderr;
	}

	if (strcmp(group_info->name, newname) == 0) {
		tl_server_msg_success(comm, msg);
		return 0;
	}

	conn = pgsql_begin();
	if (!conn) {
		snprintf(errmsg, sizeof(errmsg), "Unable to connect to db\n");
		goto senderr;
	}

	strcpy(name, group_conf.name);
	group_conf.group_id = group_info->group_id;
	strcpy(group_conf.name, newname);

	retval = sql_rename_pool(group_info->group_id, newname);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Update db with new pool name failed\n");
		goto rollback;
	}

	retval = tl_ioctl(TLTARGIOCRENAMEGROUP, &group_conf);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Changing pool name failed\n");
		goto rollback;
	}

	retval = pgsql_commit(conn);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Unable to commit transaction\n");
		goto senderr;
	}
	strcpy(group_info->name, newname);
	tl_server_msg_success(comm, msg);
	return 0;

rollback:
	strcpy(group_conf.name, name);
	pgsql_rollback(conn);
senderr:
	tl_server_msg_failure2(comm, msg, errmsg);
	return -1;
}

static int
tl_server_get_pool_configured_disks(struct tl_comm *comm, struct tl_msg *msg)
{
	char filepath[256];
	FILE *fp;
	struct tl_blkdevinfo *blkdev;
	int retval, i;
	uint32_t group_id;

	if (sscanf(msg->msg_data, "target_id: %u\ntempfile: %s\n", &group_id, filepath) != 2) {
		DEBUG_ERR_SERVER("Invalid msg data");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	fp = fopen(filepath, "w");
	if (!fp)
	{
		DEBUG_ERR_SERVER("Cannot open file %s\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	for (i = 1; i < TL_MAX_DISKS; i++) {
		struct bdev_info binfo;

		blkdev = bdev_list[i];
		if (!blkdev)
			continue;

		if (!blkdev->group || blkdev->group->group_id != group_id)
			continue;

		fprintf(fp, "<disk>\n");
		memset(&binfo, 0, sizeof(struct bdev_info));
		binfo.bid = blkdev->bid;

		if (!blkdev->offline) {
			retval = tl_ioctl(TLTARGIOCGETBLKDEV, &binfo);
			if (retval == 0) {
				blkdev->disk.size = binfo.size;
				blkdev->disk.used = (binfo.size - binfo.free);
				blkdev->disk.reserved = binfo.reserved;
				blkdev->disk.unmap = binfo.unmap;
			}
			blkdev->disk.info.online = 1;
			strcpy(blkdev->disk.group_name, blkdev->group->name);
		}

		dump_disk(fp, &blkdev->disk, blkdev->bid);
		fprintf(fp, "</disk>\n");
	}

	fclose(fp);

	tl_server_msg_success(comm, msg);
	return 0;
}

static int
tl_server_handle_msg(struct tl_comm *comm, struct tl_msg *msg)
{

	switch (msg->msg_id) {
		case MSG_ID_SERVER_STATUS:
			tl_server_get_status(comm, msg);
			break;
		case MSG_ID_LOAD_CONF:
			tl_server_load_conf(comm, msg);
			break;
		case MSG_ID_UNLOAD_CONF:
			tl_server_unload();
			tl_server_msg_success(comm, msg);
			break;
		case MSG_ID_ADD_VTL_CONF:
			tl_server_add_vtl_conf(comm, msg);
			break;
		case MSG_ID_ADD_DRIVE_CONF:
			tl_server_add_drive_conf(comm, msg);
			break;
		case MSG_ID_ADD_VOL_CONF:
			tl_server_add_vol_conf(comm, msg);
			break;
		case MSG_ID_DELETE_VOL_CONF:
			tl_server_delete_vol_conf(comm, msg);
			break;
		case MSG_ID_DELETE_VTL_CONF:
			tl_server_delete_vtl_conf(comm, msg);
			break;
		case MSG_ID_GET_VTL_LIST:
			tl_server_get_vtl_list(comm, msg);
			break;
		case MSG_ID_GET_VTL_CONF:
			tl_server_get_vtl_conf(comm, msg);
			break;
		case MSG_ID_GET_CONFIGURED_DISKS:
			tl_server_get_configured_disks(comm, msg);
			break;
		case MSG_ID_LIST_DISKS:
			tl_server_list_disks(comm, msg);
			break;
		case MSG_ID_ADD_DISK:
			tl_server_add_disk(comm, msg);
			break;
		case MSG_ID_DELETE_DISK:
			tl_server_delete_disk(comm, msg);
			break;
		case MSG_ID_RESCAN_DISKS:
			tl_server_rescan_disks(comm, msg);
			break;
		case MSG_ID_GET_ISCSICONF:
			tl_server_get_iscsiconf(comm, msg);
			break;
		case MSG_ID_SET_ISCSICONF:
			tl_server_set_iscsiconf(comm, msg);
			break;
		case MSG_ID_VTL_INFO:
			tl_server_vtl_info(comm, msg);
			break;
		case MSG_ID_VTL_DRIVE_INFO:
			tl_server_vtl_drive_info(comm, msg);
			break;
		case MSG_ID_GET_VDRIVE_STATS:
			tl_server_get_vdrive_stats(comm, msg);
			break;
		case MSG_ID_RESET_VDRIVE_STATS:
			tl_server_reset_vdrive_stats(comm, msg);
			break;
		case MSG_ID_VTL_VOL_INFO:
			tl_server_vtl_vol_info(comm, msg);
			break;
		case MSG_ID_RUN_DIAGNOSTICS:
			tl_server_run_diagnostics(comm, msg);
			break;
		case MSG_ID_DISK_CHECK:
			tl_server_disk_check(comm, msg);
			break;
		case MSG_ID_LOAD_DRIVE:
		case MSG_ID_UNLOAD_DRIVE:
			tl_server_load_drive(comm, msg);
			break;
		case MSG_ID_RELOAD_EXPORT:
			tl_server_reload_export(comm, msg);
			break;
		case MSG_ID_ADD_GROUP:
			pthread_mutex_lock(&daemon_lock);
			tl_server_add_group(comm, msg);
			pthread_mutex_unlock(&daemon_lock);
			break;
		case MSG_ID_LIST_GROUP:
			pthread_mutex_lock(&daemon_lock);
			tl_server_list_groups(comm, msg, 0);
			pthread_mutex_unlock(&daemon_lock);
			break;
		case MSG_ID_LIST_GROUP_CONFIGURED:
			pthread_mutex_lock(&daemon_lock);
			tl_server_list_groups(comm, msg, 1);
			pthread_mutex_unlock(&daemon_lock);
			break;
		case MSG_ID_DELETE_GROUP:
			pthread_mutex_lock(&daemon_lock);
			tl_server_delete_group(comm, msg);
			pthread_mutex_unlock(&daemon_lock);
			break;
		case MSG_ID_RENAME_POOL:
			pthread_mutex_lock(&daemon_lock);
			tl_server_rename_pool(comm, msg);
			pthread_mutex_unlock(&daemon_lock);
			break;
		case MSG_ID_GET_POOL_CONFIGURED_DISKS:
			pthread_mutex_lock(&daemon_lock);
			tl_server_get_pool_configured_disks(comm, msg);
			pthread_mutex_unlock(&daemon_lock);
			break;
		case MSG_ID_LIST_FC_RULES:
			pthread_mutex_lock(&daemon_lock);
			tl_server_list_fc_rules(comm, msg);
			pthread_mutex_unlock(&daemon_lock);
			break;
		case MSG_ID_ADD_FC_RULE:
			pthread_mutex_lock(&daemon_lock);
			tl_server_add_fc_rule(comm, msg);
			pthread_mutex_unlock(&daemon_lock);
			break;
		case MSG_ID_REMOVE_FC_RULE:
			pthread_mutex_lock(&daemon_lock);
			tl_server_remove_fc_rule(comm, msg);
			pthread_mutex_unlock(&daemon_lock);
			break;
		default:
			/* Invalid msg id */
			DEBUG_ERR_SERVER("Invalid msg id %d in message", msg->msg_id);
			tl_server_msg_invalid(comm, msg);
			break;
	}
	return 0;
}

int
tl_server_process_request(int fd, struct sockaddr_un *client_addr)
{
	struct tl_comm comm;
	struct tl_msg *msg;

	comm.sockfd = fd;

	msg = tl_msg_recv_message(&comm);

	if (!msg)
	{
		DEBUG_ERR("Message reception failed");
		return -1;
	}

	tl_server_handle_msg(&comm, msg);

	return 0;
}

static void *
server_init(void * arg)
{
	struct sockaddr_un un_addr;
	struct sockaddr_un client_addr;
	socklen_t addr_len;
	int sockfd, newfd;
	int reuse = 1;

	if ((sockfd = socket(AF_LOCAL, SOCK_STREAM, 0)) < 0) {
		DEBUG_ERR_SERVER("Unable to create listen socket\n");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) == -1) {
		DEBUG_ERR_SERVER("Unable to setsockopt SO_REUSEADDR\n");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	unlink(MDAEMON_PATH);
	memset(&un_addr, 0, sizeof(struct sockaddr_un));
	un_addr.sun_family = AF_LOCAL;
#ifdef FREEBSD
	memcpy((char *) &un_addr.sun_path, MDAEMON_PATH, strlen(MDAEMON_PATH));
#else
	memcpy((char *) &un_addr.sun_path+1, MDAEMON_PATH, strlen(MDAEMON_PATH));
#endif
	addr_len = SUN_LEN(&un_addr); 

	if (bind(sockfd, (struct sockaddr *)&un_addr, sizeof(un_addr)) == -1)
	{
		DEBUG_ERR_SERVER("Unable to bind to mdaemon port errno %d %s\n", errno, strerror(errno));
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	if (listen(sockfd, MDAEMON_BACKLOG) == -1)
	{
		DEBUG_ERR_SERVER("Listen call for mdaemon socket failed\n");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	pthread_mutex_lock(&daemon_lock);
	done_socket_init = 1;
	pthread_cond_broadcast(&socket_cond);
	pthread_mutex_unlock(&daemon_lock);

	while (1)
	{
		addr_len = sizeof(struct sockaddr_un);

		if ((newfd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_len)) == -1)
		{
			DEBUG_WARN_SERVER("Client connection accept failed");
			continue;
		}

		tl_server_process_request(newfd, &client_addr);
	}

	close(sockfd);
	pthread_exit(0);
}

int
main_server_start(pthread_t *thread_id)
{
	int retval;

	retval = pthread_create(thread_id, NULL, server_init, NULL);

	if (retval != 0)
	{
		DEBUG_ERR_SERVER("Unable to start a new thread for server\n");
		return -1;
	}

	pthread_mutex_lock(&daemon_lock);
	if (!done_socket_init)
		pthread_cond_wait(&socket_cond, &daemon_lock);
	pthread_mutex_unlock(&daemon_lock);

	tl_server_load();

	pthread_mutex_lock(&daemon_lock);
	done_server_init = 1;
	pthread_cond_broadcast(&daemon_cond);
	pthread_mutex_unlock(&daemon_lock);

	return 0;

}


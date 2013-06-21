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

#include <tlclntapi.h>
#include <vdevice.h>

static int
tl_client_send_msg(struct tl_msg *msg, char *reply)
{
	struct tl_comm *tl_comm;
	struct tl_msg *resp;
	int retval;

	tl_comm = tl_msg_make_connection();
	if (!tl_comm) {
		if (msg->msg_len)
			free(msg->msg_data);
		return -1;
	}

	retval = tl_msg_send_message(tl_comm, msg);

	if (msg->msg_len)
		free(msg->msg_data);

	if (retval != 0)
		return retval;

	resp = tl_msg_recv_message(tl_comm);
	if (!resp) {
		tl_msg_free_connection(tl_comm);
		return -1;
	}

	if (!reply)
		goto skip;

	if (resp->msg_len > 0)
		memcpy(reply, resp->msg_data, resp->msg_len);
	reply[resp->msg_len] = 0; /* trailing 0 ??? ensure correctness */

skip:
	retval = resp->msg_resp;
	tl_msg_free_message(resp);
	tl_msg_free_connection(tl_comm);
	return retval;
}

static int
tl_client_get_target_data(struct tl_msg *msg, void *ptr, int len)
{
	struct tl_comm *tl_comm;
	struct tl_msg *resp;
	int retval;

	tl_comm = tl_msg_make_connection();
	if (!tl_comm) {
		fprintf(stderr, "connect failed\n");
		if (msg->msg_len)
			free(msg->msg_data);
		return -1;
	}

	retval = tl_msg_send_message(tl_comm, msg);
	free(msg->msg_data);
	if (retval != 0) {
		fprintf(stderr, "message transfer failed\n");
		return -1;
	}

	resp = tl_msg_recv_message(tl_comm);
	if (!resp) {
		tl_msg_free_connection(tl_comm);
		return -1;
	}

	if (resp->msg_resp != MSG_RESP_OK) {
		fprintf(stderr, "Failed msg response %d\n", resp->msg_resp);
		tl_msg_free_message(resp);
		tl_msg_free_connection(tl_comm);
		return -1;
	}

	if (resp->msg_len != len) {
		fprintf(stderr, "Invalid msg len %d required %d\n", resp->msg_len, len);
		tl_msg_free_message(resp);
		tl_msg_free_connection(tl_comm);
		return -1;
	}

	memcpy(ptr, resp->msg_data, len);
	tl_msg_free_message(resp);
	tl_msg_free_connection(tl_comm);
	return 0;
}

int
tl_client_list_target_generic(uint32_t target_id, char *tempfile, int msg_id)
{
	struct tl_msg msg;

	msg.msg_id = msg_id;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "target_id: %u\ntempfile: %s\n", target_id, tempfile);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_list_groups(struct group_list *group_list, int msg_id)
{
	char tempfile[100];
	FILE *fp;
	int fd, retval;

	TAILQ_INIT(group_list);

	strcpy(tempfile, "/tmp/.quadstorlstsg.XXXXXX");
	fd = mkstemp(tempfile);
	if (fd == -1)
		return -1;
	close(fd);

	retval = tl_client_list_generic(tempfile, msg_id);
	if (retval != 0) {
		remove(tempfile);
		return -1;
	}

	fp = fopen(tempfile, "r");
	if (!fp) {
		remove(tempfile);
		return -1;
	}

	retval = tl_common_parse_group(fp, group_list);
	fclose(fp);
	remove(tempfile);
	return retval;
}

int
tl_client_add_group(char *groupname, int worm, char *reply)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_ADD_GROUP;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "groupname: %s\nworm: %d", groupname, worm);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, reply);
}

int
tl_client_delete_group(uint32_t group_id)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_DELETE_GROUP;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "group_id: %d\n", group_id);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_rename_pool(uint32_t group_id, char *name, char *reply)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_RENAME_POOL;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "group_id: %u\ngroupname: %s\n", group_id, name);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, reply);
}

int
tl_client_get_iscsiconf(int tl_id, uint32_t target_id, struct iscsiconf *iscsiconf)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_GET_ISCSICONF;

	msg.msg_data = malloc(512);
	if (!msg.msg_data) {
		return -1;
	}

	sprintf(msg.msg_data, "tl_id: %d\ntarget_id: %u\n", tl_id, target_id);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_get_target_data(&msg, iscsiconf, sizeof(*iscsiconf));
}

int
tl_client_set_iscsiconf(struct iscsiconf *iscsiconf, char *reply)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_SET_ISCSICONF;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	memcpy(msg.msg_data, iscsiconf, sizeof(*iscsiconf));
	msg.msg_len = sizeof(*iscsiconf);

	return tl_client_send_msg(&msg, reply);
}

int
tl_client_get_string(char *reply, int msg_id)
{
	struct tl_msg msg;

	msg.msg_id = msg_id;
	msg.msg_len = 0;

	return tl_client_send_msg(&msg, reply);
}

int
tl_client_list_generic(char *tempfile, int msg_id)
{
	struct tl_msg msg;

	msg.msg_id = msg_id;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tempfile: %s\n", tempfile);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_load_conf()
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_LOAD_CONF;
	msg.msg_len = 0;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_unload_conf()
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_UNLOAD_CONF;
	msg.msg_len = 0;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_load_drive(int msg_id, int tl_id, uint32_t tape_id, char *reply)
{
	struct tl_msg msg;

	msg.msg_id = msg_id;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tl_id: %d\ntape_id: %u", tl_id, tape_id);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, reply);
}

int
tl_client_get_configured_disks(char *tempfile)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_GET_CONFIGURED_DISKS;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tempfile: %s\n", tempfile);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_list_disks(char *tempfile)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_LIST_DISKS;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tempfile: %s\n", tempfile);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_list_vtls(char *tempfile)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_GET_VTL_LIST;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tempfile: %s\n", tempfile);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_get_vtl_conf(char *tempfile, int tl_id)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_GET_VTL_CONF;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tempfile: %s\ntl_id: %d\n", tempfile, tl_id);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int tl_client_delete_vtl_conf(int tl_id)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_DELETE_VTL_CONF;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tl_id: %d\n", tl_id);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int tl_client_delete_vol_conf(int tl_id, uint32_t tape_id)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_DELETE_VOL_CONF;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tl_id: %d\ntape_id: %u\n", tl_id, tape_id);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_add_vol_conf(uint32_t group_id, char *label, int tl_id, int voltype, int nvolumes, int worm, char *reply)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_ADD_VOL_CONF;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "group_id: %u\nlabel: %s\ntl_id: %d\nvoltype: %d\nnvolumes: %d\nworm: %d\n", group_id, label, tl_id, voltype, nvolumes, worm);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, reply);
}

int tl_client_add_vtl_conf(char *tempfile, int *tl_id, char *reply)
{
	struct tl_msg msg;
	int retval;

	msg.msg_id = MSG_ID_ADD_VTL_CONF;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tempfile: %s\n", tempfile);
	msg.msg_len = strlen(msg.msg_data)+1;

	retval = tl_client_send_msg(&msg, reply);
	if (retval != 0)
		return retval;

	retval = sscanf(reply, "tl_id: %d\n", tl_id);
	if (retval != 1)
		return -1;
	return 0;
}

int tl_client_add_drive_conf(char *name, int drivetype, int *ret_tl_id, char *reply)
{
	struct tl_msg msg;
	int retval;

	msg.msg_id = MSG_ID_ADD_DRIVE_CONF;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "name: %s\ndrivetype: %d\n", name, drivetype);
	msg.msg_len = strlen(msg.msg_data)+1;

	retval = tl_client_send_msg(&msg, reply);
	if (retval != 0)
		return retval;

	retval = sscanf(reply, "tl_id: %d\n", ret_tl_id);
	if (retval != 1)
		return -1;
	return 0;
}

int
tl_client_add_disk(char *dev, uint32_t group_id, char *reply)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_ADD_DISK;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "group_id: %u\ndev: %s\n", group_id, dev);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, reply);
}

int
tl_client_delete_disk(char *dev, char *reply)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_DELETE_DISK;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "dev: %s\n", dev);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, reply);
}

int
tl_client_rescan_disks(void)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_RESCAN_DISKS;
	msg.msg_len = 0;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_vtl_info(char *tempfile, int tl_id, int msgid)
{
	struct tl_msg msg;

	msg.msg_id = msgid;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tempfile: %s\ntl_id: %u\n", tempfile, tl_id);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_drive_info(char *tempfile, int tl_id, int target_id, int msgid)
{
	struct tl_msg msg;

	msg.msg_id = msgid;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tempfile: %s\ntl_id: %u\ntarget_id: %d", tempfile, tl_id, target_id);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_run_diagnostics(char *tempfile)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_RUN_DIAGNOSTICS;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tempfile: %s\n", tempfile);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_modify_vtlconf(int tl_id, int op, int val)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_MODIFY_VTLCONF;
	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tl_id: %d\nop: %d\nval: %d\n", tl_id, op, val);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_reload_export(int tl_id, uint32_t tape_id)
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_RELOAD_EXPORT;

	msg.msg_data = malloc(512);
	if (!msg.msg_data)
		return -1;

	sprintf(msg.msg_data, "tl_id: %d\ntape_id: %u\n", tl_id, tape_id);
	msg.msg_len = strlen(msg.msg_data)+1;

	return tl_client_send_msg(&msg, NULL);
}

int tl_client_disk_check()
{
	struct tl_msg msg;

	msg.msg_id = MSG_ID_DISK_CHECK;
	msg.msg_len = 0;
	return tl_client_send_msg(&msg, NULL);
}

int
tl_client_fc_rule_op(struct fc_rule_spec *fc_rule_spec, char *reply, int msg_id)
{
	struct tl_msg msg;

	msg.msg_id = msg_id;
	msg.msg_len = sizeof(*fc_rule_spec);
	msg.msg_data = malloc(sizeof(*fc_rule_spec));
	if (!msg.msg_data)
		return -1;

	memcpy(msg.msg_data, fc_rule_spec, sizeof(*fc_rule_spec));

	return tl_client_send_msg(&msg, reply);
}


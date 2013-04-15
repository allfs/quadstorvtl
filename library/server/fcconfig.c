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
#include <tlsrvapi.h>
#include <sqlint.h>

extern struct fc_rule_list fc_rule_list;

struct fc_rule *
fc_rule_locate(struct fc_rule_spec *fc_rule_spec)
{
	struct fc_rule *fc_rule;

	TAILQ_FOREACH(fc_rule, &fc_rule_list, q_entry) {
		if (fc_rule_spec->vtl[0] && fc_rule->target_id == -1)
			continue;
		if (!fc_rule_spec->vtl[0] && fc_rule->target_id != -1)
			continue;
		if (fc_rule->target_id != -1 && strcmp(fc_rule->vtl, fc_rule_spec->vtl))
			continue;
		if (strcmp(fc_rule->wwpn, fc_rule_spec->wwpn))
			continue;
		return fc_rule;
	}
	return NULL;
}

int
tl_server_remove_vtl_fc_rules(int tl_id)
{
	int retval;
	struct fc_rule *fc_rule, *next;

	retval = sql_delete_vtl_fc_rules(tl_id);
	if (retval != 0)
		return retval;

	TAILQ_FOREACH_SAFE(fc_rule, &fc_rule_list, q_entry, next) {
		if (fc_rule->target_id != tl_id)
			continue;
		TAILQ_REMOVE(&fc_rule_list, fc_rule, q_entry);
		free(fc_rule);
	}
	return 0;
}
 
int
tl_server_remove_fc_rule(struct tl_comm *comm, struct tl_msg *msg)
{
	struct fc_rule_config fc_rule_config;
	struct fc_rule_spec *fc_rule_spec;
	struct fc_rule *fc_rule, *next;
	int retval;
	int found = 0;

	if (msg->msg_len < sizeof(*fc_rule_spec)) {
		tl_server_msg_failure2(comm, msg, "Invalid message");
		return -1;
	}

	fc_rule_spec = (struct fc_rule_spec *)(msg->msg_data);

	if (!fc_rule_spec->wwpn[0] && !fc_rule_spec->vtl[0]) {
		fc_rule = fc_rule_locate(fc_rule_spec);
		if (!fc_rule) {
			tl_server_msg_failure2(comm, msg, "Cannot locate fc rule\n");
			return -1;
		}

		retval = sql_delete_fc_rule(fc_rule);
		if (retval != 0) {
			tl_server_msg_failure2(comm, msg, "Cannot remove fc rule some/all specifications from db");
			return -1;
		}

		fc_rule_config_fill(fc_rule, &fc_rule_config);
		retval = tl_ioctl(TLTARGIOCREMOVEFCRULE, &fc_rule_config);
		if (retval != 0) {
			tl_server_msg_failure2(comm, msg, "Cannot remove fc rule some/all specifications from kernel");
			return -1;
		}
		TAILQ_REMOVE(&fc_rule_list, fc_rule, q_entry);
		free(fc_rule);
		tl_server_msg_success(comm, msg);
		return 0;
	}

	TAILQ_FOREACH_SAFE(fc_rule, &fc_rule_list, q_entry, next) {
		if (fc_rule_spec->wwpn[0] && !fc_rule->wwpn[0])
			continue;
		if (fc_rule_spec->wwpn[0] && fc_rule->wwpn[0] && strcmp(fc_rule->wwpn, fc_rule_spec->wwpn))
			continue;
		if (fc_rule_spec->vtl[0] && fc_rule->target_id == -1)
			continue;
		if (fc_rule_spec->vtl[0] && strcmp(fc_rule->vtl, fc_rule_spec->vtl))
			continue;
		retval = sql_delete_fc_rule(fc_rule);
		if (retval != 0) {
			tl_server_msg_failure2(comm, msg, "Cannot remove fc rule some/all specifications from db");
			return -1;
		}

		fc_rule_config_fill(fc_rule, &fc_rule_config);
		retval = tl_ioctl(TLTARGIOCREMOVEFCRULE, &fc_rule_config);
		if (retval != 0) {
			tl_server_msg_failure2(comm, msg, "Cannot remove fc rule some/all specifications from kernel");
			return -1;
		}
		TAILQ_REMOVE(&fc_rule_list, fc_rule, q_entry);
		free(fc_rule);
		found++;
	}

	if (!found) {
		tl_server_msg_failure2(comm, msg, "Cannot find any matching rules");
		return -1;
	}
	tl_server_msg_success(comm, msg);
	return 0;
}

int
tl_server_add_fc_rule(struct tl_comm *comm, struct tl_msg *msg)
{
	PGconn *conn;
	struct fc_rule_spec *fc_rule_spec;
	struct fc_rule_config fc_rule_config;
	struct fc_rule *fc_rule;
	struct vdevice *vdevice = NULL;
	int retval;

	if (msg->msg_len < sizeof(*fc_rule_spec)) {
		tl_server_msg_failure2(comm, msg, "Invalid message");
		return -1;
	}

	fc_rule_spec = (struct fc_rule_spec *)(msg->msg_data);

	fc_rule = fc_rule_locate(fc_rule_spec);
	if (fc_rule) {
		tl_server_msg_failure2(comm, msg, "FC rule for WWPN already exists");
		return -1;
	}

	if (fc_rule_spec->vtl[0]) {
		vdevice = find_vdevice_by_name(fc_rule_spec->vtl);
		if (!vdevice) {
			tl_server_msg_failure2(comm, msg, "Cannot locate VTL");
			return -1;
		}
	}

	fc_rule = alloc_buffer(sizeof(*fc_rule));
	if (!fc_rule) {
		tl_server_msg_failure2(comm, msg, "Memory allocation failure");
		return -1;
	}

	strcpy(fc_rule->wwpn, fc_rule_spec->wwpn);
	if (vdevice) {
		strcpy(fc_rule->vtl, vdevice->name);
		fc_rule->target_id = vdevice->tl_id;
	}
	else 
		fc_rule->target_id = -1;
	fc_rule->rule = fc_rule_spec->rule;

	conn = pgsql_begin();
	if (!conn) {
		free(fc_rule);
		tl_server_msg_failure2(comm, msg, "Cannot connect to DB");
		return -1;
	}

	retval = sql_add_fc_rule(fc_rule);
	if (retval != 0) {
		free(fc_rule);
		tl_server_msg_failure2(comm, msg, "Cannot insert fc rule specification into db");
		pgsql_rollback(conn);
		return -1;
	}

	fc_rule_config_fill(fc_rule, &fc_rule_config);
	retval = tl_ioctl(TLTARGIOCADDFCRULE, &fc_rule_config);
	if (retval != 0) {
		free(fc_rule);
		tl_server_msg_failure2(comm, msg, "Cannot insert fc rule specification into kernel");
		pgsql_rollback(conn);
		return -1;
	}

	retval = pgsql_commit(conn);
	if (retval != 0) {
		free(fc_rule);
		tl_server_msg_failure2(comm, msg, "Cannot commit db transaction\n");
		return -1;
	}

	TAILQ_INSERT_TAIL(&fc_rule_list, fc_rule, q_entry);
	tl_server_msg_success(comm, msg);
	return 0;
}

int
tl_server_list_fc_rules(struct tl_comm *comm, struct tl_msg *msg)
{
	char filepath[256];
	FILE *fp;
	struct fc_rule *fc_rule;
	struct sockaddr_in in_addr;
	char wwpn[24];

	if (sscanf(msg->msg_data, "tempfile: %s\n", filepath) != 1) {
		DEBUG_ERR_SERVER("Invalid msg data receied");
		tl_server_msg_failure(comm, msg);
		return -1;
	}

	fp = fopen(filepath, "w");
	if (!fp) {
		DEBUG_ERR_SERVER("Cannot open file %s\n", filepath);
		tl_server_msg_failure(comm, msg);
		return -1;
	}

        memset(&in_addr, 0, sizeof(in_addr));
	TAILQ_FOREACH(fc_rule, &fc_rule_list, q_entry) {
		if (fc_rule->wwpn[0])
			strcpy(wwpn, fc_rule->wwpn);
		else
			strcpy(wwpn, "All");

		if (fc_rule->vtl[0])
			fprintf(fp, "wwpn: %s vtl: %s rule: %d\n", wwpn, fc_rule->vtl, fc_rule->rule);
		else
			fprintf(fp, "wwpn: %s vtl: All rule: %d\n", wwpn, fc_rule->rule);
	}
	fclose(fp);
	tl_server_msg_success(comm, msg);
	return 0;
}


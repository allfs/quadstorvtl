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

#include "ietadm.h"
#include <sqlint.h>

void
vdevice_construct_iqn(struct vdevice *vdevice, struct vdevice *parent)
{
	struct iscsiconf *iscsiconf = &vdevice->iscsiconf;
	char device_str[128];

	if (iscsiconf->iqn[0])
		return;

	if (vdevice->type == T_CHANGER)
		sprintf(device_str, "iqn.2006-06.com.quadstor.vtl.%s.autoloader", vdevice->name);
	else if (vdevice->target_id)
		sprintf(device_str, "iqn.2006-06.com.quadstor.vtl.%s.drive%d", parent->name, vdevice->target_id);
	else
		sprintf(device_str, "iqn.2006-06.com.quadstor.vdrive.%s", vdevice->name);
	sprintf(iscsiconf->iqn, device_str);
}

int 
ietadm_default_settings(struct vdevice *vdevice, struct vdevice *parent)
{
	int retval;
	struct iscsiconf *iscsiconf = &vdevice->iscsiconf;

	memset(iscsiconf, 0, sizeof(*iscsiconf));
	iscsiconf->tl_id = vdevice->tl_id;
	iscsiconf->target_id = vdevice->target_id;
	strcpy(iscsiconf->IncomingUser, "");
	strcpy(iscsiconf->IncomingPasswd, "");
	strcpy(iscsiconf->OutgoingUser, "");
	strcpy(iscsiconf->OutgoingPasswd, "");
	vdevice_construct_iqn(vdevice, parent);

	retval = sql_add_iscsiconf(vdevice->tl_id, vdevice->target_id, iscsiconf);
	if (retval != 0) {
		DEBUG_ERR("Failed for tl_id %d target_id %u\n", vdevice->tl_id, vdevice->target_id);
		return -1;
	}

	return 0;
}

int
ietadm_mod_target(int tid, struct iscsiconf *iscsiconf, struct iscsiconf *oldconf)
{
	char cmd[512];
	int retval;
	char user[40], passwd[40];

	if (tid <= 0)
		return 0;

	if (oldconf && strcmp(iscsiconf->iqn, oldconf->iqn)) {
		sprintf(cmd, "%s --op rename --tid=%d --params Name=%s", IETADM_PATH, tid, iscsiconf->iqn);
		DEBUG_INFO("iqn change cmd is %s\n", iscsiconf->iqn);
		retval  = system(cmd);
		if (retval != 0) {
			DEBUG_WARN_SERVER("Changing target iqn failed: cmd is %s %d %s\n", cmd, errno, strerror(errno));
			return -1;
		}
	}

	if (strlen(iscsiconf->IncomingUser) > 0) {
		strcpy(user, iscsiconf->IncomingUser);
		strcpy(passwd, iscsiconf->IncomingPasswd);
		sprintf(cmd, "%s --op new --tid=%d --user --params=IncomingUser=%s,Password=%s\n", IETADM_PATH, tid, user, passwd);
		retval  = system(cmd);
		if (retval != 0) {
			DEBUG_WARN_SERVER("Changing target user configuration failed: cmd is %s %d %s\n", cmd, errno, strerror(errno));
			return -1;
		}
	}

	if (oldconf && strlen(oldconf->IncomingUser) > 0) {
		strcpy(user, oldconf->IncomingUser);
		strcpy(passwd, oldconf->IncomingPasswd);
		sprintf(cmd, "%s --op delete --tid=%d --user --params=IncomingUser=%s,Password=%s\n", IETADM_PATH, tid, user, passwd);
		retval  = system(cmd);
		if (retval != 0) {
			DEBUG_WARN_SERVER("Changing target user configuration failed: cmd is %s %d %s\n", cmd, errno, strerror(errno));
			return -1;
		}
	}

	if (oldconf && strlen(oldconf->OutgoingUser) > 0) {
		strcpy(user, oldconf->OutgoingUser);
		strcpy(passwd, oldconf->OutgoingPasswd);
		sprintf(cmd, "%s --op delete --tid=%d --user --params=OutgoingUser=%s,Password=%s\n", IETADM_PATH, tid, user, passwd);
		retval  = system(cmd);
		if (retval != 0) {
			DEBUG_WARN_SERVER("Changing target user configuration failed: cmd is %s %d %s\n", cmd, errno, strerror(errno));
			return -1;
		}
	}

	if (strlen(iscsiconf->OutgoingUser) > 0) {
		strcpy(user, iscsiconf->OutgoingUser);
		strcpy(passwd, iscsiconf->OutgoingPasswd);
		sprintf(cmd, "%s --op new --tid=%d --user --params=OutgoingUser=%s,Password=%s\n", IETADM_PATH, tid, user, passwd);
		retval  = system(cmd);
		if (retval != 0) {
			DEBUG_WARN_SERVER("Changing target user configuration failed: cmd is %s %d %s\n", cmd, errno, strerror(errno));
			return -1;
		}
	}

	return 0;
}

int
ietadm_add_target(struct vdevice *vdevice)
{
	char cmd[512];
	int retval;

	if (vdevice->iscsi_tid < 0)
		return -1;

	sprintf(cmd, "%s --op new --tid=%d --params Name=%s\n", IETADM_PATH, vdevice->iscsi_tid, vdevice->iscsiconf.iqn);
	retval  = system(cmd);

	if (retval != 0) {
		DEBUG_ERR("ietadm_add_target returned not zero status %d cmd is %s err %d %s\n", retval, cmd, errno, strerror(errno));
		vdevice->iscsi_tid = -1;
		return retval;	
	}
	vdevice->iscsi_attached = 1;

	retval = ietadm_mod_target(vdevice->iscsi_tid, &vdevice->iscsiconf, NULL);
	return retval;
}

int
ietadm_delete_target(int tid)
{
	char cmd[256];
	int retval;

	if (tid < 0)
		return 0;

	sprintf(cmd, "/quadstor/bin/ietadm --op delete --tid=%d > /dev/null 2>&1", tid);
	retval = system(cmd);
	if (retval != 0)
	{
		DEBUG_ERR("ietadm_delete_target failed for tid %d cmd %s errno %d %s\n", tid, cmd, errno, strerror(errno));
		return -1;
	}

	return 0;
}

int
ietadm_qload_done(void)
{
	char cmd[512];
	int retval;

	sprintf(cmd, "%s -q\n", IETADM_PATH);
	retval = system(cmd);
	if (retval != 0) {
		DEBUG_ERR_SERVER("ietadm returned not zero status %d cmd is %s %d %s\n", retval, cmd, errno, strerror(errno));
	}
	return retval;
}

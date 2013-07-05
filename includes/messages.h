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

#ifndef MESSAGES_H_
#define MESSAGES_H_

enum {
	MSG_ID_SERVER_STATUS,
	MSG_ID_LIST_BLKDEV,
	MSG_ID_ADD_BLKDEV,
	MSG_ID_REMOVE_BLKDEV,
	MSG_ID_ADD_VOLUME,
	MSG_ID_REMOVE_VOLUME,
	MSG_ID_LOAD_CONF,
	MSG_ID_UNLOAD_CONF,
	MSG_ID_ADD_DEVICE,
	MSG_ID_REMOVE_DEVICE,
	MSG_ID_LIST_VOLUME,
	MSG_ID_GET_BLKDEVINFO,
	MSG_ID_ADD_VTL_CONF,
	MSG_ID_ADD_VOL_CONF,
	MSG_ID_ADD_DRIVE_CONF,
	MSG_ID_DELETE_VOL_CONF,
	MSG_ID_GET_VOL_CONF,
	MSG_ID_GET_DRIVE_CONF,
	MSG_ID_DELETE_VTL_CONF,
	MSG_ID_DELETE_BLKDEV,
	MSG_ID_DELETE_DRIVE_CONF,
	MSG_ID_GET_CONFIGURED_DISKS,
	MSG_ID_LIST_DISKS,
	MSG_ID_ADD_DISK,
	MSG_ID_DELETE_DISK,
	MSG_ID_GET_VTL_LIST,
	MSG_ID_GET_VTL_CONF,
	MSG_ID_RESCAN_DISKS,
	MSG_ID_REBOOT_SYSTEM,
	MSG_ID_GET_ISCSICONF,
	MSG_ID_SET_ISCSICONF,
	MSG_ID_VTL_INFO,
	MSG_ID_VTL_DRIVE_INFO,
	MSG_ID_VTL_VOL_INFO,
	MSG_ID_RUN_DIAGNOSTICS,
	MSG_ID_DISK_CHECK,
	MSG_ID_MODIFY_VTLCONF,
	MSG_ID_LOAD_DRIVE,
	MSG_ID_UNLOAD_DRIVE,
	MSG_ID_RELOAD_EXPORT,
	MSG_ID_ADD_GROUP,
	MSG_ID_DELETE_GROUP,
	MSG_ID_LIST_GROUP,
	MSG_ID_LIST_GROUP_CONFIGURED,
	MSG_ID_GET_POOL_CONFIGURED_DISKS,
	MSG_ID_RENAME_POOL,
	MSG_ID_ADD_FC_RULE,
	MSG_ID_REMOVE_FC_RULE,
	MSG_ID_LIST_FC_RULES,
	MSG_ID_GET_VDRIVE_STATS,
	MSG_ID_RESET_VDRIVE_STATS,
};

#define MSG_STR_INVALID_MSG  "Invalid Message data or ID"
#define MSG_STR_COMMAND_FAILED  "Command Failed"
#define MSG_STR_COMMAND_SUCCESS  "Command Success"
#define MSG_STR_SERVER_BUSY  "Server Busy"
#define MSG_STR_AUTH_FAILURE  "Authentication failure"

/* Response Codes */
enum {
	MSG_RESP_OK		= 0x0000,
	MSG_RESP_ERROR		= 0x0001,
	MSG_RESP_BUSY		= 0x0002,
	MSG_RESP_INVALID	= 0x0003,
	MSG_RESP_AUTH_FAILURE	= 0x0004,
};

#endif /* MESSAGES_H_ */

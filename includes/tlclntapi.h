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

#ifndef TLCLNTAPI_H_
#define TLCLNTAPI_H_

#include <apicommon.h>
int tl_client_list_target_generic(uint32_t target_id, char *tempfile, int msg_id);
int tl_client_get_string(char *reply, int msg_id);
int tl_client_list_generic(char *tempfile, int msg_id);
int tl_client_get_iscsiconf(int tl_id, uint32_t target_id, struct iscsiconf *iscsiconf);
int tl_client_set_iscsiconf(struct iscsiconf *iscsiconf, char *reply);
int tl_client_list_disks(struct d_list *dlist, int msg_id);
int tl_client_list_groups(struct group_list *group_list, int msg_id);
int tl_client_add_group(char *groupname, int worm, char *reply);
int tl_client_delete_group(uint32_t group_id, char *reply);
int tl_client_rename_pool(uint32_t group_id, char *name, char *reply);

int tl_client_load_conf(void);
int tl_client_unload_conf(void);
int tl_client_add_blkdev(char *devname, char *reply);
struct tl_blkdevinfo * tl_client_get_blkdevinfo(void);
int tl_client_add_driveconf(char *filepath);
int tl_client_delete_volconf(uint32_t tape_id);
int tl_client_delete_blkdev(char *devname, char *reply);
int tl_client_load_drive(int msg_id, int tl_id, uint32_t tape_id, char *reply);
int tl_client_get_configured_disks(char *tempfile);
int tl_client_add_disk(char *dev, uint32_t group_id, char *reply);
int tl_client_delete_disk(char *dev, char *reply);
int tl_client_list_vtls(char *tempfile);
int tl_client_get_vtl_conf(char *tempfile, int tl_id);
int tl_client_delete_vtl_conf(int tl_id);
int tl_client_add_drive_conf(char *name, int drivetype, int *ret_tl_id, char *reply);
int tl_client_add_vtl_conf(char *tempfile, int *tl_id, char *reply);
int tl_client_add_vol_conf(uint32_t group_id, char *label, int tl_id, int voltype, int nvolumes, int worm, char *reply);
int tl_client_delete_vol_conf(int tl_id, uint32_t tape_id);
int tl_client_rescan_disks(void);
int tl_client_vtl_info(char *tempfile, int tl_id, int msgid);
int tl_client_drive_info(char *tempfile, int tl_id, int target_id, int msgid);
int tl_client_run_diagnostics(char *tempfile);
int tl_client_reload_export(int tl_id, uint32_t tape_id);
int tl_client_disk_check(void);
int tl_client_modify_vtlconf(int tl_id, int op, int val);
int tl_client_fc_rule_op(struct fc_rule_spec *fc_rule_spec, char *reply, int msg_id);
int tl_client_get_vdrive_stats(int tl_id, int target_id, struct tdrive_stats *stats);
int tl_client_reset_vdrive_stats(int tl_id, int target_id);

#endif /* TLCLNTAPI_H_ */

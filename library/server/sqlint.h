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

#ifndef QUADSTOR_SQLINT_H_
#define QUADSTOR_SQLINT_H_

#include "pgsql.h"

PGconn *sql_add_blkdev(struct physdisk *disk, uint32_t bid);
int sql_delete_blkdev(struct tl_blkdevinfo *binfo);
int sql_add_vcartridge(PGconn *conn, struct vcartridge *vinfo);
int sql_delete_vcartridge(PGconn *conn, int tl_id, uint32_t tape_id);
int sql_add_vtl_drive(int tl_id, struct tdriveconf *driveconf);
int sql_add_drive(struct tdriveconf *driveconf);
int sql_delete_vtl(int tl_id);
int sql_update_iscsiconf(int tl_id, uint32_t target_id, struct iscsiconf *iscsiconf);
int sql_add_iscsiconf(int tl_id, uint32_t target_id, struct iscsiconf *iscsiconf);
int sql_add_vtl(struct vtlconf *vtlconf);
int sql_query_driveprop(struct tdriveconf *driveconf);
int sql_query_drives(struct vtlconf *vtlconf);
struct vdevice;
int sql_query_vdevice(struct vdevice *device_list[]);
int sql_query_volumes(struct tl_blkdevinfo *binfo);
struct blist;
int sql_query_blkdevs(struct tl_blkdevinfo *bdev_list[]);
int sql_virtvol_label_exists(char *label);
int sql_get_last_range(char *prefix, char *suffix);
int sql_virtvol_label_unique(char *label);
int sql_set_volume_exported(struct vcartridge *vinfo);
uint32_t sql_get_libid(struct physdevice *device, int devtype, int *enabled);
uint32_t sql_get_driveid(struct physdevice *device, uint32_t libid);
int sql_query_iscsiconf(int tl_id, uint32_t target_id, struct iscsiconf *iscsiconf);
int sql_query_groups(struct group_list *group_list);
int sql_add_group(PGconn *conn, struct group_info *group_info);
int sql_delete_group(uint32_t group_id);
int sql_query_fc_rules(struct fc_rule_list *fc_rule_list);
int sql_add_fc_rule(struct fc_rule *fc_rule);
int sql_rename_pool(uint32_t group_id, char *name);
int sql_delete_fc_rule(struct fc_rule *fc_rule);
int sql_delete_vtl_fc_rules(int tl_id);
int sql_clear_slot_configuration(PGconn *conn, int tl_id);
int sql_update_element_address(PGconn *conn, int tl_id, int tid, int eaddress);
#endif

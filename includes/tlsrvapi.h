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

#ifndef TLSRVAPI_H_
#define TLSRVAPI_H_

#include <physlib.h>
#include <vdevice.h>


#include <apicommon.h>

uint32_t get_new_tape_id(void);
int main_server_start(pthread_t *thread_id);
int new_server_start(pthread_t *thread_id);
int tl_server_register_pid(void);
int tl_server_unload(void);
struct group_info * find_group(uint32_t group_id);
struct vcartridge * find_volume(int tl_id, uint32_t tape_id);
struct vdevice * find_vdevice(uint32_t tl_id, uint32_t target_id);
int vtl_name_exists(char *name);
struct tl_blkdevinfo * blkdev_new(char *devname);
void tl_server_msg_success(struct tl_comm *comm, struct tl_msg *msg);
void tl_server_msg_failure(struct tl_comm *comm, struct tl_msg *msg);
void tl_server_msg_failure2(struct tl_comm *comm, struct tl_msg *msg, char *newmsg);
void fc_rule_config_fill(struct fc_rule *fc_rule, struct fc_rule_config *fc_rule_config);
int tl_server_remove_vtl_fc_rules(int tl_id);
int tl_server_remove_fc_rule(struct tl_comm *comm, struct tl_msg *msg);
int tl_server_add_fc_rule(struct tl_comm *comm, struct tl_msg *msg);
int tl_server_list_fc_rules(struct tl_comm *comm, struct tl_msg *msg);
#endif

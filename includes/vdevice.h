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

#ifndef VDEVICE_H_
#define VDEVICE_H_

#include <queue.h>
#include <pthread.h>
#include "../common/commondefs.h"

TAILQ_HEAD(vlist, vcartridge); 

struct vdevice {
	int type;
	int tl_id;
	int target_id;
	int iscsi_tid;
	int iscsi_attached;
	int vhba_id;
	int offline;
	char name[40];
	char serialnumber[40];
	struct vlist vol_list;
	pthread_mutex_t vdevice_lock;
	struct iscsiconf iscsiconf;
	TAILQ_ENTRY(vdevice) q_entry;
};
TAILQ_HEAD(vdevlist, vdevice);

struct vtlconf;
struct tdriveconf;
struct vtlconf * vtlconf_new(int tl_id, char *name, char *serialnumber);
struct tdriveconf * tdriveconf_new(int tl_id, int target_id, char *name, char *serialnumber);
int dump_vdevice(FILE *fp, struct vdevice *vdevice, int dumpdrivelist);
int dump_volume(FILE *fp, struct vcartridge *vinfo);
struct vdevice * parse_vdevice(FILE *fp);
void free_vdevice(struct vdevice *);
struct vdevice * find_vdevice_by_name(char *name);

#endif

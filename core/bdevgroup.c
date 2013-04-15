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

#include "bdevgroup.h"

static SLIST_HEAD(, bdevgroup) group_list = SLIST_HEAD_INITIALIZER(group_list);

struct bdevgroup *group_none;

struct bdevgroup *
bdev_group_locate(uint32_t group_id)
{
	struct bdevgroup *group;

	SLIST_FOREACH(group, &group_list, g_list) {
		if (group->group_id == group_id)
			return group;
	}
	return NULL;
}

int
bdev_group_add(struct group_conf *group_conf)
{
	struct bdevgroup *group;

	group = bdev_group_locate(group_conf->group_id);
	if (group) {
		debug_warn("Pool with id %u already exists\n", group_conf->group_id);
		return -1;
	}

	group = zalloc(sizeof(*group), M_QUADSTOR, Q_WAITOK);
	if (unlikely(!group)) {
		debug_warn("Memory allocation failure\n");
		return -1;
	}

	group->group_id = group_conf->group_id;
	strcpy(group->name, group_conf->name);
	group->worm = group_conf->worm;
	SLIST_INIT(&group->alloc_list);
	group->alloc_lock = sx_alloc("group alloc lock");

	if (!group->group_id)
		group_none = group;

	SLIST_INSERT_HEAD(&group_list, group, g_list);
	return 0;
}

static void
bdev_group_free(struct bdevgroup *group)
{
	if (group == group_none)
		group_none = NULL;
	sx_free(group->alloc_lock);
	free(group, M_QUADSTOR);
}

int
bdev_group_rename(struct group_conf *group_conf)
{
	struct bdevgroup *group;
	struct bdevint *bint;
	char prev_name[TDISK_MAX_NAME_LEN];
	int retval;

	group = bdev_group_locate(group_conf->group_id);
	if (!group) {
		debug_warn("Cannot locate pool with id %u\n", group_conf->group_id);
		return -1;
	}

	bint = group->master_bint;
	strcpy(prev_name, group->name);
	strcpy(group->name, group_conf->name);

	if (!bint)
		return 0;

	retval = bint_sync(bint);
	if (unlikely(retval != 0)) {
		strcpy(group->name, prev_name);
	}
	return retval;
}

int
bdev_group_remove(struct group_conf *group_conf)
{
	struct bdevgroup *group;

	group = bdev_group_locate(group_conf->group_id);
	if (!group) {
		debug_warn("Cannot locate pool with id %u\n", group_conf->group_id);
		return -1;
	}

	if (atomic_read(&group->bdevs)) {
		debug_warn("Pool %s:%u busy bdevs %d\n", group->name, group->group_id, atomic_read(&group->bdevs));
		return -1;
	}

	SLIST_REMOVE(&group_list, group, bdevgroup, g_list);
	bdev_group_free(group);
	return 0;
}

void
bdev_groups_free(void)
{
	struct bdevgroup *group;

	while ((group = SLIST_FIRST(&group_list)) != NULL) {
		SLIST_REMOVE_HEAD(&group_list, g_list);
		bdev_group_free(group);
	}
}

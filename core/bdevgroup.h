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

#ifndef QS_BDEVGROUP_H_
#define QS_BDEVGROUP_H_
#include "bdev.h"
#include "../common/commondefs.h"

struct bdevgroup {
	char name[TL_MAX_NAME_LEN];
	SLIST_HEAD(, bdevint) alloc_list;
	atomic_t bdevs;
	struct bdevint *eligible;
	struct bdevint *master_bint;
	sx_t *alloc_lock;
	SLIST_ENTRY(bdevgroup) g_list;
	uint32_t group_id;
	int worm;
};

struct bdevgroup * bdev_group_locate(uint32_t group_id);
int bdev_group_add(struct group_conf *group_conf);
int bdev_group_remove(struct group_conf *group_conf);
int bdev_group_rename(struct group_conf *group_conf);
void bdev_groups_free(void);

static inline struct bdevint *
bint_get_group_master(struct bdevint *bint)
{
	return (bint->group->master_bint);
}

static inline void
bint_set_group_master(struct bdevint *bint)
{
	struct bdevgroup *group = bint->group;

	debug_check(group->master_bint && group->master_bint != bint);
	group->master_bint = bint;
	if (group->group_id && atomic_test_bit(GROUP_FLAGS_WORM, &bint->group_flags)) {
		debug_check(!group->worm);
		group->worm = 1;
	}
}

static inline void
bint_clear_group_master(struct bdevgroup *group, struct bdevint *bint)
{
	if (group->master_bint == bint)
		group->master_bint = NULL;
}

static inline int
bint_is_group_master(struct bdevint *bint)
{
	return atomic_test_bit(GROUP_FLAGS_MASTER, &bint->group_flags);
}

#endif

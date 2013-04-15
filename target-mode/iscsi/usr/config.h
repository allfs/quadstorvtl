/*
 * config.h - ietd plain file-based configuration
 *
 * Copyright (C) 2004-2005 FUJITA Tomonori <tomof at acm dot org>
 * Copyright (C) 2004-2010 VMware, Inc. All Rights Reserved.
 * Copyright (C) 2007-2010 Ross Walker <rswwalker at gmail dot com>
 *
 * This file is part of iSCSI Enterprise Target software.
 *
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef CONFIG_H
#define CONFIG_H

struct config_operations {
	void (*init) (char *params, int *timeout);
	int (*target_add) (u32 *, char *);
	int (*target_rename) (u32 , char *);
	int (*target_stop) (u32);
	int (*target_del) (u32);
	int (*lunit_add) (u32, u32, char *);
	int (*lunit_stop) (u32, u32);
	int (*lunit_del) (u32, u32);
	int (*param_set) (u32, u64, int, u32, struct iscsi_param *);
	int (*account_add) (u32, int, char *, char *);
	int (*account_del) (u32, int, char *);
	int (*account_query) (u32, int, char *, char *);
	int (*account_list) (u32, int, u32 *, u32 *, char *, size_t);
	int (*initiator_allow) (u32, int, char *);
	int (*target_allow) (u32, struct sockaddr *);
	int (*target_redirect) (u32, char *, u8);
};

extern struct config_operations *cops;

#endif

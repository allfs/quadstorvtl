/*
 * ietadm.h - ietd management program
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

#ifndef _IET_ADM_H
#define _IET_ADM_H

#ifdef FREEBSD
#define IETADM_NAMESPACE "/var/run/iet.sock"
#else
#define IETADM_NAMESPACE "IET_ABSTRACT_NAMESPACE"
#endif

struct msg_trgt {
	char name[ISCSI_NAME_LEN];
	char alias[ISCSI_NAME_LEN];

	u32 type;
	u32 session_partial;
	u32 target_partial;
	struct iscsi_param session_param[session_key_last];
	struct iscsi_param target_param[target_key_last];
};

struct msg_acnt {
	u32 auth_dir;
	union {
		struct {
			char name[ISCSI_NAME_LEN];
			char pass[ISCSI_NAME_LEN];
		} user;
		struct {
			u32 alloc_len;
			u32 count;
			u32 overflow;
		} list;
	} u;
};

struct msg_lunit {
	char args[ISCSI_ARGS_LEN];
};

struct msg_redir {
	char dest[NI_MAXHOST + NI_MAXSERV + 4];
};

enum ietadm_cmnd {
	C_TRGT_NEW,
	C_TRGT_DEL,
	C_TRGT_UPDATE,
	C_TRGT_SHOW,
	C_TRGT_REDIRECT,

	C_SESS_NEW,
	C_SESS_DEL,
	C_SESS_UPDATE,
	C_SESS_SHOW,

	C_CONN_NEW,
	C_CONN_DEL,
	C_CONN_UPDATE,
	C_CONN_SHOW,

	C_LUNIT_NEW,
	C_LUNIT_DEL,
	C_LUNIT_UPDATE,
	C_LUNIT_SHOW,

	C_ACCT_NEW,
	C_ACCT_DEL,
	C_ACCT_UPDATE,
	C_ACCT_SHOW,

	C_SYS_NEW,
	C_SYS_DEL,
	C_SYS_UPDATE,
	C_SYS_SHOW,

	C_ACCT_LIST,

	C_QLOAD,
	C_TRGT_RENAME,
};

struct ietadm_req {
	enum ietadm_cmnd rcmnd;

	u32 tid;
	u64 sid;
	u32 cid;
	u32 lun;

	union {
		struct msg_trgt trgt;
		struct msg_acnt acnt;
		struct msg_lunit lunit;
		struct msg_redir redir;
	} u;
};

struct ietadm_rsp {
	int err;
};

#endif

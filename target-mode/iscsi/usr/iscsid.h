/*
 * iscsid.h - ietd iSCSI protocol processing
 *
 * Copyright (C) 2002-2003 Ardis Technolgies <roman at ardistech dot com>
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

#ifndef ISCSID_H
#define ISCSID_H

#include <search.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#include "types.h"
#include "iscsi_hdr.h"
#include "iet_u.h"
#include "param.h"
#include "config.h"
#include "misc.h"

#define PROC_SESSION	"/proc/net/iet/session"
#define CTL_DEVICE	"/dev/iscsit"

struct buf_segment {
	struct __qelem entry;

	unsigned int len;
	char data[0];
};

struct PDU {
	struct iscsi_hdr bhs;
	void *ahs;
	unsigned int ahssize;
	void *data;
	unsigned int datasize;
};

#define KEY_STATE_START		0
#define KEY_STATE_REQUEST	1
#define KEY_STATE_DONE		2

struct session {
	struct __qelem slist;

	char *initiator;
	struct target *target;
	union iscsi_sid sid;

	int conn_cnt;
};

struct connection {
	int state;
	int iostate;
	int fd;

	struct session *session;

	u32 tid;
	struct iscsi_param session_param[session_key_last];

	char *initiator;
	union iscsi_sid sid;
	u16 cid;

	int session_type;
	int auth_method;

	u32 stat_sn;
	u32 exp_stat_sn;

	u32 cmd_sn;
	u32 exp_cmd_sn;
	u32 max_cmd_sn;
	u32 ttt;

	struct PDU req;
	void *req_buffer;
	struct PDU rsp;
	struct __qelem rsp_buf_list;

	unsigned char *buffer;
	int rwsize;

	int auth_state;
	union {
		struct {
			int digest_alg;
			int id;
			int challenge_size;
			unsigned char *challenge;
		} chap;
	} auth;
};

#define IOSTATE_FREE		0
#define IOSTATE_READ_BHS	1
#define IOSTATE_READ_AHS_DATA	2
#define IOSTATE_WRITE_BHS	3
#define IOSTATE_WRITE_AHS	4
#define IOSTATE_WRITE_DATA	5

#define STATE_FREE		0
#define STATE_SECURITY		1
#define STATE_SECURITY_AUTH	2
#define STATE_SECURITY_DONE	3
#define STATE_SECURITY_LOGIN	4
#define STATE_SECURITY_FULL	5
#define STATE_LOGIN		6
#define STATE_LOGIN_FULL	7
#define STATE_FULL		8
#define STATE_KERNEL		9
#define STATE_CLOSE		10
#define STATE_EXIT		11

#define AUTH_STATE_START	0
#define AUTH_STATE_CHALLENGE	1

/* don't touch these */
#define AUTH_DIR_INCOMING       0
#define AUTH_DIR_OUTGOING       1

#define SESSION_NORMAL		0
#define SESSION_DISCOVERY	1
#define AUTH_UNKNOWN		-1
#define AUTH_NONE		0
#define AUTH_CHAP		1
#define DIGEST_UNKNOWN		-1

#define BHS_SIZE		48

#define INCOMING_BUFSIZE	8192

#define LISTEN_MAX		8
#define INCOMING_MAX		32

enum {
	POLL_LISTEN,
	POLL_IPC = POLL_LISTEN + LISTEN_MAX,
	POLL_NL,
	POLL_ISNS,
	POLL_SCN_LISTEN,
	POLL_SCN,
	POLL_INCOMING,
	POLL_MAX = POLL_INCOMING + INCOMING_MAX,
};

struct target {
	struct __qelem tlist;

	struct __qelem sessions_list;

	u32 tid;
	char name[ISCSI_NAME_LEN];
	char *alias;

	struct redirect_addr {
		char addr[NI_MAXHOST + 1];
		char port[NI_MAXSERV + 1];
		u8 type;
	} redirect;

	int max_nr_sessions;
	int nr_sessions;

	struct __qelem isns_head;
};

/* chap.c */
extern int cmnd_exec_auth_chap(struct connection *conn);

/* conn.c */
extern struct connection *conn_alloc(void);
extern void conn_free(struct connection *conn);
extern int conn_test(struct connection *conn);
extern void conn_take_fd(struct connection *conn, int fd);
extern void conn_read_pdu(struct connection *conn);
extern void conn_write_pdu(struct connection *conn);
extern void conn_free_pdu(struct connection *conn);
extern void conn_free_rsp_buf_list(struct connection *conn);

/* ietd.c */
extern uint16_t server_port;
extern void isns_set_fd(int isns, int scn_listen, int scn);

/* iscsid.c */
extern int iscsi_debug;

extern int cmnd_execute(struct connection *conn);
extern void cmnd_finish(struct connection *conn);
extern char *text_key_find(struct connection *conn, char *searchKey);
extern void text_key_add(struct connection *conn, char *key, char *value);

/* log.c */
extern int log_daemon;
extern int log_level;

extern void log_init(void);
extern void log_warning(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern void log_error(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern void log_debug(int level, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));
extern void log_pdu(int level, struct PDU *pdu);

/* session.c */
extern struct session *session_find_name(u32 tid, const char *iname, union iscsi_sid sid);
extern struct session *session_find_id(u32 tid, u64 sid);
extern int session_create(struct connection *conn);
extern int session_remove(struct session *session);

/* target.c */
extern struct __qelem targets_list;
extern int target_add(u32 *, char *);
extern int target_rename(u32 , char *);
extern int target_del(u32);
extern int target_redirected(struct target *, struct connection *, struct sockaddr *);
extern struct target * target_find_by_name(const char *name);
extern struct target * target_find_by_id(u32);
extern void target_list_build(struct connection *, char *);

/* message.c */
extern int ietadm_request_listen(void);
extern int ietadm_request_handle(int accept_fd);

/* ctldev.c */
struct iscsi_kernel_interface {
	int (*ctldev_open) (void);
	int (*module_info) (struct module_info *);
	int (*lunit_create) (u32 tid, u32 lun, char *args);
	int (*lunit_destroy) (u32 tid, u32 lun);
	int (*param_get) (u32, u64, int, struct iscsi_param *);
	int (*param_set) (u32, u64, int, u32, struct iscsi_param *);
	int (*target_create) (u32 *, char *);
	int (*target_destroy) (u32);
	int (*session_create) (u32, u64, u32, u32, char *);
	int (*session_destroy) (u32, u64);
	int (*session_info) (struct session_info *);
	int (*conn_create) (u32, u64, u32, u32, u32, int, u32, u32);
	int (*conn_destroy) (u32 tid, u64 sid, u32 cid);
};

extern struct iscsi_kernel_interface *ki;

/* event.c */
extern void handle_iscsi_events(int fd);
extern int nl_open(void);

/* param.c */
extern int param_index_by_name(char *name, struct iscsi_key *keys);

/* isns.c */
extern int isns_init(char *addr, int isns_ac);
extern int isns_handle(int is_timeout, int *timeout);
extern int isns_scn_handle(int accept);
extern int isns_scn_allow(u32 tid, char *name);
extern int isns_target_register(char *name);
extern int isns_target_deregister(char *name);
extern void isns_exit(void);

#endif	/* ISCSID_H */

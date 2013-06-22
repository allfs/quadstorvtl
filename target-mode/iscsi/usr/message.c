/*
 * message.c - ietd inter-process communication
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "iscsid.h"
#include "ietadm.h"

int ietadm_request_listen(void)
{
	int fd, err;
	struct sockaddr_un addr;

	fd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (fd < 0)
		return fd;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_LOCAL;
#ifdef FREEBSD
	memcpy((char *) &addr.sun_path, IETADM_NAMESPACE, strlen(IETADM_NAMESPACE));
	unlink(IETADM_NAMESPACE);
#else
	memcpy((char *) &addr.sun_path + 1, IETADM_NAMESPACE, strlen(IETADM_NAMESPACE));
#endif

	if ((err = bind(fd, (struct sockaddr *) &addr, sizeof(addr))) < 0) {
		close(fd);
		return err;
	}

	if ((err = listen(fd, 32)) < 0) {
		close(fd);
		return err;
	}

	return fd;
}

extern int qload;

static void ietadm_request_exec(struct ietadm_req *req, struct ietadm_rsp *rsp,
				void **rsp_data, size_t *rsp_data_sz)
{
	int err = 0;

	log_debug(1, "%u %u %" PRIu64 " %u %u", req->rcmnd, req->tid,
		  req->sid, req->cid, req->lun);

	switch (req->rcmnd) {
	case C_TRGT_NEW:
		err = cops->target_add(&req->tid, req->u.trgt.name);
		break;
	case C_TRGT_RENAME:
		err = cops->target_rename(req->tid, req->u.trgt.name);
		break;
	case C_TRGT_DEL:
		err = cops->target_del(req->tid);
		break;
	case C_TRGT_UPDATE:
		if (req->u.trgt.type & (1 << key_session))
			err = cops->param_set(req->tid, req->sid,
					      key_session,
					      req->u.trgt.session_partial,
					      req->u.trgt.session_param);

		if (err < 0)
			goto out;

		if (req->u.trgt.type & (1 << key_target))
			err = cops->param_set(req->tid, req->sid, key_target,
					      req->u.trgt.target_partial,
					      req->u.trgt.target_param);
		break;
	case C_TRGT_SHOW:
		err = ki->param_get(req->tid, req->sid, key_target,
				    req->u.trgt.target_param);
		break;
        case C_TRGT_REDIRECT:
		err = cops->target_redirect(req->tid, req->u.redir.dest,
						ISCSI_STATUS_TGT_MOVED_TEMP);
		break;

	case C_SESS_NEW:
		break;
	case C_SESS_DEL:
		err = ki->session_destroy(req->tid, req->sid);
		break;
	case C_SESS_UPDATE:
		break;
	case C_SESS_SHOW:
		err = ki->param_get(req->tid, req->sid, key_session,
				    req->u.trgt.session_param);
		break;

	case C_LUNIT_NEW:
		err = cops->lunit_add(req->tid, req->lun, req->u.lunit.args);
		break;
	case C_LUNIT_DEL:
		err = cops->lunit_del(req->tid, req->lun);
		break;
	case C_LUNIT_UPDATE:
	case C_LUNIT_SHOW:
		break;

	case C_CONN_NEW:
		break;
	case C_CONN_DEL:
		err = ki->conn_destroy(req->tid, req->sid, req->cid);
		break;
	case C_CONN_UPDATE:
	case C_CONN_SHOW:
		break;

	case C_ACCT_NEW:
		err = cops->account_add(req->tid, req->u.acnt.auth_dir,
					req->u.acnt.u.user.name,
					req->u.acnt.u.user.pass);
		break;
	case C_ACCT_DEL:
		err = cops->account_del(req->tid, req->u.acnt.auth_dir,
					req->u.acnt.u.user.name);
		break;
	case C_ACCT_LIST:
		*rsp_data = malloc(req->u.acnt.u.list.alloc_len);
		if (!*rsp_data) {
			err = -ENOMEM;
			break;
		}

		*rsp_data_sz = req->u.acnt.u.list.alloc_len;
		memset(*rsp_data, 0x0, *rsp_data_sz);

		err = cops->account_list(req->tid, req->u.acnt.auth_dir,
					 &req->u.acnt.u.list.count,
					 &req->u.acnt.u.list.overflow,
					 *rsp_data, *rsp_data_sz);
		break;
	case C_ACCT_UPDATE:
		break;
	case C_ACCT_SHOW:
		err = cops->account_query(req->tid, req->u.acnt.auth_dir,
					  req->u.acnt.u.user.name,
					  req->u.acnt.u.user.pass);
		break;
	case C_SYS_NEW:
		break;
	case C_SYS_DEL:
		if (!err)
			isns_exit();
		break;
	case C_SYS_UPDATE:
	case C_SYS_SHOW:
		break;
	case C_QLOAD:
		qload = 1;
		break;
	default:
		break;
	}

out:
	rsp->err = err;
}

int ietadm_request_handle(int accept_fd)
{
	struct sockaddr addr;
#ifdef LINUX
	struct ucred cred;
#endif
	int fd, err;
	socklen_t len;
	struct ietadm_req req;
	struct ietadm_rsp rsp;
	struct iovec iov[3];
	void *rsp_data = NULL;
	size_t rsp_data_sz;

	memset(&rsp, 0, sizeof(rsp));
	len = sizeof(addr);
	if ((fd = accept(accept_fd, (struct sockaddr *) &addr, &len)) < 0) {
		if (errno == EINTR)
			err = -EINTR;
		else
			err = -EIO;

		goto out;
	}

#ifdef LINUX
	len = sizeof(cred);
	if ((err = getsockopt(fd, SOL_SOCKET, SO_PEERCRED, (void *) &cred, &len)) < 0) {
		rsp.err = -EPERM;
		goto send;
	}

	if (cred.uid || cred.gid) {
		rsp.err = -EPERM;
		goto send;
	}
#endif

	if ((err = read(fd, &req, sizeof(req))) != sizeof(req)) {
		if (err >= 0)
			err = -EIO;
		goto out;
	}

	ietadm_request_exec(&req, &rsp, &rsp_data, &rsp_data_sz);

#ifdef LINUX
send:
#endif
	iov[0].iov_base = &req;
	iov[0].iov_len = sizeof(req);
	iov[1].iov_base = &rsp;
	iov[1].iov_len = sizeof(rsp);
	iov[2].iov_base = rsp.err ? NULL : rsp_data;
	iov[2].iov_len = iov[2].iov_base ? rsp_data_sz : 0;

	err = writev(fd, iov, 2 + !!iov[2].iov_len);
out:
	if (fd >= 0)
		close(fd);
	if (rsp_data)
		free(rsp_data);

	return err;
}

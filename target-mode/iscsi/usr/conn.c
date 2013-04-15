/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#include "iscsid.h"

#define ISCSI_CONN_NEW		1
#define ISCSI_CONN_EXIT		5

struct connection *conn_alloc(void)
{
	struct connection *conn;

	if (!(conn = malloc(sizeof(*conn))))
		return NULL;

	memset(conn, 0, sizeof(*conn));
	conn->state = STATE_FREE;
	param_set_defaults(conn->session_param, session_keys);
	INIT_LIST_HEAD(&conn->rsp_buf_list);

	return conn;
}

void conn_free(struct connection *conn)
{
	free(conn->initiator);
	free(conn);
}

void conn_take_fd(struct connection *conn, int fd)
{
	int err;

	log_debug(1, "conn_take_fd: %d %u %u %u %" PRIx64,
		  fd, conn->cid, conn->stat_sn, conn->exp_stat_sn, conn->sid.id64);

	err = ki->conn_create(conn->tid, conn->session->sid.id64, conn->cid,
			      conn->stat_sn, conn->exp_stat_sn, fd,
			      conn->session_param[key_header_digest].val,
			      conn->session_param[key_data_digest].val);
	if (err) {
		conn->session->conn_cnt--;
		log_error("unable to create connection %u for session %#" PRIx64 " in target %u: %d",
			conn->cid, conn->session->sid.id64, conn->tid, errno);
	}

	return;
}

void conn_read_pdu(struct connection *conn)
{
	conn->iostate = IOSTATE_READ_BHS;
	conn->buffer = (void *)&conn->req.bhs;
	conn->rwsize = BHS_SIZE;
}

void conn_write_pdu(struct connection *conn)
{
	conn->iostate = IOSTATE_WRITE_BHS;
	memset(&conn->rsp, 0, sizeof(conn->rsp));
	conn->buffer = (void *)&conn->rsp.bhs;
	conn->rwsize = BHS_SIZE;
}

void conn_free_rsp_buf_list(struct connection *conn)
{
	struct buf_segment *seg, *tmp;

	list_for_each_entry_safe(seg, tmp, &conn->rsp_buf_list, entry) {
		list_del(&seg->entry);
		free(seg);
	}

	conn->rsp.datasize = 0;
	conn->rsp.data = NULL;
}

void conn_free_pdu(struct connection *conn)
{
	conn->iostate = IOSTATE_FREE;
	if (conn->req.ahs) {
		free(conn->req.ahs);
		conn->req.ahs = NULL;
	}
	if (conn->rsp.ahs) {
		free(conn->rsp.ahs);
		conn->rsp.ahs = NULL;
	}
	conn_free_rsp_buf_list(conn);
}

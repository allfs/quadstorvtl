/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <errno.h>

#include "iscsid.h"

static struct session *session_alloc(u32 tid)
{
	struct session *session;
	struct target *target = target_find_by_id(tid);

	if (!target)
		return NULL;
	if (!(session = malloc(sizeof(*session))))
		return NULL;
	memset(session, 0, sizeof(*session));

	session->target = target;
	INIT_LIST_HEAD(&session->slist);
	insque(&session->slist, &target->sessions_list);

	return session;
}

struct session *session_find_name(u32 tid, const char *iname, union iscsi_sid sid)
{
	struct session *session;
	struct target *target;

	if (!(target = target_find_by_id(tid)))
		return NULL;

	log_debug(1, "session_find_name: %s %#" PRIx64, iname, sid.id64);
	list_for_each_entry(session, &target->sessions_list, slist) {
		if (!memcmp(sid.id.isid, session->sid.id.isid, 6) &&
		    !strcmp(iname, session->initiator))
			return session;
	}

	return NULL;
}

struct session *session_find_id(u32 tid, u64 sid)
{
	struct session *session;
	struct target *target;

	if (!(target = target_find_by_id(tid)))
		return NULL;

	log_debug(1, "session_find_id: %#" PRIx64, sid);
	list_for_each_entry(session, &target->sessions_list, slist) {
		if (session->sid.id64 == sid)
			return session;
	}

	return NULL;
}

int session_exist(u32 t_tid, u64 t_sid)
{
	struct session_info info;

	memset(&info, 0x0, sizeof(info));

	info.tid = t_tid;
	info.sid = t_sid;

	return !ki->session_info(&info);
}

int session_create(struct connection *conn)
{
	struct session *session;
	char *initiator;
	static u16 tsih = 1;
	int err;

	if (!conn->session) {
		initiator = strdup(conn->initiator);
		if (!initiator)
			return -ENOMEM;

		session = session_alloc(conn->tid);
		if (!session) {
			free(initiator);
			return -ENOMEM;
		}

		session->sid = conn->sid;
		session->sid.id.tsih = tsih;

		while (session_exist(conn->tid, session->sid.id64))
			session->sid.id.tsih++;

		tsih = session->sid.id.tsih + 1;
		session->initiator = initiator;

		conn->session = session;
		session->conn_cnt = 1;
		conn->sid = session->sid;
	} else {
		session = conn->session;
		conn->sid = session->sid;

		if (session_exist(conn->tid, session->sid.id64))
			return 0;
	}

	log_debug(1, "session_create: %#" PRIx64, session->sid.id64);

	err = ki->session_create(conn->tid, session->sid.id64, conn->exp_cmd_sn,
			   conn->max_cmd_sn, session->initiator);
	if (err < 0 && err != -EEXIST) {
		log_error("unable to create session %#" PRIx64 " in target %u: %d",
			session->sid.id64, conn->tid, errno);
		goto out;
	}

	err = ki->param_set(conn->tid, session->sid.id64, key_session, 0,
							conn->session_param);
	if (err < 0)
		log_warning("unable to set parameters for session %#" PRIx64 " in target %u: %d",
			session->sid.id64, conn->tid, errno);

	return 0;
out:
	conn->session = NULL;

	if (session->target) {
		remque(&session->slist);
		--session->target->nr_sessions;
	}

	free(session->initiator);
	free(session);

	return err;
}

int session_remove(struct session *session)
{
	int err;

	log_debug(1, "session_remove: %#"  PRIx64, session->sid.id64);

	err = ki->session_destroy(session->target->tid, session->sid.id64);
	if (err < 0 && err != -ENOENT) {
		log_error("unable to delete session %#" PRIx64 " in target %u: %d",
			session->sid.id64, session->target->tid, errno);
		return err;
	}

	if (session->target) {
		remque(&session->slist);
		--session->target->nr_sessions;
	}

	free(session->initiator);
	free(session);

	return 0;
}

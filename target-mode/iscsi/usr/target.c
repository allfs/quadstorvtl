/*
 * target.c - ietd target handling
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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "iscsid.h"

struct __qelem targets_list = LIST_HEAD_INIT(targets_list);

extern struct pollfd poll_array[POLL_MAX];

static int is_addr_loopback(char *addr)
{
	struct in_addr ia;
	struct in6_addr ia6;

	if (inet_pton(AF_INET, addr, &ia) == 1)
		return !strncmp(addr, "127.", 4);

	if (inet_pton(AF_INET6, addr, &ia6) == 1)
		return IN6_IS_ADDR_LOOPBACK(&ia6);

	return 0;
}

static int is_addr_unspecified(char *addr)
{
	struct in_addr ia;
	struct in6_addr ia6;

	if (inet_pton(AF_INET, addr, &ia) == 1)
		return (ia.s_addr == 0);

	if (inet_pton(AF_INET6, addr, &ia6) == 1)
		return IN6_IS_ADDR_UNSPECIFIED(&ia6);

	return 0;
}

static void target_print_addr(struct connection *conn, char *addr, int family)
{
	char taddr[NI_MAXHOST + NI_MAXSERV + 5];

	/* strip ipv6 zone id */
	addr = strsep(&addr, "%");

	snprintf(taddr, sizeof(taddr),
		(family == AF_INET) ? "%s:%d,1" : "[%s]:%d,1",
							addr, server_port);

	text_key_add(conn, "TargetAddress", taddr);
}

void target_list_build_ifaddrs(struct connection *conn, u32 tid, char *addr,
								int family)
{
	struct ifaddrs *ifaddr, *ifa;
	char if_addr[NI_MAXHOST];

	getifaddrs(&ifaddr);

	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;

		int sa_family = ifa->ifa_addr->sa_family;

		if (sa_family == family) {
			if (getnameinfo(ifa->ifa_addr, (family == AF_INET) ?
						sizeof(struct sockaddr_in) :
						sizeof(struct sockaddr_in6),
						if_addr, sizeof(if_addr),
						NULL, 0, NI_NUMERICHOST))
				continue;

			if (strcmp(addr, if_addr) && !is_addr_loopback(if_addr)
				&& cops->target_allow(tid, ifa->ifa_addr))
				target_print_addr(conn, if_addr, family);
		}
	}

	freeifaddrs(ifaddr);
}

void target_list_build(struct connection *conn, char *name)
{
	struct target *target;
	struct sockaddr_storage ss1, ss2;
	socklen_t slen = sizeof(struct sockaddr_storage);
	char addr1[NI_MAXHOST], addr2[NI_MAXHOST];
	int ret, family, i;

	if (getsockname(conn->fd, (struct sockaddr *) &ss1, &slen)) {
		log_error("getsockname failed: %m");
		return;
	}

	ret = getnameinfo((struct sockaddr *) &ss1, slen, addr1,
				sizeof(addr1), NULL, 0, NI_NUMERICHOST);
	if (ret) {
		log_error("getnameinfo failed: %s",
			(ret == EAI_SYSTEM) ? strerror(errno) :
							gai_strerror(ret));
		return;
	}

	family = ss1.ss_family;

	list_for_each_entry(target, &targets_list, tlist) {
		if (name && strcmp(target->name, name))
			continue;

		if (!isns_scn_allow(target->tid, conn->initiator)
			|| !cops->initiator_allow(target->tid, conn->fd,
							conn->initiator)
			|| !cops->target_allow(target->tid,
						(struct sockaddr *) &ss1))
			continue;

		text_key_add(conn, "TargetName", target->name);

		target_print_addr(conn, addr1, family);

		for (i = 0; i < LISTEN_MAX && poll_array[i].fd; i++) {
			slen = sizeof(struct sockaddr_storage);

			if (getsockname(poll_array[i].fd,
					(struct sockaddr *) &ss2, &slen))
				continue;

			if (getnameinfo((struct sockaddr *) &ss2, slen, addr2,
				sizeof(addr2), NULL, 0, NI_NUMERICHOST))
				continue;

			if (ss2.ss_family != family)
				continue;

			if (is_addr_unspecified(addr2))
				target_list_build_ifaddrs(conn, target->tid,
								addr1, family);
			else if (strcmp(addr1, addr2)
				&& !is_addr_loopback(addr2)
				&& cops->target_allow(target->tid,
						(struct sockaddr *) &ss2))
				target_print_addr(conn, addr2, family);
		}
	}
}

struct target* target_find_by_name(const char *name)
{
	struct target *target;

	list_for_each_entry(target, &targets_list, tlist) {
		if (!strcasecmp(target->name, name))
			return target;
	}

	return NULL;
}

struct target* target_find_by_id(u32 tid)
{
	struct target *target;

	list_for_each_entry(target, &targets_list, tlist) {
		if (target->tid == tid)
			return target;
	}

	return NULL;
}

static void all_accounts_del(u32 tid, int dir)
{
	char name[ISCSI_NAME_LEN], pass[ISCSI_NAME_LEN];

	memset(name, 0, sizeof(name));

	for (;cops->account_query(tid, dir, name, pass) != -ENOENT;
		memset(name, 0, sizeof(name))) {
		cops->account_del(tid, dir, name);
	}

}

int target_del(u32 tid)
{
	struct target *target = target_find_by_id(tid);
	int err;

	if (!target)
		return -ENOENT;

#if 0
	if (!list_empty(&target->sessions_list)) {
		log_warning("%s: target %u still has sessions\n", __FUNCTION__,
			  tid);
		return -EBUSY;
	}
#endif

	err = ki->target_destroy(tid);
	if (err < 0) {
		log_error("unable to delete target %u: %d", tid, errno);
		return err;
	}

	remque(&target->tlist);

	all_accounts_del(tid, AUTH_DIR_INCOMING);
	all_accounts_del(tid, AUTH_DIR_OUTGOING);

	isns_target_deregister(target->name);
	free(target);

	return 0;
}

int target_rename(u32 tid, char *name)
{
	struct target *target;

	if (!name)
		return -EINVAL;

	target = target_find_by_id(tid);
	if (!target)
		return -EINVAL;

	if (strcmp(target->name, name) == 0)
		return 0;

	isns_target_deregister(target->name);
	memcpy(target->name, name, sizeof(target->name) - 1);
	isns_target_register(name);
	return 0;
}

int target_add(u32 *tid, char *name)
{
	struct target *target;
	int err;
	struct target *old_target;
	struct target *prev_target = NULL;

	if (!name)
		return -EINVAL;

	if (!(target = malloc(sizeof(*target))))
		return -ENOMEM;

	memset(target, 0, sizeof(*target));
	memcpy(target->name, name, sizeof(target->name) - 1);

	if ((err = ki->target_create(tid, name)) < 0) {
		log_warning("can't create a target %d %u\n", errno, *tid);
		goto out;
	}

	INIT_LIST_HEAD(&target->tlist);
	INIT_LIST_HEAD(&target->sessions_list);
	INIT_LIST_HEAD(&target->isns_head);
	target->tid = *tid;

	list_for_each_entry(old_target, &targets_list, tlist) {
		if (old_target->tid < target->tid)
		{
			prev_target = old_target;
			continue;
		}
		if (prev_target == NULL)
		{
			insque(&target->tlist, &targets_list);
		}
		else
		{
			insque(&target->tlist, &prev_target->tlist);
		}
		isns_target_register(name);
		return 0;
	}	
	list_add_tail(&target->tlist, &targets_list);

	isns_target_register(name);

	return 0;
out:
	free(target);
	return err;
}

int target_redirected(struct target *target, struct connection *conn, struct sockaddr *sa)
{
	char tmp[NI_MAXHOST + 1];
	char addr[NI_MAXHOST + 3];
	char redirect[NI_MAXHOST + NI_MAXSERV + 4];
	char *p;

	if (!strlen(target->redirect.addr))
		return 0;

	if (getnameinfo(sa, (sa->sa_family == AF_INET) ?
					sizeof(struct sockaddr_in) :
					sizeof(struct sockaddr_in6),
					tmp, sizeof(tmp), NULL, 0,
					NI_NUMERICHOST))
		return 0;

	if ((p = strrchr(tmp, '%')))
		*p = '\0';

	if (sa->sa_family == AF_INET6)
		snprintf(addr, sizeof(addr), "[%s]", tmp);
	else
		snprintf(addr, sizeof(addr), "%s", tmp);

	snprintf(redirect, sizeof(redirect), "%s:%s", target->redirect.addr,
		strlen(target->redirect.port) ? target->redirect.port : "3260");

	if (strcmp(target->redirect.addr, addr)) {
		text_key_add(conn, "TargetAddress", redirect);
		return 1;
	}

	return 0;
}

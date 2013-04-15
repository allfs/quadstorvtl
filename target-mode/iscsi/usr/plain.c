/*
 * plain.c - ietd plain file-based configuration
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

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <netdb.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netinet/ip.h>
#include <arpa/inet.h>

#include "iscsid.h"

#define BUFSIZE		4096
#define CONFIG_FILE	"ietd.conf"
#define CONFIG_DIR	"/quadstor/etc/iet/"
#define INI_ALLOW_FILE	"initiators.allow"
#define INI_DENY_FILE	"initiators.deny"
#define TGT_ALLOW_FILE	"targets.allow"

/*
 * Account configuration code
 */

struct user {
	struct __qelem ulist;

	u32 tid;
	char *name;
	char *password;
};

int is_addr_valid(char *addr)
{
	struct in_addr ia;
	struct in6_addr ia6;
	char tmp[NI_MAXHOST + 1], *p = tmp, *q;

	snprintf(tmp, sizeof(tmp), "%s", addr);

	if (inet_pton(AF_INET, p, &ia) == 1)
		return 1;

	if (*p == '[') {
		p++;
		q = p + strlen(p) - 1;
		if (*q != ']')
			return 0;
		*q = '\0';
	}

	if (inet_pton(AF_INET6, p, &ia6) == 1)
		return 1;

	return 0;
}

/* this is the orignal Ardis code. */
static char *target_sep_string(char **pp)
{
	char *p = *pp;
	char *q;

	for (p = *pp; isspace(*p); p++)
		;
	for (q = p; *q && !isspace(*q); q++)
		;
	if (*q)
		*q++ = 0;
	else
		p = NULL;
	*pp = q;
	return p;
}

static struct iscsi_key user_keys[] = {
	{"IncomingUser",},
	{"OutgoingUser",},
	{NULL,},
};

static struct __qelem discovery_users_in = LIST_HEAD_INIT(discovery_users_in);
static struct __qelem discovery_users_out = LIST_HEAD_INIT(discovery_users_out);

#define HASH_ORDER	4
#define acct_hash(x)	((x) & ((1 << HASH_ORDER) - 1))

static struct __qelem trgt_acct_in[1 << HASH_ORDER];
static struct __qelem trgt_acct_out[1 << HASH_ORDER];

static struct __qelem *account_list_get(u32 tid, int dir)
{
	struct __qelem *list = NULL;

	if (tid) {
		list = (dir == AUTH_DIR_INCOMING) ?
			&trgt_acct_in[acct_hash(tid)] : &trgt_acct_out[acct_hash(tid)];
	} else
		list = (dir == AUTH_DIR_INCOMING) ?
			&discovery_users_in : &discovery_users_out;

	return list;
}

/* Return the first account if the length of name is zero */
static struct user *account_lookup_by_name(u32 tid, int dir, char *name)
{
	struct __qelem *list = account_list_get(tid, dir);
	struct user *user = NULL;

	list_for_each_entry(user, list, ulist) {
		fprintf(stderr, "%u %s %s\n", user->tid, user->password, user->name);
		if (user->tid != tid)
			continue;
		if (!strlen(name))
			return user;
		if (!strcmp(user->name, name))
			return user;
	}

	return NULL;
}

static int plain_account_query(u32 tid, int dir, char *name, char *pass)
{
	struct user *user;

	if (!(user = account_lookup_by_name(tid, dir, name)))
		return -ENOENT;

	if (!strlen(name))
		strncpy(name, user->name, ISCSI_NAME_LEN);

	strncpy(pass, user->password, ISCSI_NAME_LEN);

	return 0;
}

static int plain_account_list(u32 tid, int dir, u32 *cnt, u32 *overflow,
			      char *buf, size_t buf_sz)
{
	struct __qelem *list = account_list_get(tid, dir);
	struct user *user;

	*cnt = *overflow = 0;

	if (!list)
		return -ENOENT;

	list_for_each_entry(user, list, ulist) {
		if (user->tid != tid)
			continue;
		if (buf_sz >= ISCSI_NAME_LEN) {
			strncpy(buf, user->name, ISCSI_NAME_LEN);
			buf_sz -= ISCSI_NAME_LEN;
			buf += ISCSI_NAME_LEN;
			*cnt += 1;
		} else
			*overflow += 1;
	}

	return 0;
}

static void account_destroy(struct user *user)
{
	if (!user)
		return;
	remque(&user->ulist);
	free(user->name);
	free(user->password);
	free(user);
}

static int plain_account_del(u32 tid, int dir, char *name)
{
	struct user *user;

	if (!name || !(user = account_lookup_by_name(tid, dir, name)))
		return -ENOENT;

	account_destroy(user);

	/* update the file here. */
	return 0;
}

static struct user *account_create(void)
{
	struct user *user;

	if (!(user = malloc(sizeof(*user))))
		return NULL;

	memset(user, 0, sizeof(*user));
	INIT_LIST_HEAD(&user->ulist);

	return user;
}

static int plain_account_add(u32 tid, int dir, char *name, char *pass)
{
	int err = -ENOMEM;
	struct user *user;
	struct __qelem *list;

	if (!name || !pass)
		return -EINVAL;

	if (tid) {
		/* check here */
/* 		return -ENOENT; */
	}

	if (!(user = account_create()) ||
	    !(user->name = strdup(name)) ||
	    !(user->password = strdup(pass)))
		goto out;

	user->tid = tid;
	list = account_list_get(tid, dir);
	if (dir == AUTH_DIR_OUTGOING) {
		struct user *old, *tmp;
		list_for_each_entry_safe(old, tmp, list, ulist) {
			if (tid != old->tid)
				continue;
			log_warning("Only one outgoing %s account is supported."
				    " Replacing the old one.\n",
				    tid ? "target" : "discovery");
			account_destroy(old);
		}
	}

	insque(user, list);

	/* update the file here. */
	return 0;
out:
	account_destroy(user);

	return err;
}

/*
 * Access control code
 */

static int netmask_match_v6(struct sockaddr *sa1, struct sockaddr *sa2, uint32_t mbit)
{
	uint16_t mask, a1[8], a2[8];
	int i;

	for (i = 0; i < 8; i++) {
		a1[i] = ntohs(((struct sockaddr_in6 *) sa1)->sin6_addr.s6_addr16[i]);
		a2[i] = ntohs(((struct sockaddr_in6 *) sa2)->sin6_addr.s6_addr16[i]);
	}

	for (i = 0; i < mbit / 16; i++)
		if (a1[i] ^ a2[i])
			return 0;

	if (mbit % 16) {
		mask = ~((1 << (16 - (mbit % 16))) - 1);
		if ((mask & a1[mbit / 16]) ^ (mask & a2[mbit / 16]))
			return 0;
	}

	return 1;
}

static int netmask_match_v4(struct sockaddr *sa1, struct sockaddr *sa2, uint32_t mbit)
{
	uint32_t s1, s2, mask = ~((1 << (32 - mbit)) - 1);

	s1 = htonl(((struct sockaddr_in *) sa1)->sin_addr.s_addr);
	s2 = htonl(((struct sockaddr_in *) sa2)->sin_addr.s_addr);

	if (~mask & s1)
		return 0;

	if (!((mask & s2) ^ (mask & s1)))
		return 1;

	return 0;
}

static int netmask_match(struct sockaddr *sa1, struct sockaddr *sa2, char *buf)
{
	uint32_t mbit;
	uint8_t family = sa1->sa_family;

	mbit = strtoul(buf, NULL, 0);
	if (mbit < 0 ||
	    (family == AF_INET && mbit > 31) ||
	    (family == AF_INET6 && mbit > 127))
		return 0;

	if (family == AF_INET)
		return netmask_match_v4(sa1, sa2, mbit);

	return netmask_match_v6(sa1, sa2, mbit);
}

static int address_match(struct sockaddr *sa1, struct sockaddr *sa2)
{
	if (sa1->sa_family == AF_INET)
		return ((struct sockaddr_in *) sa1)->sin_addr.s_addr ==
			((struct sockaddr_in *) sa2)->sin_addr.s_addr;
	else {
		struct in6_addr *a1, *a2;

		a1 = &((struct sockaddr_in6 *) sa1)->sin6_addr;
		a2 = &((struct sockaddr_in6 *) sa2)->sin6_addr;

		return (a1->s6_addr32[0] == a2->s6_addr32[0] &&
			a1->s6_addr32[1] == a2->s6_addr32[1] &&
			a1->s6_addr32[2] == a2->s6_addr32[2] &&
			a1->s6_addr32[3] == a2->s6_addr32[3]);
	}

	return 0;
}

static int initiator_match(char *initiator, char *p)
{
	int match = 0;
	char pattern[strlen(p) + 3];
	regex_t re;

	snprintf(pattern, sizeof(pattern), "^%s$", p);

	if (regcomp(&re, pattern, REG_NOSUB))
		return 0;

	match = !regexec(&re, initiator, (size_t) 0, NULL, 0);

	regfree(&re);

	return match;
}

static int __match(struct sockaddr *sa, char *initiator, char *str)
{
	struct addrinfo hints, *res;
	char *p, *q;
	int match = 0;

	while ((p = strsep(&str, ","))) {
		while (isspace(*p))
			p++;

		if (!*p)
			continue;

		q = p + (strlen(p) - 1);

		while (isspace(*q))
			*(q--) = '\0';

		if (!strcmp(p, "ALL"))
			return 1;

		if (initiator)
			if (initiator_match(initiator, p))
				return 1;

		if (*p == '[') {
			p++;
			if (!(q = strchr(p, ']')))
				return 0;
			*(q++) = '\0';
		} else
			q = p;

		if ((q = strchr(q, '/')))
			*(q++) = '\0';

		memset(&hints, 0, sizeof(hints));
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_NUMERICHOST;

		if (getaddrinfo(p, NULL, &hints, &res) == 0) {
			if (q)
				match = netmask_match(res->ai_addr, sa, q);
			else
				match = address_match(res->ai_addr, sa);

			freeaddrinfo(res);
		}

		if (match)
			break;
	}

	return match;
}

static int match(u32 tid, struct sockaddr *sa, char *initiator, char *filename)
{
	int err = -EPERM;
	FILE *fp;
	char buf[BUFSIZE], fname[PATH_MAX], *p, *q;

	snprintf(fname, sizeof(fname), "%s%s", CONFIG_DIR, filename);
	if (!(fp = fopen(fname, "r"))) {
		if (errno != ENOENT)
			return -errno;

		snprintf(fname, sizeof(fname), "%s%s", "/quadstor/etc/", filename);
		fp = fopen(fname, "r");
		if (!fp)
			return -errno;
	}

	/*
	 * Every time we are called, we read the file. So we don't need to
	 * implement 'reload feature'. It's slow, however, it doesn't matter.
	 */
	while ((p = fgets(buf, sizeof(buf), fp))) {
		struct target *target;

		q = &buf[strlen(buf) - 1];

		if (*q != '\n')
			continue;
		else
			*q = '\0';

		q = p;

		p = target_sep_string(&q);

		if (!p || *p == '#')
			continue;

		if (strcmp(p, "ALL")) {
			if (!(target = target_find_by_name(p)))
				continue;
			if (target->tid != tid)
				continue;
		}

		if (__match(sa, initiator, q))
			err = 0;

		break;
	}

	fclose(fp);
	return err;
}

static int plain_initiator_allow(u32 tid, int fd, char *initiator)
{
	struct sockaddr_storage ss;
	socklen_t slen = sizeof(struct sockaddr_storage);
	int allow, deny;

	memset(&ss, 0, sizeof(ss));

	getpeername(fd, (struct sockaddr *) &ss, &slen);

	allow = match(tid, (struct sockaddr *) &ss, initiator,
						INI_ALLOW_FILE);
	deny = match(tid, (struct sockaddr *) &ss, initiator,
						INI_DENY_FILE);

	if (deny != -ENOENT) {
		if (!deny && allow)
			return 0;
		else
			return 1;
	}

	return (allow == -ENOENT) ? 1 : !allow;
}

static int plain_target_allow(u32 tid, struct sockaddr *sa)
{
	int allow;

	allow = match(tid, sa, NULL, TGT_ALLOW_FILE);

	return (allow == -ENOENT) ? 1 : !allow;
}

/*
 * Main configuration code
 */

static int __plain_target_redirect(u32 tid, char *dest, u8 type, int update)
{
	struct target *target;
	char *a = dest, *p = dest + strlen(dest);

	if (!tid || !dest || !type)
		return -EINVAL;

	if (type != ISCSI_STATUS_TGT_MOVED_TEMP &&
			type != ISCSI_STATUS_TGT_MOVED_PERM)
		return -EINVAL;

	target = target_find_by_id(tid);
	if (!target) {
		log_error("can't find target: %d", tid);
		return -ENOENT;
	}

	while(isspace(*a))
		a++;

	if (strlen(a)) {
		while(isspace(*(--p)))
			*p = '\0';

		if ((p = strrchr(a, ']')))
			p = strchrnul(p, ':');
		else
			p = strchrnul(a, ':');

		if (*p) {
			*(p++) = '\0';
			if (!atoi(p))
				return -EINVAL;
		}

		if (!is_addr_valid(a))
			return -EINVAL;

		log_warning("target %s %s redirected to %s:%s", target->name,
			(type == ISCSI_STATUS_TGT_MOVED_TEMP) ? "temporarily" :
								"permanently",
						a, strlen(p) ? p : "3260");
	} else
		log_warning("target redirection for %s cleared", target->name);

	target->redirect.type = type;

	snprintf(target->redirect.addr, sizeof(target->redirect.addr), "%s", a);
	snprintf(target->redirect.port, sizeof(target->redirect.port), "%s", p);

	return 0;
}

static int plain_target_redirect(u32 tid, char *dest, u8 type)
{
	return __plain_target_redirect(tid, dest, type, 1);
}

static int plain_target_rename(u32 tid, char *name)
{
	return target_rename(tid, name);
}

static int __plain_target_create(u32 *tid, char *name, int update)
{
	int err;

	if (target_find_by_name(name)) {
		log_error("duplicated target %s", name);
		return -EINVAL;
	}
	if ((err = target_add(tid, name)) < 0)
		return err;

	if (update)
		; /* Update the config file here. */

	return err;
}

static int plain_target_create(u32 *tid, char *name)
{
	return __plain_target_create(tid, name, 1);
}

static int plain_target_destroy(u32 tid)
{
	int err;

	if ((err = target_del(tid)) < 0)
		return err;

	/* Update the config file here. */
	return err;
}

static int __plain_lunit_create(u32 tid, u32 lun, char *args, int update)
{
	int err;

	err = ki->lunit_create(tid, lun, args);
	if (err < 0) {
		log_error("unable to create logical unit %u in target %u: %d",
			lun, tid, errno);
		return err;
	}

	if (update)
		;

	return err;
}

static int plain_lunit_create(u32 tid, u32 lun, char *args)
{
	return __plain_lunit_create(tid, lun, args, 1);
}

static int plain_lunit_destroy(u32 tid, u32 lun)
{
	int err;

	err = ki->lunit_destroy(tid, lun);
	if (err < 0)
		log_error("unable to delete logical unit %u in target %u: %d",
			lun, tid, errno);

	return err;
}

static int __plain_param_set(u32 tid, u64 sid, int type,
			   u32 partial, struct iscsi_param *param, int update)
{
	int err;

	err = ki->param_set(tid, sid, type, partial, param);
	if (err < 0) {
		log_error ("unable to set parameter (%d:%u) of session %#" PRIx64 " in target %u: %d",
			type, partial, sid, tid, errno);
		return err;
	}

	if (update)
		;

	return err;
}

static int plain_param_set(u32 tid, u64 sid, int type,
			   u32 partial, struct iscsi_param *param)
{
	return __plain_param_set(tid, sid, type, partial, param, 1);
}

static int iscsi_param_partial_set(u32 tid, u64 sid, int type, int key, u32 val)
{
	struct iscsi_param *param;
	struct iscsi_param session_param[session_key_last];
	struct iscsi_param target_param[target_key_last];

	if (type == key_session)
		param = session_param;
	else
		param = target_param;

	memset(param, 0x0, (type == key_session) ?
	       sizeof(session_param) :
	       sizeof(target_param));

	param[key].val = val;

	return __plain_param_set(tid, sid, type, 1 << key, param, 0);
}

static void plain_portal_init(FILE *fp, int *timeout)
{
	char buf[BUFSIZE];
	char *p, *q;
	char *isns = NULL;
	int isns_ac = 0;

	while (fgets(buf, BUFSIZE, fp)) {
		q = buf;
		p = target_sep_string(&q);
		if (!p || *p == '#')
			continue;
		if (!strcasecmp(p, "iSNSServer")) {
			if (isns)
				free(isns);
			isns = strdup(target_sep_string(&q));
		} else if (!strcasecmp(p, "iSNSAccessControl")) {
			char *str = target_sep_string(&q);
			if (!strcasecmp(str, "Yes"))
				isns_ac = 1;
		}
	}

	if (isns) {
		*timeout = isns_init(isns, isns_ac);
		free(isns);
	}
}

void plain_target_init(FILE *fp)
{
	char buf[BUFSIZE];
	char *p, *q;
	int idx;
	u32 tid, val;

	tid = 0;
	while (fgets(buf, BUFSIZE, fp)) {
		q = buf;
		p = target_sep_string(&q);
		if (!p || *p == '#')
			continue;
		if (!strcasecmp(p, "Target")) {
			tid = 0;
			if (!(p = target_sep_string(&q)))
				continue;
			__plain_target_create(&tid, p, 0);
		} else if (!strcasecmp(p, "Alias") && tid) {
			;
		} else if (!strcasecmp(p, "MaxSessions") && tid) {
			struct target * const target =
				target_find_by_id(tid);
			if (target)
				target->max_nr_sessions = strtol(q, &q, 0);
			else
				log_warning("Target '%s' not found", q);
		} else if (!strcasecmp(p, "Lun") && tid) {
			u32 lun = strtol(q, &q, 10);
			__plain_lunit_create(tid, lun, q, 0);
		} else if (!((idx = param_index_by_name(p, target_keys)) < 0) && tid) {
			val = strtol(q, &q, 0);
			if (param_check_val(target_keys, idx, &val) < 0)
				log_warning("%s, %u\n", target_keys[idx].name, val);
			iscsi_param_partial_set(tid, 0, key_target, idx, val);
		} else if (!((idx = param_index_by_name(p, session_keys)) < 0) && tid) {
			char *str = target_sep_string(&q);
			if (param_str_to_val(session_keys, idx, str, &val) < 0)
				continue;
			if (param_check_val(session_keys, idx, &val) < 0)
				log_warning("%s, %u\n", session_keys[idx].name, val);
			iscsi_param_partial_set(tid, 0, key_session, idx, val);
		}
	}

	return;
}

static void plain_account_init(FILE *fp)
{
	char buf[BUFSIZE], *p, *q;
	u32 tid;
	int idx;

	tid = 0;
	while (fgets(buf, sizeof(buf), fp)) {
		q = buf;
		p = target_sep_string(&q);
		if (!p || *p == '#')
			continue;

		if (!strcasecmp(p, "Target")) {
			struct target *target;
			tid = 0;
			if (!(p = target_sep_string(&q)))
				continue;

			target = target_find_by_name(p);
			if (target)
				tid = target->tid;
		} else if (!((idx = param_index_by_name(p, user_keys)) < 0)) {
			char *name, *pass;
			name = target_sep_string(&q);
			pass = target_sep_string(&q);

			if (plain_account_add(tid, idx, name, pass) < 0)
				fprintf(stderr, "%s %s\n", name, pass);
		}
	}

	return;
}

static void plain_init(char *params, int *timeout)
{
	FILE *fp;
	char file1[PATH_MAX], file2[PATH_MAX];
	int i;

	for (i = 0; i < 1 << HASH_ORDER; i++) {
		INIT_LIST_HEAD(&trgt_acct_in[i]);
		INIT_LIST_HEAD(&trgt_acct_out[i]);
	}

	snprintf(file1, sizeof(file1), "%s%s", CONFIG_DIR, CONFIG_FILE);
	snprintf(file2, sizeof(file2), "/quadstor/etc/%s", CONFIG_FILE);

	if (!(fp = fopen(params ? params : file1, "r"))) {
		if ((fp = fopen(file2, "r")))
			log_warning("%s's location is depreciated and will be moved in the next release to %s", file2, file1);
		else {
			return;
		}
	}

	plain_portal_init(fp, timeout);

	rewind(fp);

	plain_account_init(fp);

	fclose(fp);

	return;
}

struct config_operations plain_ops = {
	.init			= plain_init,
	.target_add		= plain_target_create,
	.target_rename		= plain_target_rename,
	.target_del		= plain_target_destroy,
	.lunit_add		= plain_lunit_create,
	.lunit_del		= plain_lunit_destroy,
	.param_set		= plain_param_set,
	.account_add		= plain_account_add,
	.account_del		= plain_account_del,
	.account_query		= plain_account_query,
	.account_list		= plain_account_list,
	.initiator_allow	= plain_initiator_allow,
	.target_allow		= plain_target_allow,
	.target_redirect	= plain_target_redirect,
};

/*
 * Event notification code.
 * (C) 2005 FUJITA Tomonori <tomof@acm.org>
 * This code is licenced under the GPL.
 *
 * Some functions are based on open-iscsi code
 * written by Dmitry Yusupov, Alex Aizman.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/socket.h>

#include "iscsid.h"

#ifdef LINUX
static struct sockaddr_nl src_addr, dest_addr;

static int nl_write(int fd, void *data, int len)
{
	struct iovec iov[2];
	struct msghdr msg;
	struct nlmsghdr nlh = {0};

	iov[0].iov_base = &nlh;
	iov[0].iov_len = sizeof(nlh);
	iov[1].iov_base = data;
	iov[1].iov_len = NLMSG_SPACE(len) - sizeof(nlh);

	nlh.nlmsg_len = NLMSG_SPACE(len);
	nlh.nlmsg_pid = getpid();
	nlh.nlmsg_flags = 0;
	nlh.nlmsg_type = 0;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name= (void*)&dest_addr;
	msg.msg_namelen = sizeof(dest_addr);
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;

	return sendmsg(fd, &msg, 0);
}

static int nl_read(int fd, void *data, int len)
{
	struct iovec iov[2];
	struct msghdr msg;
	struct nlmsghdr nlh;

	iov[0].iov_base = &nlh;
	iov[0].iov_len = sizeof(nlh);
	iov[1].iov_base = data;
	iov[1].iov_len = len;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name= (void*)&src_addr;
	msg.msg_namelen = sizeof(src_addr);
	msg.msg_iov = iov;
	msg.msg_iovlen = 2;

	return recvmsg(fd, &msg, MSG_DONTWAIT);
}

void handle_iscsi_events(int fd)
{
	struct session *session;
	struct iet_event event;
	int res;

retry:
	if ((res = nl_read(fd, &event, sizeof(event))) < 0) {
		if (errno == EAGAIN)
			return;
		if (errno == EINTR)
			goto retry;
		log_error("read netlink fd (%d)", errno);
		exit(1);
	}

	log_debug(1, "conn %u session %llu target %u, state %u",
		  event.cid, event.sid, event.tid, event.state);

	switch (event.state) {
	case E_CONN_CLOSE:
		if (!(session = session_find_id(event.tid, event.sid)))
			/* session previously closed for reinstatement? */
			break;

		if (--session->conn_cnt <= 0)
			session_remove(session);
		break;
	default:
		log_error("%s(%d) %u\n", __FUNCTION__, __LINE__, event.state);
		exit(-1);
		break;
	}
}

int nl_open(void)
{
	int nl_fd, res;

	nl_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_IET);
	if (nl_fd == -1) {
		log_error("%s %d\n", __FUNCTION__, errno);
		return -1;
	}

	memset(&src_addr, 0, sizeof(src_addr));
	src_addr.nl_family = AF_NETLINK;
	src_addr.nl_pid = getpid();
	src_addr.nl_groups = 0; /* not in mcast groups */

	memset(&dest_addr, 0, sizeof(dest_addr));
	dest_addr.nl_family = AF_NETLINK;
	dest_addr.nl_pid = 0; /* kernel */
	dest_addr.nl_groups = 0; /* unicast */

	res = nl_write(nl_fd, NULL, 0);
	if (res < 0) {
		log_error("%s %d\n", __FUNCTION__, res);
		close(nl_fd);
		return res;
	}

	return nl_fd;
}
#else

#include <sys/mman.h>
#include <pthread.h>

uint8_t *mbuf;
int page_size;
int page_shift;
int kcomm_pages;
int kcomm_msg_per_page;
int kcomm_max_msgs;
int kring_idx;

struct iet_event *
kcomm_next_msg(int idx)
{
	int page_idx;
	int page_offset;
	struct iet_event *ret;

	page_idx = idx / kcomm_msg_per_page;
	page_offset = idx % kcomm_msg_per_page;

	ret = (struct iet_event *)(mbuf + ((page_idx * page_size) + (sizeof(struct iet_event) * page_offset)));
	return ret;
}

void handle_iscsi_events(int fd)
{
	struct iet_event *event;
	struct session *session;

	event = kcomm_next_msg(kring_idx);
	if (!event->busy)
	{
		return;
	}

	kring_idx++;
	if (kring_idx == kcomm_max_msgs)
		kring_idx = 0;

	log_debug(1, "conn %u session %llu target %u, state %u",
		  event->cid, (unsigned long long)event->sid, event->tid, event->state);

	switch (event->state) {
	case E_CONN_CLOSE:
		if (!(session = session_find_id(event->tid, event->sid)))
			/* session previously closed for reinstatement? */
			break;

		if (--session->conn_cnt <= 0)
			session_remove(session);
		break;
	default:
		log_error("%s(%d) %u\n", __FUNCTION__, __LINE__, event->state);
		exit(-1);
		break;
	}
	event->busy = 0;
}

int nl_open(void)
{
	int nl_fd;

	nl_fd = open(CTL_DEVICE, O_RDWR);
	if (nl_fd < 0)
	{
		log_error("%s %d\n", __FUNCTION__, errno);
		return -1;
	}

	/* right now only writes from kernel, so PROT_WRITE is unnecessary */
	mbuf = mmap(NULL, IET_MMAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, nl_fd, 0);
	if (mbuf == MAP_FAILED)
	{
		log_error("%s %d\n", __FUNCTION__, errno);
		return -1;
	}

	page_size = sysconf(_SC_PAGESIZE);
	for (page_shift = 0;; page_shift++)
	{
		if (1UL << page_shift == page_size)
			break;
	}

	kcomm_pages = IET_MMAP_SIZE >> page_shift;
	kcomm_msg_per_page = page_size/sizeof(struct iet_event);
	kcomm_max_msgs = kcomm_pages * kcomm_msg_per_page;
	return nl_fd;
}
#endif

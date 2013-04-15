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

#include <apicommon.h>
#include <messages.h>
#include <netdb.h>

#define DO_READ  0x01
#define DO_WRITE 0x02
#define TIMEOUT_RETRIES		150

static int
select_poll(int fd, int rw)
{
	fd_set fdset;
	int ret;
	struct timeval timeout;

	FD_ZERO(&fdset);
	FD_SET(fd, &fdset);

	timeout.tv_sec = 0;
	timeout.tv_usec = 100000;

	if (rw == DO_READ)
		ret = select(fd+1, &fdset, NULL, NULL, &timeout);
	else
		ret = select(fd+1, NULL, &fdset, NULL, &timeout);

	if (ret < 0)
		return -1;
	return 0;
}

static int
do_read(int fd, char *buf, int nbytes, int retries)
{
	int bytes_read = 0;
	int status = 0;

	do {
		status = read(fd, buf, nbytes);
		if (status < 0 && errno	!= EAGAIN)
			return bytes_read;

		if (status < 0) {
			int retval;
			retval = select_poll(fd, DO_READ);
			if (retval < 0)
				break;
			if (retries) {
				retries--;
				if (!retries)
					return bytes_read;
			}
		}
		else if (!status) {
			break;
		}
		else {
			bytes_read += status;
			nbytes -= status;
			buf = buf+status;
		}
	} while (status != 0 && nbytes);

	return bytes_read;
}

static int 
do_write(int fd, char *buf, int nbytes, int retries)
{
	int bytes_written = 0;
	int status;

	do
	{
		status = write(fd, buf, nbytes);
		if (status < 0 && errno != EAGAIN)
			return bytes_written;

		if (status < 0) {
			int retval;
			retval = select_poll(fd, DO_WRITE);
			if (retval < 0)
				break;

			if (retries) {
				retries--;
				if (!retries)
					return bytes_written;
			}
		}
		else if (!status) {
			break;
		}
		else {
			bytes_written += status;
			nbytes -= status;
			buf = buf+status;
		}
	} while (status != 0 && nbytes);

	return bytes_written;
}

void
tl_msg_free_data(struct tl_msg *msg)
{
	if (msg->msg_data)
	{
		free(msg->msg_data);
		msg->msg_data = NULL;
	}
	msg->msg_len = 0;
}

void
tl_msg_free_message(struct tl_msg *msg)
{
	tl_msg_free_data(msg);
	free(msg);
}

struct tl_msg *
tl_msg_recv_message(struct tl_comm *comm)
{
	struct tl_msg *msg;
	int status;
	struct tl_msg recv;

	msg = malloc(sizeof(struct tl_msg));
	if (!msg)
		return NULL;

	memset(msg, 0, sizeof(struct tl_msg));
	status = do_read(comm->sockfd, (char *)&recv, offsetof(struct tl_msg, msg_data), 0);
	if (status != offsetof(struct tl_msg, msg_data)) {
		free(msg);
		return NULL;
	}

	msg->msg_id = ntohl(recv.msg_id);
	msg->msg_len = ntohl(recv.msg_len);
	msg->msg_resp = ntohl(recv.msg_resp);

	if (msg->msg_len) {
		msg->msg_data = malloc(msg->msg_len + 1);
		if (!msg->msg_data) {
			tl_msg_free_message(msg);
			return NULL;
		}
		status = do_read(comm->sockfd, msg->msg_data, msg->msg_len, 0);
		if (status != msg->msg_len) {
			tl_msg_free_message(msg);
			return NULL;
		}
		msg->msg_data[msg->msg_len] = 0;
	}

	return msg;
}

struct tl_msg *
tl_msg_recv_message_timeout(struct tl_comm *comm)
{
	struct tl_msg *msg;
	int status;
	struct tl_msg recv;

	msg = malloc(sizeof(struct tl_msg));
	if (!msg)
		return NULL;

	memset(msg, 0, sizeof(struct tl_msg));
	status = do_read(comm->sockfd, (char *)&recv, offsetof(struct tl_msg, msg_data), TIMEOUT_RETRIES);
	if (status != offsetof(struct tl_msg, msg_data)) {
		free(msg);
		return NULL;
	}

	msg->msg_id = ntohl(recv.msg_id);
	msg->msg_len = ntohl(recv.msg_len);
	msg->msg_resp = ntohl(recv.msg_resp);

	if (msg->msg_len) {
		msg->msg_data = malloc(msg->msg_len + 1);
		if (!msg->msg_data) {
			tl_msg_free_message(msg);
			return NULL;
		}
		status = do_read(comm->sockfd, msg->msg_data, msg->msg_len, TIMEOUT_RETRIES);
		if (status != msg->msg_len) {
			tl_msg_free_message(msg);
			return NULL;
		}
		msg->msg_data[msg->msg_len] = 0;
	}

	return msg;
}

void
tl_msg_free_connection(struct tl_comm *tl_comm)
{
	tl_msg_close_connection(tl_comm);
	free(tl_comm);
}

void
tl_msg_close_connection(struct tl_comm *tl_comm)
{
	close(tl_comm->sockfd);
}

static int
do_connect(int sockfd, struct sockaddr *addr, socklen_t len)
{
	int status;
	int tries = 5;

again:
	status = connect(sockfd, addr, len);
	if (!status) {
		return 0;
	}
	else if (errno != ECONNREFUSED || !tries) {
		DEBUG_ERR("failed to connect errno %d %s\n", errno, strerror(errno));
		return -1;
	}
	tries--;
	sleep(1);
	goto again;
}

struct tl_comm *
tl_msg_make_connection(void)
{
	struct tl_comm *tl_comm;
	struct sockaddr_un un_addr;
	int sockfd;
	int status;

	tl_comm = malloc(sizeof(struct tl_comm));

	if (!tl_comm)
	{
		return NULL;
	}

	sockfd = socket(AF_LOCAL, SOCK_STREAM, 0);
	if (sockfd < 0)
	{
		free(tl_comm);
		return NULL;
	}

	memset(&un_addr, 0, sizeof(struct sockaddr_un));
	un_addr.sun_family = AF_LOCAL;
#ifdef FREEBSD
	memcpy((char *)&un_addr.sun_path, MDAEMON_PATH, strlen(MDAEMON_PATH));
#else
	memcpy((char *)&un_addr.sun_path+1, MDAEMON_PATH, strlen(MDAEMON_PATH));
#endif

	status = do_connect(sockfd, (struct sockaddr *)&un_addr, sizeof(un_addr));
	if (status < 0)
	{
		close(sockfd);
		free(tl_comm);
		return NULL;
	}

	if ((status = fcntl(sockfd, F_GETFL, 0)) != -1)
	{
		status |= O_NONBLOCK;
		fcntl(sockfd, F_SETFL, status);
	}

	tl_comm->sockfd = sockfd;
	return tl_comm;
}

int
tl_msg_send_message(struct tl_comm *tl_comm, struct tl_msg *msg)
{
	int retval;
	struct tl_msg send;

	if (msg->msg_len < 0)
		return -1;

	send.msg_id = htonl(msg->msg_id);
	send.msg_len = htonl(msg->msg_len);
	send.msg_resp = htonl(msg->msg_resp);
	retval = do_write(tl_comm->sockfd, (char *)&send, offsetof(struct tl_msg, msg_data), 0);

	if (retval != offsetof(struct tl_msg, msg_data))
		return -1;

	if (msg->msg_len > 0) {
		retval = do_write(tl_comm->sockfd, msg->msg_data, msg->msg_len, 0);
		if (retval != msg->msg_len)
			return -1;
	}
	return 0;
}

int
tl_msg_send_message_timeout(struct tl_comm *tl_comm, struct tl_msg *msg)
{
	int retval;
	struct tl_msg send;

	if (msg->msg_len < 0)
		return -1;

	send.msg_id = htonl(msg->msg_id);
	send.msg_len = htonl(msg->msg_len);
	send.msg_resp = htonl(msg->msg_resp);
	retval = do_write(tl_comm->sockfd, (char *)&send, offsetof(struct tl_msg, msg_data), TIMEOUT_RETRIES);

	if (retval != offsetof(struct tl_msg, msg_data))
		return -1;

	if (msg->msg_len > 0) {
		retval = do_write(tl_comm->sockfd, msg->msg_data, msg->msg_len, TIMEOUT_RETRIES);
		if (retval != msg->msg_len)
			return -1;
	}
	return 0;
}

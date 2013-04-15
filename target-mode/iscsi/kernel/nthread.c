/*
 * Network thread.
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 * (C) 2008 Arne Redlich <agr@powerkom-dd.de>
 * This code is licenced under the GPL.
 */

#include "iscsi.h"
#include "iscsi_dbg.h"
#include "digest.h"
#include "scdefs.h"

wait_queue_head_t iscsi_ctl_wait;

enum rx_state {
	RX_INIT_BHS, /* Must be zero. */
	RX_BHS,

	RX_INIT_AHS,
	RX_AHS,

	RX_INIT_HDIGEST,
	RX_HDIGEST,
	RX_CHECK_HDIGEST,

	RX_INIT_DATA,
	RX_DATA,

	RX_INIT_DDIGEST,
	RX_DDIGEST,
	RX_CHECK_DDIGEST,

	RX_END,
};

#if 0
void nthread_wakeup(struct iscsi_target *target)
{
	struct network_thread_info *info = &target->nthread_info;

	spin_lock_bh(&info->nthread_lock);
	set_bit(D_DATA_READY, &info->flags);
	wake_up_process(info->task);
	spin_unlock_bh(&info->nthread_lock);
}
#endif

static inline void iscsi_conn_init_read(struct iscsi_conn *conn, void *data, size_t len)
{
	len = (len + 3) & -4; // XXX ???
	conn->read_iov[0].iov_base = data;
	conn->read_iov[0].iov_len = len;
	conn->read_msg.msg_iov = conn->read_iov;
	conn->read_msg.msg_iovlen = 1;
	conn->read_size = (len + 3) & -4;
}

static void iscsi_conn_read_ahs(struct iscsi_conn *conn, struct iscsi_cmnd *cmnd)
{
	cmnd->pdu.ahs = malloc(cmnd->pdu.ahssize, M_IETAHS, M_WAITOK);
	assert(cmnd->pdu.ahs);
	iscsi_conn_init_read(conn, cmnd->pdu.ahs, cmnd->pdu.ahssize);
}

static struct iscsi_cmnd * iscsi_get_send_cmnd(struct iscsi_conn *conn)
{
	struct iscsi_cmnd *cmnd = NULL;

	spin_lock(&conn->list_lock);
	if (!list_empty(&conn->write_list)) {
		cmnd = list_entry(conn->write_list.next, struct iscsi_cmnd, list);
		list_del_init(&cmnd->list);
	}
	spin_unlock(&conn->list_lock);

	return cmnd;
}

extern int is_data_available(struct iscsi_conn *conn);
#ifdef LINUX
int is_data_available(struct iscsi_conn *conn)
{
	int avail, res;
	mm_segment_t oldfs;
	struct socket *sock = conn->sock;

	oldfs = get_fs();
	set_fs(get_ds());
	res = sock->ops->ioctl(sock, SIOCINQ, (unsigned long) &avail);
	set_fs(oldfs);
	return (res >= 0) ? avail : res;
}
#else
int is_data_available(struct iscsi_conn *conn)
{
	struct file filetmp;
	int error, avail = 0;

	filetmp.f_data = conn->sock;
	filetmp.f_cred = NULL;

	error = soo_ioctl(&filetmp, FIONREAD, &avail, NULL, curthread);
	if (error)
		return 0;

	return (avail > 0) ? avail : 0; 
}
#endif

static void forward_iov(struct msghdr *msg, int len)
{
	while (msg->msg_iov->iov_len <= len) {
		len -= msg->msg_iov->iov_len;
		msg->msg_iov++;
		msg->msg_iovlen--;
	}

	msg->msg_iov->iov_base = (char *) msg->msg_iov->iov_base + len;
	msg->msg_iov->iov_len -= len;
}

static int do_recv(struct iscsi_conn *conn, int state)
{
#ifdef LINUX
	mm_segment_t oldfs;
#else
	struct uio uio;
	int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
#endif
	struct msghdr msg;
	struct iovec iov[ISCSI_CONN_IOV_MAX >> 4];
	int i, len, res;

	if (!test_bit(CONN_ACTIVE, &conn->state)) {
		res = -EIO;
		goto out;
	}

	if (is_data_available(conn) <= 0) {
		res = -EAGAIN;
		goto out;
	}

	if (conn->read_state == RX_DATA && conn->read_ctio)
	{
		res = do_recv_ctio(conn, state);
		if (!conn->read_size && state != RX_INIT_DDIGEST)
		{
			conn->read_ctio = NULL;
		}
		return res;
	}

	msg.msg_iov = iov;
	msg.msg_iovlen = min_t(size_t, conn->read_msg.msg_iovlen, ISCSI_CONN_IOV_MAX >> 4);
	for (i = 0, len = 0; i < msg.msg_iovlen; i++) {
		iov[i] = conn->read_msg.msg_iov[i];
		len += iov[i].iov_len;
	}

#ifdef LINUX
	oldfs = get_fs();
	set_fs(get_ds());
	res = sock_recvmsg(conn->sock, &msg, len, MSG_DONTWAIT | MSG_NOSIGNAL);
	set_fs(oldfs);
#else
	uio_fill(&uio, iov, i, len, UIO_READ);
	flags = MSG_DONTWAIT | MSG_NOSIGNAL;
	res = soreceive(conn->sock, NULL, &uio, NULL, NULL, &flags);
	map_result(&res, &uio, len, 1);
#endif

	if (res <= 0) {
		switch (res) {
		case -EAGAIN:
		case -ERESTARTSYS:
			break;
		default:
			eprintk("%d\n", res);
			conn_close(conn);
			break;
		}
	} else {
		conn->read_size -= res;
		if (conn->read_size)
			forward_iov(&conn->read_msg, res);
		else
			conn->read_state = state;
	}

out:
	dprintk(D_IOD, "%d\n", res);
	return res;
}

static void rx_ddigest(struct iscsi_conn *conn, int state)
{
	struct iscsi_cmnd *cmnd = conn->read_cmnd;
	int res = digest_rx_data(cmnd);

	conn->read_ctio = NULL;
	if (!res)
		conn->read_state = state;
	else {
		DEBUG_WARN_NEW("rx ddigest failed\n");
		conn_close(conn);
	}
}

static void rx_hdigest(struct iscsi_conn *conn, int state)
{
	struct iscsi_cmnd *cmnd = conn->read_cmnd;
	int res = digest_rx_header(cmnd);

	if (!res)
		conn->read_state = state;
	else {
		DEBUG_WARN_NEW("rx hdigest failed\n");
		conn_close(conn);
	}
}

static struct iscsi_cmnd *create_cmnd(struct iscsi_conn *conn)
{
	struct iscsi_cmnd *cmnd;

	cmnd = cmnd_alloc(conn, 1);
	iscsi_conn_init_read(cmnd->conn, &cmnd->pdu.bhs, sizeof(cmnd->pdu.bhs));
	conn->read_state = RX_BHS;

	return cmnd;
}

static int recv(struct iscsi_conn *conn)
{
	struct iscsi_cmnd *cmnd = conn->read_cmnd;
	int hdigest, ddigest, res = 1;

	hdigest = conn->hdigest_type & DIGEST_NONE ? 0 : 1;
	ddigest = conn->ddigest_type & DIGEST_NONE ? 0 : 1;

next_state:
	if (!test_bit(CONN_ACTIVE, &conn->state))
		return -EIO;

	switch (conn->read_state) {
	case RX_INIT_BHS:
		assert(!cmnd);
		cmnd = conn->read_cmnd = create_cmnd(conn);
		conn->read_ctio = NULL;
	case RX_BHS:
		res = do_recv(conn, RX_INIT_AHS);
		if (res <= 0 || conn->read_state != RX_INIT_AHS)
			break;
	case RX_INIT_AHS:
		iscsi_cmnd_get_length(&cmnd->pdu);
		if (cmnd->pdu.ahssize) {
			iscsi_conn_read_ahs(conn, cmnd);
			conn->read_state = RX_AHS;
		} else
			conn->read_state = hdigest ? RX_INIT_HDIGEST : RX_INIT_DATA;

		if (conn->read_state != RX_AHS)
			break;
	case RX_AHS:
		res = do_recv(conn, hdigest ? RX_INIT_HDIGEST : RX_INIT_DATA);
		if (res <= 0 || conn->read_state != RX_INIT_HDIGEST)
			break;
	case RX_INIT_HDIGEST:
		iscsi_conn_init_read(conn, &cmnd->hdigest, sizeof(u32));
		conn->read_state = RX_HDIGEST;
	case RX_HDIGEST:
		res = do_recv(conn, RX_CHECK_HDIGEST);
		if (res <= 0 || conn->read_state != RX_CHECK_HDIGEST)
			break;
	case RX_CHECK_HDIGEST:
		rx_hdigest(conn, RX_INIT_DATA);
		if (conn->read_state != RX_INIT_DATA)
			break;
	case RX_INIT_DATA:
		cmnd_rx_start(cmnd);
		conn->read_state = cmnd->pdu.datasize ? RX_DATA : RX_END;
		if (conn->read_state != RX_DATA)
			break;
	case RX_DATA:
		res = do_recv(conn, ddigest ? RX_INIT_DDIGEST : RX_END);
		if (res <= 0 || conn->read_state != RX_INIT_DDIGEST)
			break;
	case RX_INIT_DDIGEST:
		iscsi_conn_init_read(conn, &cmnd->ddigest, sizeof(u32));
		conn->read_state = RX_DDIGEST;
	case RX_DDIGEST:
		res = do_recv(conn, RX_CHECK_DDIGEST);
		if (res <= 0 || conn->read_state != RX_CHECK_DDIGEST)
			break;
	case RX_CHECK_DDIGEST:
		rx_ddigest(conn, RX_END);
		break;
	default:
		eprintk("%d %d %x\n", res, conn->read_state, cmnd_opcode(cmnd));
		assert(0);
	}

	if (res <= 0)
		return res;

	if (conn->read_state != RX_END)
		goto next_state;

	if (conn->read_size) {
		eprintk("%d %x %d\n", res, cmnd_opcode(cmnd), conn->read_size);
		assert(0);
	}

	cmnd_rx_end(cmnd);
	if (conn->read_size) {
		eprintk("%x %d\n", cmnd_opcode(cmnd), conn->read_size);
		conn->read_state = RX_DATA;
		return 1;
	}

	conn->read_cmnd = NULL;
	conn->read_ctio = NULL;
	conn->read_state = RX_INIT_BHS;

	return 0;
}

/* This is taken from the Ardis code. */
static int write_data(struct iscsi_conn *conn)
{
#ifdef LINUX
	mm_segment_t oldfs;
	struct file *file;
	ssize_t (*sendpage)(struct socket *, pagestruct_t *, int, size_t, int);
#else
	struct uio uio;
	struct iovec iov;
#endif
	struct socket *sock;
	struct tio *tio;
	struct iovec *iop;
	int saved_size, size, sendsize;
	int offset, idx;
	int flags, res;

#ifdef LINUX
	file = conn->file;
#endif
	saved_size = size = conn->write_size;
	iop = conn->write_iop;

	if (iop) while (1) {
#ifdef LINUX
		loff_t off = 0;
		int rest;
#endif
		struct iovec *vec;
		unsigned long count;
		int len = 0;

		vec = iop;
		for (count = 0; vec->iov_len; count++, vec++) {
			len += vec->iov_len; /* Could have moved to the for loop above */
		}
#ifdef LINUX
		oldfs = get_fs();
		set_fs(KERNEL_DS);
		res = vfs_writev(file, (struct iovec __user *) iop, count, &off);
		set_fs(oldfs);
#else
		uio_fill(&uio, iop, count, len, UIO_WRITE);
		flags = MSG_DONTWAIT | MSG_NOSIGNAL;
		res = sosend(conn->sock, NULL, &uio, NULL, NULL, flags, curthread);
		map_result(&res, &uio, len, 0);
#endif
		dprintk(D_DATA, "%#Lx:%u: %d(%ld)\n",
			(unsigned long long) conn->session->sid, conn->cid,
			res, (long) iop->iov_len);
		if (unlikely(res <= 0)) {
			if (res == -EAGAIN || res == -EINTR) {
				conn->write_iop = iop;
				goto out_iov;
			}
			goto err;
		}

		size -= res;
#ifdef LINUX
		rest = res;
		while (iop->iov_len && iop->iov_len <= rest && rest) {
			rest -= iop->iov_len;
			iop++;
		}
		iop->iov_base = (char *)(iop->iov_base) + rest;
		iop->iov_len -= rest;
#else
		iop = uio.uio_iov;
#endif
		if (!iop->iov_len) {
			conn->write_iop = NULL;
			if (size)
				break;
			goto out_iov;
		}

	}

	if (!(tio = conn->write_tcmnd)) {
		eprintk("%s\n", "warning data missing!");
		return 0;
	}
	offset = conn->write_offset;
	idx = offset >> PAGE_CACHE_SHIFT;
	offset &= ~PAGE_CACHE_MASK;

	sock = conn->sock;
#ifdef LINUX
	sendpage = sock->ops->sendpage ? : sock_no_sendpage;
#endif
	flags = MSG_DONTWAIT;

	while (1) {
		sendsize = PAGE_CACHE_SIZE - offset;
		if (size <= sendsize) {
#ifdef LINUX
			res = sendpage(sock, tio->pvec[idx], offset, size, flags);
#else
			iov.iov_base = (caddr_t)page_address(tio->pvec[idx]) + offset;
			iov.iov_len = size; 
			uio_fill(&uio, &iov, 1, size, UIO_WRITE);
			res = sosend(sock, NULL, &uio, NULL, NULL, flags, curthread);
			map_result(&res, &uio, size, 0);
#endif
			dprintk(D_DATA, "%s %#Lx:%u: %d(%lu,%u,%u)\n",
				sock->ops->sendpage ? "sendpage" : "writepage",
				(unsigned long long ) conn->session->sid, conn->cid,
				res, tio->pvec[idx]->index, offset, size);
			if (unlikely(res <= 0)) {
				if (res == -EAGAIN || res == -EINTR) {
					goto out;
				}
				goto err;
			}
			if (res == size) {
				conn->write_tcmnd = NULL;
				conn->write_size = 0;
				return saved_size;
			}
			offset += res;
			size -= res;
			continue;
		}

#ifdef LINUX
		res = sendpage(sock, tio->pvec[idx], offset,sendsize, flags | MSG_MORE);
#else
		iov.iov_base = (caddr_t)page_address(tio->pvec[idx]) + offset;
		iov.iov_len = sendsize; 
		uio_fill(&uio, &iov, 1, sendsize, UIO_WRITE);
		res = sosend(sock, NULL, &uio, NULL, NULL, flags, curthread);
		map_result(&res, &uio, sendsize, 0);
#endif
		dprintk(D_DATA, "%s %#Lx:%u: %d(%lu,%u,%u)\n",
			sock->ops->sendpage ? "sendpage" : "writepage",
			(unsigned long long ) conn->session->sid, conn->cid,
			res, tio->pvec[idx]->index, offset, sendsize);
		if (unlikely(res <= 0)) {
			if (res == -EAGAIN || res == -EINTR) {
				goto out;
			}
			goto err;
		}
		if (res == sendsize) {
			idx++;
			offset = 0;
		} else
			offset += res;
		size -= res;
	}
 out:
	conn->write_offset = (idx << PAGE_CACHE_SHIFT) + offset;
 out_iov:
	conn->write_size = size;
	if (res == -EAGAIN) {
		set_conn_wspace_wait(conn);
		if (saved_size == size)
			return res;
	}

	return saved_size - size;

 err:
	eprintk("error %d at %#Lx:%u\n", res,
		(unsigned long long) conn->session->sid, conn->cid);
	return res;
}

void exit_tx(struct iscsi_conn *conn, int res)
{
	if (res > 0)
		return;

	switch (res) {
	case -EAGAIN:
	case -ERESTARTSYS:
		break;
	default:
		eprintk("%d %d %d\n", conn->write_size, conn->write_state, res);
		conn_close(conn);
		break;
	}
}

static int tx_ddigest(struct iscsi_cmnd *cmnd, int state)
{
	int res, rest = cmnd->conn->write_size;
#ifdef LINUX
	struct msghdr msg = {.msg_flags = MSG_NOSIGNAL | MSG_DONTWAIT};
	struct kvec iov;
#else
	struct iovec iov;
	struct uio uio;
	int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
#endif

	iov.iov_base = (char *) (&cmnd->ddigest) + (sizeof(u32) - rest);
	iov.iov_len = rest;

#ifdef LINUX
	res = kernel_sendmsg(cmnd->conn->sock, &msg, &iov, 1, rest);
#else
	uio_fill(&uio, &iov, 1, rest, UIO_WRITE);
	res = sosend(cmnd->conn->sock, NULL, &uio, NULL, NULL, flags, curthread);
	map_result(&res, &uio, rest, 0);
#endif

	if (res > 0) {
		cmnd->conn->write_size -= res;
		if (!cmnd->conn->write_size)
			cmnd->conn->write_state = state;
	} else
		exit_tx(cmnd->conn, res);

	return res;
}

static void init_tx_hdigest(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn = cmnd->conn;
	struct iovec *iop;

	if (conn->hdigest_type & DIGEST_NONE)
		return;

	digest_tx_header(cmnd);

	for (iop = conn->write_iop; iop->iov_len; iop++)
		;
	iop->iov_base = &(cmnd->hdigest);
	iop->iov_len = sizeof(u32);
	conn->write_size += sizeof(u32);
	iop++;
	iop->iov_len = 0;

	return;
}

enum tx_state {
	TX_INIT, /* Must be zero. */
	TX_BHS_DATA,
	TX_INIT_DDIGEST,
	TX_DDIGEST,
	TX_END,
};

extern int do_send(struct iscsi_conn *conn, int state);
int do_send(struct iscsi_conn *conn, int state)
{
	int res;

	if (conn->ctio)
	{
		res = do_send_ctio(conn, state);
		if (!conn->write_size)
		{
			conn->ctio = NULL;
			conn->write_state = state;
		}
		return res;
	}

	res = write_data(conn);

	if (res > 0) {
		if (!conn->write_size)
			conn->write_state = state;
	} else
		exit_tx(conn, res);

	return res;
}

static int send(struct iscsi_conn *conn)
{
	struct iscsi_cmnd *cmnd = conn->write_cmnd;
	struct iscsi_cmnd *req;
	int ddigest, res = 0;

	ddigest = conn->ddigest_type != DIGEST_NONE ? 1 : 0;

next_state:
	switch (conn->write_state) {
	case TX_INIT:
		assert(!cmnd);
		cmnd = conn->write_cmnd = iscsi_get_send_cmnd(conn);
		if (!cmnd)
			return 0;
		req = cmnd->req;
		if (req && cmnd_tmfabort(req) && !cmnd_sendabort(req)) {
			cmnd_release(cmnd, 0);
			conn->write_cmnd = NULL;
			conn->write_state = TX_INIT;
			return 0;
		} 
		cmnd_tx_start(cmnd);
		init_tx_hdigest(cmnd);
		conn->write_state = TX_BHS_DATA;
	case TX_BHS_DATA:
		res = do_send(conn, ddigest && cmnd->pdu.datasize ? TX_INIT_DDIGEST : TX_END);
		if (res <= 0 || conn->write_state != TX_INIT_DDIGEST)
			break;
	case TX_INIT_DDIGEST:
		digest_tx_data(cmnd);
		assert(!cmnd->conn->write_size);
		cmnd->conn->write_size += sizeof(u32);
		conn->write_state = TX_DDIGEST;
	case TX_DDIGEST:
		res = tx_ddigest(cmnd, TX_END);
		break;
	default:
		eprintk("%d %d %x\n", res, conn->write_state, cmnd_opcode(cmnd));
		assert(0);
	}

	if (res <= 0)
		return res;

	if (conn->write_state != TX_END)
		goto next_state;

	if (conn->write_size) {
		eprintk("%d %x %u\n", res, cmnd_opcode(cmnd), conn->write_size);
		assert(!conn->write_size);
	}
	cmnd_tx_end(cmnd);
	cmnd_release(cmnd, 0);
	conn->write_cmnd = NULL;
	conn->write_state = TX_INIT;

	return 0;
}

#ifdef LINUX
static void conn_nop_timeout(unsigned long data)
#else
static void conn_nop_timeout(void *data)
#endif
{
	struct iscsi_conn *conn = (struct iscsi_conn *)data;

	if (test_bit(CONN_ACTIVE, &conn->state))
		set_bit(CONN_NEED_NOP_IN, &conn->state);

	dprintk(D_THREAD, "conn %llu:%hu, NOP timer %p\n", conn->session->sid,
		conn->cid, &conn->nop_timer);

	nthread_wakeup(conn->session->target);
}

static void conn_reset_nop_timer(struct iscsi_conn *conn)
{
	struct iscsi_target *target = conn->session->target;

	if (target->trgt_param.nop_interval)
		callout_reset(&conn->nop_timer,
			  jiffies + HZ * target->trgt_param.nop_interval,
			  conn_nop_timeout, conn);
}

static void conn_start_nop_timer(struct iscsi_conn *conn)
{
	struct iscsi_target *target = conn->session->target;

	if (!target->trgt_param.nop_interval || timer_pending(&conn->nop_timer))
		return;

#ifdef LINUX
	conn->nop_timer.data = (unsigned long)conn;
	conn->nop_timer.function = conn_nop_timeout;
#endif

	dprintk(D_THREAD, "conn %llu:%hu, NOP timer %p\n", conn->session->sid,
		conn->cid, &conn->nop_timer);

	callout_reset(&conn->nop_timer,
		  jiffies + HZ * target->trgt_param.nop_interval,
		  conn_nop_timeout, conn);
}

static void process_io(struct iscsi_conn *conn)
{
	struct iscsi_target *target = conn->session->target;
	int res, wakeup = 0;

	res = recv(conn);

	if (is_data_available(conn) > 0 || res > 0) {
		conn_reset_nop_timer(conn);
		wakeup = 1;
	}


	if (!test_bit(CONN_ACTIVE, &conn->state)) {
		wakeup = 1;
		goto out;
	}

	if (test_bit(CONN_WSPACE_WAIT, &conn->state))
		goto out;

	res = send(conn);

	if (!list_empty(&conn->write_list) || conn->write_cmnd) {
		conn_reset_nop_timer(conn);
		wakeup = 1;
	}

out:
	if (wakeup)
		nthread_wakeup(target);
	else if (test_and_clear_bit(CONN_NEED_NOP_IN, &conn->state)) {
		send_nop_in(conn);
		nthread_wakeup(target);
	} else
		conn_start_nop_timer(conn);
	return;
}

void close_conn(struct iscsi_conn *conn)
{
	struct iscsi_session *session = conn->session;
	struct iscsi_target *target = session->target;
	struct iscsi_cmnd *cmnd;

	if (target->trgt_param.nop_interval)
		del_timer_sync(&conn->nop_timer);

#ifdef LINUX
	conn->sock->ops->shutdown(conn->sock, 2);

	write_lock_bh(&conn->sock->sk->sk_callback_lock);
	conn->sock->sk->sk_state_change = target->nthread_info.old_state_change;
	conn->sock->sk->sk_data_ready = target->nthread_info.old_data_ready;
	conn->sock->sk->sk_write_space = target->nthread_info.old_write_space;
	write_unlock_bh(&conn->sock->sk->sk_callback_lock);

	fput(conn->file);
#else
	SOCK_LOCK(conn->sock);
	soupcall_clear(conn->sock, SO_RCV);
	SOCK_UNLOCK(conn->sock);
	
	SOCKBUF_LOCK(&conn->sock->so_snd);
	soupcall_clear(conn->sock, SO_SND);
	SOCKBUF_UNLOCK(&conn->sock->so_snd);

	fputsock(conn->sock);
	fdrop(conn->file, curthread);
#endif
	conn->file = NULL;
	conn->sock = NULL;

	while (atomic_read(&conn->nr_busy_cmnds)) {
		pause("psg", 10);
		yield();
	}

	while (!list_empty(&conn->pdu_list)) {
		cmnd = list_entry(conn->pdu_list.next, struct iscsi_cmnd, conn_list);

		list_del_init(&cmnd->list);
		cmnd_release(cmnd, 1);
	}

	while (!list_empty(&conn->write_list)) {
		cmnd = list_entry(conn->write_list.next, struct iscsi_cmnd, list);
		list_del_init(&cmnd->list);
		cmnd_release(cmnd, 1);
	}

	if (atomic_read(&conn->nr_cmnds)) {
		eprintk("%u\n", atomic_read(&conn->nr_cmnds));
		list_for_each_entry(cmnd, &conn->pdu_list, conn_list)
			eprintk("%x %x\n", cmnd_opcode(cmnd), cmnd_itt(cmnd));
		assert(0);
	}

	if (!atomic_read(&session->target->del))
	{
		event_send(target->tid, session->sid, conn->cid, E_CONN_CLOSE, 0);
	}
	conn_free(conn);

	if (list_empty(&session->conn_list)) {
		if (session->done)
			complete(session->done);
		else
			session_del(target, session->sid);
	}
}

#ifdef LINUX
static int istd(void *arg)
#else
static void istd(void *arg)
#endif
{
	struct iscsi_target *target = arg;
	struct network_thread_info *info = &target->nthread_info;
	struct iscsi_conn *conn, *tmp;

	__sched_prio(curthread, PINOD);

	__set_current_state(TASK_RUNNING);
	for (;;) {

		wait_event_interruptible(info->nthread_wait, test_bit(D_DATA_READY, &info->flags) || test_bit(D_THR_EXIT, &info->flags) || kthread_should_stop()); 

		if(test_bit(D_THR_EXIT, &info->flags) || kthread_should_stop())
		{
			break;
		}
		clear_bit(D_DATA_READY, &info->flags);
		target_lock(target, 0);
		list_for_each_entry_safe(conn, tmp, &info->active_conns, poll_list) {
			if (test_bit(CONN_ACTIVE, &conn->state))
				process_io(conn);
			else
				close_conn(conn);
		}
		target_unlock(target);
	}
#ifdef LINUX
	return 0;
#else
	kproc_exit(0);
#endif
}

int nthread_init(struct iscsi_target *target)
{
	struct network_thread_info *info = &target->nthread_info;

	info->flags = 0;
	info->task = NULL;

#ifdef LINUX
	info->old_state_change = NULL;
	info->old_data_ready = NULL;
	info->old_write_space = NULL;
#endif

	INIT_LIST_HEAD(&info->active_conns);

	spin_lock_initt(&info->nthread_lock, "nthread");
	init_waitqueue_head(&info->nthread_wait);

	return 0;
}

int nthread_start(struct iscsi_target *target)
{
	int err = 0;
	struct network_thread_info *info = &target->nthread_info;

	if (info->task) {
		eprintk("Target (%u) already runs\n", target->tid);
		return -EALREADY;
	}

	err = kernel_thread_create(istd, target, info->task, "istd%d", target->tid);
	return err;
}

int nthread_stop(struct iscsi_target *target)
{
	int err;
	struct network_thread_info *info = &target->nthread_info;

	if (!info->task)
		return -ESRCH;

#ifdef FREEBSD
	mtx_lock(&info->nthread_wait.chan_lock);
	set_bit(D_THR_EXIT, &info->flags);
	cv_broadcast(&info->nthread_wait.chan_cond);
	msleep(info->task, &info->nthread_wait.chan_lock, 0, "texit", 0);
	mtx_unlock(&info->nthread_wait.chan_lock);
	err = 0;
#else
	err = kthread_stop(info->task);
#endif

	if (err < 0 && err != -EINTR)
		return err;

	info->task = NULL;

	return err;
}

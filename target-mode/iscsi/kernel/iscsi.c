/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 * Copyright (C) 2008 Arne Redlich <agr@powerkom-dd.de>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include "scdefs.h"

#include "iscsi.h"
#include "iscsi_dbg.h"
#include "iotype.h"

unsigned long debug_enable_flags;

static slab_cache_t *iscsi_cmnd_cache;
static char dummy_data[PAGE_SIZE];

#ifdef LINUX
static int ctr_major;
#endif
static char ctr_name[] = "iscsit";
extern struct file_operations ctr_fops;

u32 cmnd_write_size(struct iscsi_cmnd *cmnd)
{
	struct iscsi_scsi_cmd_hdr *hdr = cmnd_hdr(cmnd);

	if (hdr->flags & ISCSI_CMD_WRITE)
		return be32_to_cpu(hdr->data_length);
	return 0;
}

u32 cmnd_read_size(struct iscsi_cmnd *cmnd)
{
	struct iscsi_scsi_cmd_hdr *hdr = cmnd_hdr(cmnd);

	if (hdr->flags & ISCSI_CMD_READ) {
		struct iscsi_rlength_ahdr *ahdr =
			(struct iscsi_rlength_ahdr *)cmnd->pdu.ahs;

		if (!(hdr->flags & ISCSI_CMD_WRITE))
			return be32_to_cpu(hdr->data_length);
		if (ahdr && ahdr->ahstype == ISCSI_AHSTYPE_RLENGTH)
			return be32_to_cpu(ahdr->read_length);
	}
	return 0;
}

static void iscsi_scsi_queuecmnd(struct iscsi_cmnd *cmnd)
{
	struct qsio_scsiio *ctio;
	struct tdevice *device;

	DEBUG_BUG_ON(cmnd_waitio(cmnd));
	ctio = cmnd->ctio;
	cmnd->ctio = NULL;
	set_cmnd_queued(cmnd);
	set_cmnd_waitio(cmnd);
	atomic_inc(&cmnd->conn->nr_busy_cmnds);

	if (cmnd_tmfabort(cmnd)) {
		DEBUG_BUG_ON(ctio->queued);
		if (cmnd_sendabort(cmnd)) {
			(*icbs.ctio_free_data)(ctio);
			(*icbs.device_send_ccb)(ctio);
		}
		else {
			(*icbs.ctio_free_all)(ctio);
			cmnd_release(cmnd, 1);
		}
		return;
	}

	device = ctio->ccb_h.tdevice;
	(*icbs.device_queue_ctio)(device, ctio);
}

/**
 * create a new command.
 *
 * iscsi_cmnd_create -
 * @conn: ptr to connection (for i/o)
 *
 * @return    ptr to command or NULL
 */

struct iscsi_cmnd *cmnd_alloc(struct iscsi_conn *conn, int req)
{
	struct iscsi_cmnd *cmnd;

	/* TODO: async interface is necessary ? */
	cmnd = slab_cache_alloc(iscsi_cmnd_cache, M_WAITOK | M_ZERO, sizeof(*cmnd));

	INIT_LIST_HEAD(&cmnd->list);
	INIT_LIST_HEAD(&cmnd->pdu_list);
	INIT_LIST_HEAD(&cmnd->conn_list);
	INIT_LIST_HEAD(&cmnd->hash_list);
	cmnd->conn = conn;
	spin_lock(&conn->list_lock);
	atomic_inc(&conn->nr_cmnds);
	if (req)
		list_add_tail(&cmnd->conn_list, &conn->pdu_list);
	spin_unlock(&conn->list_lock);
	cmnd->tio = NULL;

	dprintk(D_GENERIC, "%p:%p\n", conn, cmnd);

	return cmnd;
}

/**
 * create a new command used as response.
 *
 * iscsi_cmnd_create_rsp_cmnd -
 * @cmnd: ptr to request command
 *
 * @return    ptr to response command or NULL
 */

struct iscsi_cmnd *iscsi_cmnd_create_rsp_cmnd(struct iscsi_cmnd *cmnd, int final)
{
	struct iscsi_cmnd *rsp = cmnd_alloc(cmnd->conn, 0);

	if (final)
		set_cmnd_final(rsp);
	list_add_tail(&rsp->pdu_list, &cmnd->pdu_list);
	rsp->req = cmnd;
	return rsp;
}

static struct iscsi_cmnd *get_rsp_cmnd(struct iscsi_cmnd *req)
{
	return list_entry(req->pdu_list.prev, struct iscsi_cmnd, pdu_list);
}

void iscsi_cmnds_init_write(struct list_head *send, int dec_busy)
{
	struct iscsi_cmnd *cmnd = list_entry(send->next, struct iscsi_cmnd, list);
	struct iscsi_conn *conn = cmnd->conn;
	struct list_head *pos, *next;

	spin_lock(&conn->list_lock);

	list_for_each_safe(pos, next, send) {
		cmnd = list_entry(pos, struct iscsi_cmnd, list);

		dprintk(D_GENERIC, "%p:%x\n", cmnd, cmnd_opcode(cmnd));

		list_del_init(&cmnd->list);
		assert(conn == cmnd->conn);
		list_add_tail(&cmnd->list, &conn->write_list);
	}

	if (dec_busy)
		atomic_dec(&conn->nr_busy_cmnds);

	spin_unlock(&conn->list_lock);

	nthread_wakeup(conn->session->target);
}

void iscsi_cmnd_init_write(struct iscsi_cmnd *cmnd, int dec_busy)
{
	QS_LIST_HEAD(head);

	if (!list_empty(&cmnd->list)) {
		eprintk("%x %x %x %x %lx %u %u %u %u %u %u %u %d %d\n",
			cmnd_itt(cmnd), cmnd_ttt(cmnd), cmnd_opcode(cmnd),
			cmnd_scsicode(cmnd), (unsigned long) cmnd->flags, cmnd->r2t_sn,
			cmnd->r2t_length, cmnd->is_unsolicited_data,
			cmnd->target_task_tag, cmnd->outstanding_r2t,
			cmnd->hdigest, cmnd->ddigest,
			list_empty(&cmnd->pdu_list), list_empty(&cmnd->hash_list));

		assert(list_empty(&cmnd->list));
	}
	list_add(&cmnd->list, &head);
	iscsi_cmnds_init_write(&head, dec_busy);
}

static struct iscsi_cmnd *create_sense_rsp(struct iscsi_cmnd *req,
					   u8 sense_key, u8 asc, u8 ascq)
{
	struct iscsi_cmnd *rsp;
	struct iscsi_scsi_rsp_hdr *rsp_hdr;
	struct tio *tio;
	struct iscsi_sense_data *sense;

	rsp = iscsi_cmnd_create_rsp_cmnd(req, 1);

	rsp_hdr = (struct iscsi_scsi_rsp_hdr *)&rsp->pdu.bhs;
	rsp_hdr->opcode = ISCSI_OP_SCSI_RSP;
	rsp_hdr->flags = ISCSI_FLG_FINAL;
	rsp_hdr->response = ISCSI_RESPONSE_COMMAND_COMPLETED;
	rsp_hdr->cmd_status = SAM_STAT_CHECK_CONDITION;
	rsp_hdr->itt = cmnd_hdr(req)->itt;

	tio = rsp->tio = tio_alloc(1);
	sense = (struct iscsi_sense_data *)page_address(tio->pvec[0]);
	assert(sense);
	clear_page(sense);
	sense->length = cpu_to_be16(14);
	sense->data[0] = 0xf0;
	sense->data[2] = sense_key;
	sense->data[7] = 6;	// Additional sense length
	sense->data[12] = asc;
	sense->data[13] = ascq;

	rsp->pdu.datasize = sizeof(struct iscsi_sense_data) + 14;
	tio->size = (rsp->pdu.datasize + 3) & -4;
	tio->offset = 0;

	return rsp;
}

/**
 * Free a command.
 * Also frees the additional header.
 *
 * iscsi_cmnd_remove -
 * @cmnd: ptr to command
 */

static void iscsi_cmnd_remove(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn;

	if (!cmnd)
		return;

	if (cmnd_timer_active(cmnd)) {
		clear_cmnd_timer_active(cmnd);
		del_timer_sync(&cmnd->timer);
	}

	dprintk(D_GENERIC, "%p\n", cmnd);
	conn = cmnd->conn;
	free(cmnd->pdu.ahs, M_IETAHS);

	if (unlikely(!list_empty(&cmnd->list))) {
		struct iscsi_scsi_cmd_hdr *req = cmnd_hdr(cmnd);

		eprintk("cmnd %p still on some list?, %x, %x, %x, %x, %x, %x, %x %lx\n",
			cmnd, req->opcode, req->scb[0], req->flags, req->itt,
			be32_to_cpu(req->data_length),
			req->cmd_sn, be32_to_cpu(cmnd->pdu.datasize),
			(unsigned long)conn->state);

		if (cmnd->req) {
			struct iscsi_scsi_cmd_hdr *req = cmnd_hdr(cmnd->req);
			eprintk("%p %x %u\n", req, req->opcode, req->scb[0]);
		}
		dump_stack();
		BUG();
	}
	list_del(&cmnd->list);
	spin_lock(&conn->list_lock);
	atomic_dec(&conn->nr_cmnds);
	list_del(&cmnd->conn_list);
	spin_unlock(&conn->list_lock);

	if (cmnd->tio)
		tio_put(cmnd->tio);

	if (cmnd->ctio)
	{
 		if ((cmnd->ctio->ccb_h.flags & QSIO_DIR_IN) && cmnd_final(cmnd))
		{
			(*icbs.ctio_free_all)(cmnd->ctio);
		}
		else if ((cmnd->ctio->ccb_h.flags & QSIO_DIR_OUT))
		{
			(*icbs.ctio_free_all)(cmnd->ctio);
		}
	}

	slab_cache_free(iscsi_cmnd_cache, cmnd);
}

static void cmnd_skip_pdu(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn = cmnd->conn;
	struct tio *tio = cmnd->tio;
	char *addr;
	u32 size;
	int i;

#ifdef ENABLE_DEBUG
	eprintk("%x %x %x %u\n", cmnd_itt(cmnd), cmnd_opcode(cmnd),
		cmnd_hdr(cmnd)->scb[0], cmnd->pdu.datasize);
#endif

	if (!(size = cmnd->pdu.datasize))
		return;

	if (tio)
		assert(tio->pg_cnt > 0);
	else
		tio = cmnd->tio = tio_alloc(1);

	addr = (caddr_t)page_address(tio->pvec[0]);
	assert(addr);
	size = (size + 3) & -4;
	conn->read_size = size;
	for (i = 0; size > PAGE_CACHE_SIZE; i++, size -= PAGE_CACHE_SIZE) {
		assert(i < ISCSI_CONN_IOV_MAX);
		conn->read_iov[i].iov_base = addr;
		conn->read_iov[i].iov_len = PAGE_CACHE_SIZE;
	}
	conn->read_iov[i].iov_base = addr;
	conn->read_iov[i].iov_len = size;
	conn->read_msg.msg_iov = conn->read_iov;
	conn->read_msg.msg_iovlen = ++i;
}

static void iscsi_cmnd_reject(struct iscsi_cmnd *req, int reason)
{
	struct iscsi_cmnd *rsp;
	struct iscsi_reject_hdr *rsp_hdr;
	struct tio *tio;
	char *addr;

	rsp = iscsi_cmnd_create_rsp_cmnd(req, 1);
	rsp_hdr = (struct iscsi_reject_hdr *)&rsp->pdu.bhs;

	rsp_hdr->opcode = ISCSI_OP_REJECT;
	rsp_hdr->ffffffff = ISCSI_RESERVED_TAG;
	rsp_hdr->reason = reason;

	rsp->tio = tio = tio_alloc(1);
	addr = (caddr_t)page_address(tio->pvec[0]);
	clear_page(addr);
	memcpy(addr, &req->pdu.bhs, sizeof(struct iscsi_hdr));
	tio->size = rsp->pdu.datasize = sizeof(struct iscsi_hdr);
	cmnd_skip_pdu(req);

	req->pdu.bhs.opcode = ISCSI_OP_PDU_REJECT;
}

static void cmnd_set_sn(struct iscsi_cmnd *cmnd, int set_stat_sn)
{
	struct iscsi_conn *conn = cmnd->conn;
	struct iscsi_session *sess = conn->session;

	sess->max_cmd_sn = sess->exp_cmd_sn + sess->max_queued_cmnds;

	if (set_stat_sn)
		cmnd->pdu.bhs.sn = cpu_to_be32(conn->stat_sn++);
	cmnd->pdu.bhs.exp_sn = cpu_to_be32(sess->exp_cmd_sn);
	cmnd->pdu.bhs.max_sn = cpu_to_be32(sess->max_cmd_sn);
}

static void update_stat_sn(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn = cmnd->conn;
	u32 exp_stat_sn;

	cmnd->pdu.bhs.exp_sn = exp_stat_sn = be32_to_cpu(cmnd->pdu.bhs.exp_sn);
	dprintk(D_GENERIC, "%x,%x\n", cmnd_opcode(cmnd), exp_stat_sn);
	if ((int)(exp_stat_sn - conn->exp_stat_sn) > 0 &&
	    (int)(exp_stat_sn - conn->stat_sn) <= 0) {
		// free pdu resources
		cmnd->conn->exp_stat_sn = exp_stat_sn;
	}
}

static int check_cmd_sn(struct iscsi_cmnd *cmnd)
{
	struct iscsi_session *session = cmnd->conn->session;
	u32 cmd_sn;

	cmnd->pdu.bhs.sn = cmd_sn = be32_to_cpu(cmnd->pdu.bhs.sn);

	dprintk(D_GENERIC, "cmd_sn(%u) exp_cmd_sn(%u) max_cmd_sn(%u)\n",
		cmd_sn, session->exp_cmd_sn, session->max_cmd_sn);

	if  (between(cmd_sn, session->exp_cmd_sn, session->max_cmd_sn))
		return 0;
	else if (cmnd_immediate(cmnd))
		return 0;

	eprintk("sequence error: cmd_sn(%u) exp_cmd_sn(%u) max_cmd_sn(%u)\n",
		cmd_sn, session->exp_cmd_sn, session->max_cmd_sn);

	set_cmnd_tmfabort(cmnd);

	return -ISCSI_REASON_PROTOCOL_ERROR;
}

static struct iscsi_cmnd *__cmnd_find_hash(struct iscsi_session *session, u32 itt, u32 ttt)
{
	struct list_head *head;
	struct iscsi_cmnd *cmnd;

	head = &session->cmnd_hash[cmnd_hashfn(itt)];

	list_for_each_entry(cmnd, head, hash_list) {
		if (cmnd->pdu.bhs.itt == itt) {
			if ((ttt != ISCSI_RESERVED_TAG) && (ttt != cmnd->target_task_tag))
				continue;
			return cmnd;
		}
	}

	return NULL;
}

static struct iscsi_cmnd *cmnd_find_hash(struct iscsi_session *session, u32 itt, u32 ttt)
{
	struct iscsi_cmnd *cmnd;

	spin_lock(&session->cmnd_hash_lock);

	cmnd = __cmnd_find_hash(session, itt, ttt);

	spin_unlock(&session->cmnd_hash_lock);

	return cmnd;
}

static int cmnd_insert_hash_ttt(struct iscsi_cmnd *cmnd, u32 ttt)
{
	struct iscsi_session *session = cmnd->conn->session;
	struct iscsi_cmnd *tmp;
	struct list_head *head;
	int err = 0;
	u32 itt = cmnd->pdu.bhs.itt;

	head = &session->cmnd_hash[cmnd_hashfn(itt)];

	spin_lock(&session->cmnd_hash_lock);

	tmp = __cmnd_find_hash(session, itt, ttt);
	if (!tmp) {
		list_add_tail(&cmnd->hash_list, head);
		set_cmnd_hashed(cmnd);
	} else
		err = -ISCSI_REASON_TASK_IN_PROGRESS;

	spin_unlock(&session->cmnd_hash_lock);

	return err;
}

static int cmnd_insert_hash(struct iscsi_cmnd *cmnd)
{
	int err;

	dprintk(D_GENERIC, "%p:%x\n", cmnd, cmnd->pdu.bhs.itt);

	if (cmnd->pdu.bhs.itt == ISCSI_RESERVED_TAG)
		return -ISCSI_REASON_PROTOCOL_ERROR;

	err = cmnd_insert_hash_ttt(cmnd, ISCSI_RESERVED_TAG);
	if (!err) {
		err = check_cmd_sn(cmnd);
		if (!err)
			update_stat_sn(cmnd);
	} else if (!cmnd_immediate(cmnd))
		set_cmnd_tmfabort(cmnd);

	return err;
}

static void __cmnd_remove_hash(struct iscsi_cmnd *cmnd)
{
	list_del(&cmnd->hash_list);
}

static void cmnd_remove_hash(struct iscsi_cmnd *cmnd)
{
	struct iscsi_session *session = cmnd->conn->session;
	struct iscsi_cmnd *tmp;

	spin_lock(&session->cmnd_hash_lock);

	tmp = __cmnd_find_hash(session, cmnd->pdu.bhs.itt,
			       cmnd->target_task_tag);
	if (tmp && tmp == cmnd)
		__cmnd_remove_hash(tmp);
	else
		eprintk("%p:%x not found\n", cmnd, cmnd_itt(cmnd));

	spin_unlock(&session->cmnd_hash_lock);
}

static void cmnd_skip_data(struct iscsi_cmnd *req)
{
	struct iscsi_cmnd *rsp;
	struct iscsi_scsi_rsp_hdr *rsp_hdr;
	u32 size;

	rsp = get_rsp_cmnd(req);
	rsp_hdr = (struct iscsi_scsi_rsp_hdr *)&rsp->pdu.bhs;
	if (cmnd_opcode(rsp) != ISCSI_OP_SCSI_RSP) {
		eprintk("unexpected response command %u\n", cmnd_opcode(rsp));
		return;
	}

	size = cmnd_write_size(req);
	if (size) {
		rsp_hdr->flags |= ISCSI_FLG_RESIDUAL_UNDERFLOW;
		rsp_hdr->residual_count = cpu_to_be32(size);
	}
	size = cmnd_read_size(req);
	if (size) {
		if (cmnd_hdr(req)->flags & ISCSI_CMD_WRITE) {
			rsp_hdr->flags |= ISCSI_FLG_BIRESIDUAL_UNDERFLOW;
			rsp_hdr->bi_residual_count = cpu_to_be32(size);
		} else {
			rsp_hdr->flags |= ISCSI_FLG_RESIDUAL_UNDERFLOW;
			rsp_hdr->residual_count = cpu_to_be32(size);
		}
	}
	req->pdu.bhs.opcode =
		(req->pdu.bhs.opcode & ~ISCSI_OPCODE_MASK) | ISCSI_OP_SCSI_REJECT;

	cmnd_skip_pdu(req);
}

static int cmnd_recv_pdu(struct iscsi_conn *conn, struct tio *tio, u32 offset, u32 size)
{
	int idx, i;
	char *addr;

	dprintk(D_GENERIC, "%p %u,%u\n", tio, offset, size);
	offset += tio->offset;

	if (!size)
		return 0;

	if (!(offset < tio->offset + tio->size) ||
	    !(offset + size <= tio->offset + tio->size)) {
		eprintk("%u %u %u %u", offset, size, tio->offset, tio->size);
		return -EIO;
	}
	assert(offset < tio->offset + tio->size);
	assert(offset + size <= tio->offset + tio->size);

	idx = offset >> PAGE_CACHE_SHIFT;
	offset &= ~PAGE_CACHE_MASK;

	conn->read_msg.msg_iov = conn->read_iov;
	conn->read_size = size = (size + 3) & -4;
	conn->read_overflow = 0;

	i = 0;
	while (1) {
		assert(tio->pvec[idx]);
		addr = (caddr_t)page_address(tio->pvec[idx]);
		assert(addr);
		conn->read_iov[i].iov_base =  addr + offset;
		if (offset + size <= PAGE_CACHE_SIZE) {
			conn->read_iov[i].iov_len = size;
			conn->read_msg.msg_iovlen = ++i;
			break;
		}
		conn->read_iov[i].iov_len = PAGE_CACHE_SIZE - offset;
		size -= conn->read_iov[i].iov_len;
		offset = 0;
		if (++i >= ISCSI_CONN_IOV_MAX) {
			conn->read_msg.msg_iovlen = i;
			conn->read_overflow = size;
			conn->read_size -= size;
			break;
		}

		idx++;
	}

	return 0;
}

u32 translate_lun(u16 * data)
{
	u8 *p = (u8 *) data;
	u32 lun = ~0U;

	switch (*p >> 6) {
	case 0:
		lun = p[1];
		break;
	case 1:
		lun = (0x3f & p[0]) << 8 | p[1];
		break;
	case 2:
	case 3:
	default:
		eprintk("%u %u %u %u\n", data[0], data[1], data[2], data[3]);
		break;
	}

	return lun;
}

static void send_r2t(struct iscsi_cmnd *req)
{
	struct iscsi_cmnd *rsp;
	struct iscsi_r2t_hdr *rsp_hdr;
	u32 length, offset, burst;
	QS_LIST_HEAD(send);

	length = req->r2t_length;
	burst = req->conn->session->param.max_burst_length;
	offset = be32_to_cpu(cmnd_hdr(req)->data_length) - length;

	do {
		rsp = iscsi_cmnd_create_rsp_cmnd(req, 0);
		rsp->pdu.bhs.ttt = req->target_task_tag;

		rsp_hdr = (struct iscsi_r2t_hdr *)&rsp->pdu.bhs;
		rsp_hdr->opcode = ISCSI_OP_R2T;
		rsp_hdr->flags = ISCSI_FLG_FINAL;
		memcpy(rsp_hdr->lun, cmnd_hdr(req)->lun, 8);
		rsp_hdr->itt = cmnd_hdr(req)->itt;
		rsp_hdr->r2t_sn = cpu_to_be32(req->r2t_sn++);
		rsp_hdr->buffer_offset = cpu_to_be32(offset);
		if (length > burst) {
			rsp_hdr->data_length = cpu_to_be32(burst);
			length -= burst;
			offset += burst;
		} else {
			rsp_hdr->data_length = cpu_to_be32(length);
			length = 0;
		}

		dprintk(D_WRITE, "%x %u %u %u %u\n", cmnd_itt(req),
			be32_to_cpu(rsp_hdr->data_length),
			be32_to_cpu(rsp_hdr->buffer_offset),
			be32_to_cpu(rsp_hdr->r2t_sn), req->outstanding_r2t);

		list_add_tail(&rsp->list, &send);

		if (++req->outstanding_r2t >= req->conn->session->param.max_outstanding_r2t)
			break;

	} while (length);

	iscsi_cmnds_init_write(&send, 0);
}

static void scsi_cmnd_exec(struct iscsi_cmnd *cmnd)
{
	assert(!cmnd->r2t_length);
	iscsi_scsi_queuecmnd(cmnd);
#if 0
	if (cmnd->r2t_length) {
		if (!cmnd->is_unsolicited_data)
			send_r2t(cmnd);
	} else {
		iscsi_scsi_queuecmnd(cmnd);
	}
#endif
}

static int nop_out_start(struct iscsi_conn *conn, struct iscsi_cmnd *cmnd)
{
	u32 size, tmp;
	int i, err = 0;

	if (cmnd_ttt(cmnd) != cpu_to_be32(ISCSI_RESERVED_TAG)) {
		cmnd->req = cmnd_find_hash(conn->session, cmnd->pdu.bhs.itt,
					   cmnd->pdu.bhs.ttt);
		if (!cmnd->req) {
			/*
			 * We didn't request this NOP-Out (by sending a
			 * NOP-In, see 10.18.2 of the RFC) or our fake NOP-Out
			 * timed out.
			 */
			eprintk("initiator bug %x\n", cmnd_itt(cmnd));
			err = -ISCSI_REASON_PROTOCOL_ERROR;
			goto out;
		}

		del_timer_sync(&cmnd->req->timer);
		clear_cmnd_timer_active(cmnd->req);
		dprintk(D_GENERIC, "NOP-Out: %p, ttt %x, timer %p\n",
			cmnd->req, cmnd_ttt(cmnd->req), &cmnd->req->timer);
	}

	if (cmnd_itt(cmnd) == cpu_to_be32(ISCSI_RESERVED_TAG)) {
		if (!cmnd_immediate(cmnd))
			eprintk("%s\n", "initiator bug!");
		err = check_cmd_sn(cmnd);
		if (err)
			goto out;
		update_stat_sn(cmnd);
	} else if ((err = cmnd_insert_hash(cmnd)) < 0) {
		eprintk("ignore this request %x\n", cmnd_itt(cmnd));
		goto out;
	}

	if ((size = cmnd->pdu.datasize)) {
		size = (size + 3) & -4;
		conn->read_msg.msg_iov = conn->read_iov;
		if (cmnd->pdu.bhs.itt != cpu_to_be32(ISCSI_RESERVED_TAG)) {
			struct tio *tio;
			int pg_cnt = get_pgcnt(size, 0);

			assert(pg_cnt < ISCSI_CONN_IOV_MAX);
			cmnd->tio = tio = tio_alloc(pg_cnt);
			tio_set(tio, size, 0);

			for (i = 0; i < pg_cnt; i++) {
				conn->read_iov[i].iov_base
					= (caddr_t)page_address(tio->pvec[i]);
				tmp = min_t(u32, size, PAGE_CACHE_SIZE);
				conn->read_iov[i].iov_len = tmp;
				conn->read_size += tmp;
				size -= tmp;
			}
		} else {
			for (i = 0; i < ISCSI_CONN_IOV_MAX; i++) {
				conn->read_iov[i].iov_base = dummy_data;
				tmp = min_t(u32, size, sizeof(dummy_data));
				conn->read_iov[i].iov_len = tmp;
				conn->read_size += tmp;
				size -= tmp;
			}
		}
		assert(!size);
		conn->read_overflow = size;
		conn->read_msg.msg_iovlen = i;
	}

out:
	return err;
}

static u32 get_next_ttt(struct iscsi_session *session)
{
	u32 ttt;

	if (session->next_ttt == ISCSI_RESERVED_TAG)
		session->next_ttt++;
	ttt = session->next_ttt++;

	return cpu_to_be32(ttt);
}

static void scsi_cmnd_start(struct iscsi_conn *conn, struct iscsi_cmnd *req)
{
	struct iscsi_scsi_cmd_hdr *req_hdr = cmnd_hdr(req);
	struct qsio_scsiio *ctio;
	int valid;
	struct iscsi_target *target =  conn->session->target;

	if (atomic_read(&target->disabled)) {
		create_sense_rsp(req, ILLEGAL_REQUEST, LOGICAL_UNIT_NOT_SUPPORTED_ASC, LOGICAL_UNIT_NOT_SUPPORTED_ASCQ);
		cmnd_skip_data(req);
		return;
	}
	dprintk(D_GENERIC, "scsi command: %02x\n", req_hdr->scb[0]);
	valid = (*icbs.device_check_cmd)(target->tdevice, req_hdr->scb[0]);
	if (valid != 0)
	{
		create_sense_rsp(req, ILLEGAL_REQUEST, INVALID_COMMAND_OPERATION_CODE_ASC, INVALID_COMMAND_OPERATION_CODE_ASCQ);
		cmnd_skip_data(req);
		return;
	}

	ctio = iscsi_construct_ctio(conn, req);
	if (!ctio)
	{
		DEBUG_WARN("scsi_cmnd_start: iscsi_construct_ctio failed\n");
		create_sense_rsp(req, ABORTED_COMMAND, INTERNAL_TARGET_FAILURE_ASC, INTERNAL_TARGET_FAILURE_ASCQ);
		cmnd_skip_data(req);
		return;
	}

	switch (req_hdr->scb[0]) {
	case SERVICE_ACTION_IN:
		if ((req_hdr->scb[1] & 0x1f) != 0x10)
			goto error;
	case INQUIRY:
	case REPORT_LUNS:
	case TEST_UNIT_READY:
	case SYNCHRONIZE_CACHE:
	case SYNCHRONIZE_CACHE_16:
	case VERIFY:
	case VERIFY_12:
	case VERIFY_16:
	case START_STOP:
	case READ_CAPACITY:
	case MODE_SENSE:
	case REQUEST_SENSE:
	case RESERVE:
	case RELEASE:
	{
		if (!(req_hdr->flags & ISCSI_CMD_FINAL) || req->pdu.datasize) {
			/* unexpected unsolicited data */
			eprintk("%x %x\n", cmnd_itt(req), req_hdr->scb[0]);
			(*icbs.ctio_free_all)(ctio);
			create_sense_rsp(req, ABORTED_COMMAND, 0xc, 0xc);
			cmnd_skip_data(req);
			goto out;
		}
		break;
	}
	case READ_6:
	{
		if (!(req_hdr->flags & ISCSI_CMD_FINAL) || req->pdu.datasize) {
			/* unexpected unsolicited data */
			eprintk("%x %x\n", cmnd_itt(req), req_hdr->scb[0]);
			(*icbs.ctio_free_all)(ctio);
			create_sense_rsp(req, ABORTED_COMMAND, 0xc, 0xc);
			cmnd_skip_data(req);
			goto out;
		}
		break;
	}
	case WRITE_6:
	case MODE_SELECT:
	case MODE_SELECT_10:
	case WRITE_ATTRIBUTE:
	case SEND_DIAGNOSTIC:
	case PERSISTENT_RESERVE_OUT:
	{
		struct iscsi_sess_param *param = &conn->session->param;

		req->r2t_length = be32_to_cpu(req_hdr->data_length) - req->pdu.datasize;
		req->is_unsolicited_data = !(req_hdr->flags & ISCSI_CMD_FINAL);
		req->target_task_tag = get_next_ttt(conn->session);

		if (!param->immediate_data && req->pdu.datasize)
			eprintk("%x %x\n", cmnd_itt(req), req_hdr->scb[0]);

		if (param->initial_r2t && !(req_hdr->flags & ISCSI_CMD_FINAL))
			eprintk("%x %x\n", cmnd_itt(req), req_hdr->scb[0]);

		break;
	}
	default:
		break;
	}

	switch (req_hdr->scb[0]) {
		case WRITE_6:
		{
			int retval;
			__u32 num_blocks, block_size;
			int dxfer_len;

			retval = (*icbs.ctio_write_length)(ctio, ctio->ccb_h.tdevice, &block_size, &num_blocks, &dxfer_len);
			if (unlikely(retval != 0))
			{
				(*icbs.ctio_free_all)(ctio);
				create_sense_rsp(req, ABORTED_COMMAND, COMMAND_PHASE_ERROR_ASC, COMMAND_PHASE_ERROR_ASCQ);
				cmnd_skip_data(req);
				goto out;
			}

			retval = (*icbs.device_allocate_buffers)(ctio, block_size, num_blocks, Q_NOWAIT);
			if (retval != 0)
			{
				(*icbs.ctio_free_all)(ctio);
				create_sense_rsp(req, ABORTED_COMMAND, COMMAND_PHASE_ERROR_ASC, COMMAND_PHASE_ERROR_ASCQ);
				cmnd_skip_data(req);
				goto out;
			}
			ctio->dxfer_len = dxfer_len;
			if (req->pdu.datasize) {
				iscsi_cmnd_recv_pdu(conn, ctio, 0, req->pdu.datasize);
			}
			break;
		}
		case MODE_SELECT:
		case MODE_SELECT_10:
		case SEND_DIAGNOSTIC:
		case PERSISTENT_RESERVE_OUT:
		{
			int retval;

			retval = (*icbs.device_allocate_cmd_buffers)(ctio, Q_NOWAIT);
			if (retval != 0)
			{
				(*icbs.ctio_free_all)(ctio);
				create_sense_rsp(req, ABORTED_COMMAND, COMMAND_PHASE_ERROR_ASC, COMMAND_PHASE_ERROR_ASCQ);
				cmnd_skip_data(req);
				goto out;
			}

			if (req->pdu.datasize) {
				iscsi_cmnd_recv_pdu(conn, ctio, 0, req->pdu.datasize);
			}
			break;
		}

	}
	req->ctio = ctio;

out:
	return;
error:
	eprintk("Unsupported %x\n", req_hdr->scb[0]);
	(*icbs.ctio_free_all)(ctio);
	create_sense_rsp(req, ILLEGAL_REQUEST, 0x20, 0x0);
	cmnd_skip_data(req);
	return;
}

static void data_out_start(struct iscsi_conn *conn, struct iscsi_cmnd *cmnd)
{
	struct iscsi_data_out_hdr *req = (struct iscsi_data_out_hdr *)&cmnd->pdu.bhs;
	struct iscsi_cmnd *scsi_cmnd = NULL;
	u32 offset = be32_to_cpu(req->buffer_offset);

	update_stat_sn(cmnd);

	cmnd->req = scsi_cmnd = cmnd_find_hash(conn->session, req->itt, req->ttt);
	if (!scsi_cmnd) {
		eprintk("unable to find scsi task %x %x\n",
			cmnd_itt(cmnd), cmnd_ttt(cmnd));
		goto skip_data;
	}

	if (scsi_cmnd->r2t_length < cmnd->pdu.datasize) {
		eprintk("invalid data len %x %u %u\n",
			cmnd_itt(scsi_cmnd), cmnd->pdu.datasize, scsi_cmnd->r2t_length);
		goto skip_data;
	}

	if (scsi_cmnd->r2t_length + offset != cmnd_write_size(scsi_cmnd)) {
		eprintk("%x %u %u %u\n", cmnd_itt(scsi_cmnd), scsi_cmnd->r2t_length,
			offset,	cmnd_write_size(scsi_cmnd));
		goto skip_data;
	}

	scsi_cmnd->r2t_length -= cmnd->pdu.datasize;

	if (req->ttt == cpu_to_be32(ISCSI_RESERVED_TAG)) {
		/* unsolicited burst data */
		if (scsi_cmnd->pdu.bhs.flags & ISCSI_FLG_FINAL) {
			eprintk("unexpected data from %x %x\n",
				cmnd_itt(cmnd), cmnd_ttt(cmnd));
			goto skip_data;
		}
	}

	dprintk(D_WRITE, "%u %p %p %p %u %u\n", req->ttt, cmnd, scsi_cmnd,
		scsi_cmnd->tio, offset, cmnd->pdu.datasize);

	iscsi_cmnd_recv_pdu(conn, scsi_cmnd->ctio, offset, cmnd->pdu.datasize);
	return;
skip_data:
	cmnd->pdu.bhs.opcode = ISCSI_OP_DATA_REJECT;
	cmnd_skip_pdu(cmnd);
	return;
}

static void iscsi_session_push_cmnd(struct iscsi_cmnd *cmnd);

static void data_out_end(struct iscsi_conn *conn, struct iscsi_cmnd *cmnd)
{
	struct iscsi_data_out_hdr *req = (struct iscsi_data_out_hdr *) &cmnd->pdu.bhs;
	struct iscsi_cmnd *scsi_cmnd;
	u32 offset;

	assert(cmnd);
	scsi_cmnd = cmnd->req;
	assert(scsi_cmnd);

	if (conn->read_overflow) {
		eprintk("%x %u\n", cmnd_itt(cmnd), conn->read_overflow);
		assert(scsi_cmnd->tio);
		offset = be32_to_cpu(req->buffer_offset);
		offset += cmnd->pdu.datasize - conn->read_overflow;
		if (cmnd_recv_pdu(conn, scsi_cmnd->tio, offset, conn->read_overflow) < 0)
			assert(0);
		return;
	}

	if (req->ttt == cpu_to_be32(ISCSI_RESERVED_TAG)) {
		if (req->flags & ISCSI_FLG_FINAL) {
			scsi_cmnd->is_unsolicited_data = 0;
			iscsi_session_push_cmnd(scsi_cmnd);
		}
	} else {
		/* TODO : proper error handling */
		if (!(req->flags & ISCSI_FLG_FINAL) && scsi_cmnd->r2t_length == 0)
			eprintk("initiator error %x\n", cmnd_itt(scsi_cmnd));

		if (!(req->flags & ISCSI_FLG_FINAL))
			goto out;

		scsi_cmnd->outstanding_r2t--;

		if (scsi_cmnd->r2t_length == 0)
			assert(list_empty(&scsi_cmnd->pdu_list));

		iscsi_session_push_cmnd(scsi_cmnd);
	}

out:
	iscsi_cmnd_remove(cmnd);
	return;
}

static int __cmnd_abort(struct iscsi_cmnd *cmnd, struct iscsi_session *from_session)
{
	int task_found;

	if (cmnd_waitio(cmnd)) {
		struct iscsi_conn *conn = cmnd->conn;
		struct iscsi_session *session = conn->session;
		struct iscsi_target *target = session->target;
		uint64_t abort_i_prt[2];
		uint64_t abort_t_prt[2];

		abort_i_prt[0] = session->sid;
		abort_i_prt[1] = 0;
		abort_t_prt[0] = t_prt;
		abort_t_prt[1] = 0;

		task_found = (*icbs.device_istate_abort_task)(target->tdevice, abort_i_prt, abort_t_prt, TARGET_INT_ISCSI, cmnd_itt(cmnd));
		if (!task_found)
			return 0;
	}

	set_cmnd_tmfabort(cmnd);
	if (cmnd->conn->session != from_session)
		set_cmnd_sendabort(cmnd);
	return 1;
}

static int cmnd_abort(struct iscsi_session *session, struct iscsi_cmnd *req)
{
	struct iscsi_task_mgt_hdr *req_hdr =
				(struct iscsi_task_mgt_hdr *)&req->pdu.bhs;
	struct iscsi_cmnd *cmnd;
	int retval;

	u32 min_cmd_sn = req_hdr->cmd_sn - session->max_queued_cmnds;

	req_hdr->ref_cmd_sn = be32_to_cpu(req_hdr->ref_cmd_sn);

	dprintk(D_GENERIC, "cmd_sn(%u) ref_cmd_sn(%u) min_cmd_sn(%u) rtt(%x)"
		" lun(%d) cid(%u)\n",
		req_hdr->cmd_sn, req_hdr->ref_cmd_sn, min_cmd_sn, req_hdr->rtt,
		translate_lun(req_hdr->lun), req->conn->cid);

	if (after(req_hdr->ref_cmd_sn, req_hdr->cmd_sn))
		return ISCSI_RESPONSE_FUNCTION_REJECTED;

	if (!(cmnd = cmnd_find_hash(session, req_hdr->rtt, ISCSI_RESERVED_TAG))) {
		if (between(req_hdr->ref_cmd_sn, min_cmd_sn, req_hdr->cmd_sn))
			return ISCSI_RESPONSE_FUNCTION_COMPLETE;
		else
			return ISCSI_RESPONSE_UNKNOWN_TASK;
	}

	dprintk(D_GENERIC, "itt(%x) opcode(%x) scsicode(%x) lun(%d) cid(%u)\n",
		cmnd_itt(cmnd), cmnd_opcode(cmnd), cmnd_scsicode(cmnd),
		translate_lun(cmnd_hdr(cmnd)->lun), cmnd->conn->cid);

	if (cmnd_opcode(cmnd) == ISCSI_OP_SCSI_TASK_MGT_MSG)
		return ISCSI_RESPONSE_FUNCTION_REJECTED;

	if (translate_lun(cmnd_hdr(cmnd)->lun) !=
						translate_lun(req_hdr->lun))
		return ISCSI_RESPONSE_FUNCTION_REJECTED;

	if (cmnd->conn && test_bit(CONN_ACTIVE, &cmnd->conn->state)) {
		if (cmnd->conn->cid != req->conn->cid)
			return ISCSI_RESPONSE_FUNCTION_REJECTED;
	} else {
		/* Switch cmnd connection allegiance */
	}

	retval = __cmnd_abort(cmnd, session);
	if (retval != 0)
		return ISCSI_RESPONSE_FUNCTION_REJECTED;

	return ISCSI_RESPONSE_FUNCTION_COMPLETE;
}

static int target_reset(struct iscsi_cmnd *req, u32 lun, int all)
{
	struct iscsi_target *target = req->conn->session->target;
	struct iscsi_session *session;
	struct iscsi_conn *conn;
	struct iscsi_cmnd *cmnd, *tmp;
	uint64_t reset_i_prt[2];
	uint64_t reset_t_prt[2];

	list_for_each_entry(session, &target->session_list, list) {
		list_for_each_entry(conn, &session->conn_list, list) {
			list_for_each_entry_safe(cmnd, tmp, &conn->pdu_list, conn_list) {
				if (cmnd == req)
					continue;

				if (all)
					__cmnd_abort(cmnd, req->conn->session);
				else if (translate_lun(cmnd_hdr(cmnd)->lun) == lun)
					__cmnd_abort(cmnd, req->conn->session);
			}
		}
	}

	reset_i_prt[0] = req->conn->session->sid;
	reset_i_prt[1] = 0;
	reset_t_prt[0] = t_prt;
	reset_t_prt[1] = 0;
	(*icbs.device_target_reset)(target->tdevice, reset_i_prt, reset_t_prt, TARGET_INT_ISCSI);
	return 0;
}

static void task_set_abort(struct iscsi_cmnd *req)
{
	struct iscsi_session *session = req->conn->session;
	struct iscsi_conn *conn;
	struct iscsi_cmnd *cmnd, *tmp;
	uint64_t abort_i_prt[2];
	uint64_t abort_t_prt[2];

	abort_i_prt[0] = session->sid;
	abort_i_prt[1] = 0;
	abort_t_prt[0] = t_prt;
	abort_t_prt[1] = 0;

	(*icbs.device_istate_abort_task_set)(NULL, abort_i_prt, abort_t_prt, TARGET_INT_ISCSI);

	list_for_each_entry(conn, &session->conn_list, list) {
		list_for_each_entry_safe(cmnd, tmp, &conn->pdu_list, conn_list) {
			if (translate_lun(cmnd_hdr(cmnd)->lun)
					!= translate_lun(cmnd_hdr(req)->lun))
				continue;

			if (before(cmnd_hdr(cmnd)->cmd_sn,
					cmnd_hdr(req)->cmd_sn))
				__cmnd_abort(cmnd, req->conn->session);
		}
	}
}

static inline char *tmf_desc(int fun)
{
	static char *tmf_desc[] = {
		"Unknown Function",
		"Abort Task",
		"Abort Task Set",
		"Clear ACA",
		"Clear Task Set",
		"Logical Unit Reset",
		"Target Warm Reset",
		"Target Cold Reset",
		"Task Reassign",
        };

	if ((fun < ISCSI_FUNCTION_ABORT_TASK) ||
				(fun > ISCSI_FUNCTION_TASK_REASSIGN))
		fun = 0;

	return tmf_desc[fun];
}

static inline char *rsp_desc(int rsp)
{
	static char *rsp_desc[] = {
		"Function Complete",
		"Unknown Task",
		"Unknown LUN",
		"Task Allegiant",
		"Failover Unsupported",
		"Function Unsupported",
		"No Authorization",
		"Function Rejected",
		"Unknown Response",
	};

	if (((rsp < ISCSI_RESPONSE_FUNCTION_COMPLETE) ||
			(rsp > ISCSI_RESPONSE_NO_AUTHORIZATION)) &&
			(rsp != ISCSI_RESPONSE_FUNCTION_REJECTED))
		rsp = 8;
	else if (rsp == ISCSI_RESPONSE_FUNCTION_REJECTED)
		rsp = 7;

	return rsp_desc[rsp];
}

static void execute_task_management(struct iscsi_cmnd *req)
{
	struct iscsi_conn *conn = req->conn;
	struct iscsi_cmnd *rsp;
	struct iscsi_task_mgt_hdr *req_hdr = (struct iscsi_task_mgt_hdr *)&req->pdu.bhs;
	struct iscsi_task_rsp_hdr *rsp_hdr;
	int function = req_hdr->function & ISCSI_FUNCTION_MASK;

	rsp = iscsi_cmnd_create_rsp_cmnd(req, 1);
	rsp_hdr = (struct iscsi_task_rsp_hdr *)&rsp->pdu.bhs;

	rsp_hdr->opcode = ISCSI_OP_SCSI_TASK_MGT_RSP;
	rsp_hdr->flags = ISCSI_FLG_FINAL;
	rsp_hdr->itt = req_hdr->itt;
	rsp_hdr->response = ISCSI_RESPONSE_FUNCTION_COMPLETE;

	eprintk("%x %d %x\n", cmnd_itt(req), function, req_hdr->rtt);

	switch (function) {
	case ISCSI_FUNCTION_ABORT_TASK:
		rsp_hdr->response = cmnd_abort(conn->session, req);
		break;
	case ISCSI_FUNCTION_ABORT_TASK_SET:
		task_set_abort(req);
		break;
	case ISCSI_FUNCTION_CLEAR_ACA:
		rsp_hdr->response = ISCSI_RESPONSE_FUNCTION_UNSUPPORTED;
		break;
	case ISCSI_FUNCTION_CLEAR_TASK_SET:
		rsp_hdr->response = ISCSI_RESPONSE_FUNCTION_UNSUPPORTED;
		break;
	case ISCSI_FUNCTION_LOGICAL_UNIT_RESET:
		target_reset(req, translate_lun(req_hdr->lun), 0);
		break;
	case ISCSI_FUNCTION_TARGET_WARM_RESET:
	case ISCSI_FUNCTION_TARGET_COLD_RESET:
		target_reset(req, 0, 1);
		if (function == ISCSI_FUNCTION_TARGET_COLD_RESET)
			set_cmnd_close(rsp);
		break;
	case ISCSI_FUNCTION_TASK_REASSIGN:
		rsp_hdr->response = ISCSI_RESPONSE_FUNCTION_UNSUPPORTED;
		break;
	default:
		rsp_hdr->response = ISCSI_RESPONSE_FUNCTION_REJECTED;
		break;
	}
	iscsi_cmnd_init_write(rsp, 0);
}

static void nop_hdr_setup(struct iscsi_hdr *hdr, u8 opcode, __be32 itt,
			  __be32 ttt)
{
	hdr->opcode = opcode;
	hdr->flags = ISCSI_FLG_FINAL;
	hdr->itt = itt;
	hdr->ttt = ttt;
}

static void nop_out_exec(struct iscsi_cmnd *req)
{
	struct iscsi_cmnd *rsp;

	if (cmnd_itt(req) != cpu_to_be32(ISCSI_RESERVED_TAG)) {
		rsp = iscsi_cmnd_create_rsp_cmnd(req, 1);

		nop_hdr_setup(&rsp->pdu.bhs, ISCSI_OP_NOP_IN, req->pdu.bhs.itt,
			      cpu_to_be32(ISCSI_RESERVED_TAG));

		if (req->pdu.datasize)
			assert(req->tio);
		else
			assert(!req->tio);

		if (req->tio) {
			tio_get(req->tio);
			rsp->tio = req->tio;
		}

		assert(get_pgcnt(req->pdu.datasize, 0) < ISCSI_CONN_IOV_MAX);
		rsp->pdu.datasize = req->pdu.datasize;
		iscsi_cmnd_init_write(rsp, 0);
	} else {
		if (req->req) {
			dprintk(D_GENERIC, "releasing NOP-Out %p, ttt %x; "
				"removing NOP-In %p, ttt %x\n", req->req,
				cmnd_ttt(req->req), req, cmnd_ttt(req));
			cmnd_release(req->req, 0);
		}
		iscsi_cmnd_remove(req);
	}
}

#ifdef LINUX
static void nop_in_timeout(unsigned long data)
#else
static void nop_in_timeout(void *data)
#endif
{
	struct iscsi_cmnd *req = (struct iscsi_cmnd *)data;

	printk(KERN_INFO "NOP-In ping timed out - closing sid:cid %llu:%u\n",
	       (unsigned long long)req->conn->session->sid, req->conn->cid);
	clear_cmnd_timer_active(req);
	conn_close(req->conn);
}

/* create a fake NOP-Out req and treat the NOP-In as our rsp to it */
void send_nop_in(struct iscsi_conn *conn)
{
	struct iscsi_cmnd *req = cmnd_alloc(conn, 1);
	struct iscsi_cmnd *rsp = iscsi_cmnd_create_rsp_cmnd(req, 0);

	req->target_task_tag = get_next_ttt(conn->session);

	nop_hdr_setup(&req->pdu.bhs, ISCSI_OP_NOP_OUT,
		      cpu_to_be32(ISCSI_RESERVED_TAG), req->target_task_tag);
	nop_hdr_setup(&rsp->pdu.bhs, ISCSI_OP_NOP_IN,
		      cpu_to_be32(ISCSI_RESERVED_TAG), req->target_task_tag);

	dprintk(D_GENERIC, "NOP-Out: %p, ttt %x, timer %p; "
		"NOP-In: %p, ttt %x;\n", req, cmnd_ttt(req), &req->timer, rsp,
		cmnd_ttt(rsp));

	callout_init(&req->timer, CALLOUT_MPSAFE);
#ifdef LINUX
	req->timer.data = (unsigned long)req;
	req->timer.function = nop_in_timeout;
#endif

	if (cmnd_insert_hash_ttt(req, req->target_task_tag)) {
		eprintk("%s\n",
			"failed to insert fake NOP-Out into hash table");
		cmnd_release(rsp, 0);
		cmnd_release(req, 0);
	} else
		iscsi_cmnd_init_write(rsp, 0);
}

static void nop_in_tx_end(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn = cmnd->conn;
	u32 t;

	if (cmnd->pdu.bhs.ttt == cpu_to_be32(ISCSI_RESERVED_TAG))
		return;

	/*
	 * NOP-In ping issued by the target.
	 * FIXME: Sanitize the NOP timeout earlier, during configuration
	 */
	t = conn->session->target->trgt_param.nop_timeout;

	if (!t || t > conn->session->target->trgt_param.nop_interval) {
		eprintk("Adjusting NOPTimeout of tid %u from %u to %u "
			"(== NOPInterval)\n", conn->session->target->tid,
			t,
			conn->session->target->trgt_param.nop_interval);
		t = conn->session->target->trgt_param.nop_interval;
		conn->session->target->trgt_param.nop_timeout = t;
	}

	dprintk(D_GENERIC, "NOP-In %p, %x: timer %p\n", cmnd, cmnd_ttt(cmnd),
		&cmnd->req->timer);

	set_cmnd_timer_active(cmnd->req);
	callout_reset(&cmnd->req->timer, jiffies + HZ * t, nop_in_timeout, cmnd->req);
}

static void logout_exec(struct iscsi_cmnd *req)
{
	struct iscsi_logout_req_hdr *req_hdr;
	struct iscsi_cmnd *rsp;
	struct iscsi_logout_rsp_hdr *rsp_hdr;

	req_hdr = (struct iscsi_logout_req_hdr *)&req->pdu.bhs;
	rsp = iscsi_cmnd_create_rsp_cmnd(req, 1);
	rsp_hdr = (struct iscsi_logout_rsp_hdr *)&rsp->pdu.bhs;
	rsp_hdr->opcode = ISCSI_OP_LOGOUT_RSP;
	rsp_hdr->flags = ISCSI_FLG_FINAL;
	rsp_hdr->itt = req_hdr->itt;
	set_cmnd_close(rsp);
	iscsi_cmnd_init_write(rsp, 0);
}

static void iscsi_cmnd_exec(struct iscsi_cmnd *cmnd)
{
	dprintk(D_GENERIC, "%p,%x,%u\n", cmnd, cmnd_opcode(cmnd), cmnd->pdu.bhs.sn);

	switch (cmnd_opcode(cmnd)) {
	case ISCSI_OP_NOP_OUT:
		nop_out_exec(cmnd);
		break;
	case ISCSI_OP_SCSI_CMD:
		scsi_cmnd_exec(cmnd);
		break;
	case ISCSI_OP_SCSI_TASK_MGT_MSG:
		execute_task_management(cmnd);
		break;
	case ISCSI_OP_LOGOUT_CMD:
		logout_exec(cmnd);
		break;
	case ISCSI_OP_SCSI_REJECT:
		iscsi_cmnd_init_write(get_rsp_cmnd(cmnd), 0);
		break;
	case ISCSI_OP_TEXT_CMD:
	case ISCSI_OP_SNACK_CMD:
		break;
	default:
		eprintk("unexpected cmnd op %x\n", cmnd_opcode(cmnd));
		break;
	}
}

static void __cmnd_send_pdu(struct iscsi_conn *conn, struct tio *tio, u32 offset, u32 size)
{
	dprintk(D_GENERIC, "%p %u,%u\n", tio, offset, size);
	offset += tio->offset;

	assert(offset <= tio->offset + tio->size);
	assert(offset + size <= tio->offset + tio->size);

	conn->write_tcmnd = tio;
	conn->write_offset = offset;
	conn->write_size += size;
}

static void cmnd_send_pdu(struct iscsi_conn *conn, struct iscsi_cmnd *cmnd)
{
	u32 size;
	struct tio *tio;

	if (!cmnd->pdu.datasize)
		return;

	size = (cmnd->pdu.datasize + 3) & -4;
	tio = cmnd->tio;
	assert(tio);
	assert(tio->size == size);
	__cmnd_send_pdu(conn, tio, 0, size);
}

#ifdef FREEBSD
static void set_cork(struct socket *sock, int on)
{
	struct sockopt sopt;

	sopt.sopt_dir = SOPT_SET;
        sopt.sopt_level = IPPROTO_TCP;
        sopt.sopt_name = TCP_NOPUSH;
        sopt.sopt_val = (caddr_t)&on;
        sopt.sopt_valsize = sizeof(on);
        sopt.sopt_td = NULL;
	sosetopt(sock, &sopt);
}
#else 
static void set_cork(struct socket *sock, int on)
{
	int opt = on;
	mm_segment_t oldfs;

	oldfs = get_fs();
	set_fs(get_ds());
	sock->ops->setsockopt(sock, SOL_TCP, TCP_CORK, (void *)&opt, sizeof(opt));
	set_fs(oldfs);
}
#endif 

void cmnd_release(struct iscsi_cmnd *cmnd, int force)
{
	struct iscsi_cmnd *req, *rsp;
	int is_last = 0;

	if (!cmnd)
		return;

/* 	eprintk("%x %lx %d\n", cmnd_opcode(cmnd), cmnd->flags, force); */

	req = cmnd->req;
	is_last = cmnd_final(cmnd);

	if (force) {
		while (!list_empty(&cmnd->pdu_list)) {
			rsp = list_entry(cmnd->pdu_list.next, struct iscsi_cmnd, pdu_list);
			list_del_init(&rsp->list);
			list_del(&rsp->pdu_list);
			iscsi_cmnd_remove(rsp);
		}
		list_del_init(&cmnd->list);
	}

	if (cmnd_hashed(cmnd))
		cmnd_remove_hash(cmnd);

	list_del_init(&cmnd->pdu_list);
	iscsi_cmnd_remove(cmnd);

	if (is_last) {
		assert(!force);
		assert(req);
		cmnd_release(req, 0);
	}

	return;
}

void cmnd_tx_start(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn = cmnd->conn;
	struct iovec *iop;

	dprintk(D_GENERIC, "%p:%x\n", cmnd, cmnd_opcode(cmnd));
	assert(cmnd);
	iscsi_cmnd_set_length(&cmnd->pdu);

	set_cork(conn->sock, 1);

	conn->write_iop = iop = conn->write_iov;
	iop->iov_base = &cmnd->pdu.bhs;
	iop->iov_len = sizeof(cmnd->pdu.bhs);
	iop++;
	conn->write_size = sizeof(cmnd->pdu.bhs);

	switch (cmnd_opcode(cmnd)) {
	case ISCSI_OP_NOP_IN:
		if (cmnd->pdu.bhs.itt == ISCSI_RESERVED_TAG) {
			/* NOP-In ping generated by us. Don't advance StatSN. */
			cmnd_set_sn(cmnd, 0);
			cmnd_set_sn(cmnd->req, 0);
			cmnd->pdu.bhs.sn = cpu_to_be32(conn->stat_sn);
			cmnd->req->pdu.bhs.sn = cpu_to_be32(conn->stat_sn);
		} else
			cmnd_set_sn(cmnd, 1);
		cmnd_send_pdu(conn, cmnd);
		break;
	case ISCSI_OP_SCSI_RSP:
		cmnd_set_sn(cmnd, 1);
		if (cmnd->ctio)
			iscsi_cmnd_send_pdu(conn, cmnd);
		else
			cmnd_send_pdu(conn, cmnd);
		break;
	case ISCSI_OP_SCSI_TASK_MGT_RSP:
		cmnd_set_sn(cmnd, 1);
		break;
	case ISCSI_OP_TEXT_RSP:
		cmnd_set_sn(cmnd, 1);
		break;
	case ISCSI_OP_SCSI_DATA_IN:
	{
		struct iscsi_data_in_hdr *rsp = (struct iscsi_data_in_hdr *)&cmnd->pdu.bhs;

		cmnd_set_sn(cmnd, (rsp->flags & ISCSI_FLG_STATUS) ? 1 : 0);
		iscsi_cmnd_send_pdu(conn, cmnd);
		break;
	}
	case ISCSI_OP_LOGOUT_RSP:
		cmnd_set_sn(cmnd, 1);
		break;
	case ISCSI_OP_R2T:
		cmnd_set_sn(cmnd, 0);
		cmnd->pdu.bhs.sn = cpu_to_be32(conn->stat_sn);
		break;
	case ISCSI_OP_ASYNC_MSG:
		cmnd_set_sn(cmnd, 1);
		break;
	case ISCSI_OP_REJECT:
		cmnd_set_sn(cmnd, 1);
		cmnd_send_pdu(conn, cmnd);
		break;
	default:
		eprintk("unexpected cmnd op %x\n", cmnd_opcode(cmnd));
		break;
	}

	iop->iov_len = 0;
	// move this?
	if (!conn->ctio)
	{
		conn->write_size = (conn->write_size + 3) & -4;
	}
#ifdef ENABLE_DEBUG
	iscsi_dump_pdu(&cmnd->pdu);
#endif
}

void cmnd_tx_end(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn = cmnd->conn;

	dprintk(D_GENERIC, "%p:%x\n", cmnd, cmnd_opcode(cmnd));
	switch (cmnd_opcode(cmnd)) {
	case ISCSI_OP_NOP_IN:
		nop_in_tx_end(cmnd);
		break;
	case ISCSI_OP_SCSI_RSP:
	case ISCSI_OP_SCSI_TASK_MGT_RSP:
	case ISCSI_OP_TEXT_RSP:
	case ISCSI_OP_R2T:
	case ISCSI_OP_ASYNC_MSG:
	case ISCSI_OP_REJECT:
	case ISCSI_OP_SCSI_DATA_IN:
	case ISCSI_OP_LOGOUT_RSP:
		break;
	default:
		eprintk("unexpected cmnd op %x\n", cmnd_opcode(cmnd));
		assert(0);
		break;
	}

	if (cmnd_close(cmnd))
		conn_close(conn);

	list_del_init(&cmnd->list);
	set_cork(cmnd->conn->sock, 0);
}

/**
 * Push the command for execution.
 * This functions reorders the commands.
 * Called from the read thread.
 *
 * iscsi_session_push_cmnd -
 * @cmnd: ptr to command
 */

static void iscsi_session_push_cmnd(struct iscsi_cmnd *cmnd)
{
	struct iscsi_session *session = cmnd->conn->session;
	struct list_head *entry;
	u32 cmd_sn;

	if (cmnd->r2t_length) {
		if (!cmnd->is_unsolicited_data)
			send_r2t(cmnd);
		return;
	}

	dprintk(D_GENERIC, "%p:%x %u,%u\n",
		cmnd, cmnd_opcode(cmnd), cmnd->pdu.bhs.sn, session->exp_cmd_sn);

	if (cmnd_immediate(cmnd)) {
		iscsi_cmnd_exec(cmnd);
		return;
	}

	cmd_sn = cmnd->pdu.bhs.sn;
	if (cmd_sn == session->exp_cmd_sn) {
		while (1) {
			session->exp_cmd_sn = ++cmd_sn;
			iscsi_cmnd_exec(cmnd);

			if (list_empty(&session->pending_list))
				break;

			cmnd = list_entry(session->pending_list.next, struct iscsi_cmnd, list);
			if (cmnd->pdu.bhs.sn != cmd_sn)
				break;

			list_del_init(&cmnd->list);
			clear_cmnd_pending(cmnd);
		}
	} else {
		set_cmnd_pending(cmnd);

		list_for_each(entry, &session->pending_list) {
			struct iscsi_cmnd *tmp = list_entry(entry, struct iscsi_cmnd, list);
			if (before(cmd_sn, tmp->pdu.bhs.sn))
				break;
		}

		assert(list_empty(&cmnd->list));

		list_add_tail(&cmnd->list, entry);
	}
}

static int check_segment_length(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn = cmnd->conn;
	struct iscsi_sess_param *param = &conn->session->param;

	if (cmnd->pdu.datasize > param->max_recv_data_length) {
		eprintk("data too long %x %u %u\n", cmnd_itt(cmnd),
			cmnd->pdu.datasize, param->max_recv_data_length);
		conn_close(conn);
		return -EINVAL;
	}

	return 0;
}

void cmnd_rx_start(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn = cmnd->conn;
	int err = 0;

	iscsi_dump_pdu(&cmnd->pdu);

	set_cmnd_rxstart(cmnd);
	if (check_segment_length(cmnd) < 0)
		return;

	switch (cmnd_opcode(cmnd)) {
	case ISCSI_OP_NOP_OUT:
		err = nop_out_start(conn, cmnd);
		break;
	case ISCSI_OP_SCSI_CMD:
		if (!(err = cmnd_insert_hash(cmnd)))
			scsi_cmnd_start(conn, cmnd);
		break;
	case ISCSI_OP_SCSI_TASK_MGT_MSG:
		err = cmnd_insert_hash(cmnd);
		break;
	case ISCSI_OP_SCSI_DATA_OUT:
		data_out_start(conn, cmnd);
		break;
	case ISCSI_OP_LOGOUT_CMD:
		err = cmnd_insert_hash(cmnd);
		break;
	case ISCSI_OP_TEXT_CMD:
	case ISCSI_OP_SNACK_CMD:
		err = -ISCSI_REASON_UNSUPPORTED_COMMAND;
		break;
	default:
		err = -ISCSI_REASON_UNSUPPORTED_COMMAND;
		break;
	}

	if (err < 0) {
		eprintk("%x %x %d\n", cmnd_opcode(cmnd), cmnd_itt(cmnd), err);
		iscsi_cmnd_reject(cmnd, -err);
	}
}

void cmnd_rx_end(struct iscsi_cmnd *cmnd)
{
	struct iscsi_conn *conn = cmnd->conn;

#if 0
	if (cmnd_tmfabort(cmnd)) {
		if (!cmnd_sendabort(cmnd)) {
			cmnd_release(cmnd, 1);
		}
		else {
			if (cmnd->ctio) {
				(*icbs.ctio_free_all)(cmnd->ctio);
				cmnd->ctio = NULL;
			}
			create_sense_rsp(cmnd, ABORTED_COMMAND, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASC, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASCQ);
			return;
		}
	}
#endif

	dprintk(D_GENERIC, "%p:%x\n", cmnd, cmnd_opcode(cmnd));
	switch (cmnd_opcode(cmnd)) {
	case ISCSI_OP_SCSI_REJECT:
	case ISCSI_OP_NOP_OUT:
	case ISCSI_OP_SCSI_CMD:
	case ISCSI_OP_SCSI_TASK_MGT_MSG:
	case ISCSI_OP_TEXT_CMD:
	case ISCSI_OP_LOGOUT_CMD:
		iscsi_session_push_cmnd(cmnd);
		break;
	case ISCSI_OP_SCSI_DATA_OUT:
		data_out_end(conn, cmnd);
		break;
	case ISCSI_OP_SNACK_CMD:
		break;
	case ISCSI_OP_PDU_REJECT:
		iscsi_cmnd_init_write(get_rsp_cmnd(cmnd), 0);
		break;
	case ISCSI_OP_DATA_REJECT:
		cmnd_release(cmnd, 0);
		break;
	default:
		eprintk("unexpected cmnd op %x\n", cmnd_opcode(cmnd));
		BUG();
		break;
	}
}

#ifdef FREEBSD
struct cdev *ietdev;
static struct cdevsw iet_csw = {
	.d_version = D_VERSION,
	.d_ioctl = iet_ioctl,
	.d_mmap = iet_mmap, 
	.d_poll = iet_poll,
};
#endif

struct qs_interface_cbs icbs = {
	.new_device = iscsi_target_new_device_cb,
	.remove_device = iscsi_target_remove_device_cb,
	.disable_device = iscsi_target_disable_device_cb,
	.interface = TARGET_INT_ISCSI,
};

static void iscsi_exit(void)
{
	target_del_all();

	vtdevice_unregister_interface(&icbs);
#ifdef LINUX
	unregister_chrdev(ctr_major, ctr_name);
#else
	destroy_dev(ietdev);
#endif

#ifdef LINUX
	iet_procfs_exit();

	event_exit();
#else
	iet_mmap_exit();
#endif

	tio_exit();


	if (iscsi_cmnd_cache)
		slab_cache_destroy(iscsi_cmnd_cache);
}

extern wait_queue_head_t iscsi_ctl_wait;
extern mutex_t ioctl_sem;
extern mutex_t target_list_sem; 

static int iscsi_init(void)
{
	int err = -ENOMEM;

	init_waitqueue_head(&iscsi_ctl_wait);
	sx_init(&ioctl_sem, "iet ioctl");
	sx_init(&target_list_sem, "iet tgt sem");

#ifdef LINUX
	if ((ctr_major = register_chrdev(0, ctr_name, &ctr_fops)) < 0) {
		eprintk("failed to register the control device %d\n", ctr_major);
		return ctr_major;
	}
#else
	ietdev = make_dev(&iet_csw, 0, UID_ROOT, GID_WHEEL, 0550, ctr_name);
#endif

#ifdef LINUX
	if ((err = iet_procfs_init()) < 0)
		goto err;

	if ((err = event_init()) < 0)
		goto err;
#else
	if ((err = iet_mmap_init()) < 0)
		goto err;
#endif

#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	iscsi_cmnd_cache = slab_cache_create("iscmnd", sizeof(struct iscsi_cmnd),
					     0, 0, NULL, NULL);
#else
	iscsi_cmnd_cache = slab_cache_create("iscmnd", sizeof(struct iscsi_cmnd),
					     0, 0, NULL);
#endif
#else
	iscsi_cmnd_cache = slab_cache_create("iscmnd", sizeof(struct iscsi_cmnd),
					     NULL, NULL, NULL, NULL, 0, 0);
#endif
	if (!iscsi_cmnd_cache)
		goto err;

	if ((err = tio_init()) < 0)
		goto err;

	err = vtdevice_register_interface(&icbs);
	if (err != 0)
		goto err;

	return 0;

err:
	iscsi_exit();
	return err;
}

#ifdef LINUX
module_param(debug_enable_flags, ulong, S_IRUGO);

module_init(iscsi_init);
module_exit(iscsi_exit);

MODULE_VERSION(IET_VERSION_STRING);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("iSCSI Enterprise Target");
MODULE_AUTHOR("IET development team <iscsitarget-devel@lists.sourceforge.net>");
#else
struct module *ietmod;

static int
event_handler(struct module *module, int event, void *arg) {
	int retval = 0;
	switch (event) {
		case MOD_LOAD:
			retval = iscsi_init();
			if (retval == 0)
				ietmod = module;
			break;
		case MOD_UNLOAD:
			iscsi_exit();
			break;
		default:
			retval = EOPNOTSUPP;
			break;
	}
        return retval;
}

static moduledata_t iet_info = {
    "iet",    /* module name */
     event_handler,  /* event handler */
     NULL            /* extra data */
};

MALLOC_DEFINE(M_IET, "iet core", "IET allocations");
MALLOC_DEFINE(M_IETCONN, "iet conn", "IET allocations");
MALLOC_DEFINE(M_IETAHS, "iet ahs", "IET allocations");
MALLOC_DEFINE(M_IETTARG, "iet targ", "IET allocations");
MALLOC_DEFINE(M_IETSESS, "iet sess", "IET allocations");
MALLOC_DEFINE(M_IETTIO, "iet tio", "IET allocations");

DECLARE_MODULE(iet, iet_info, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(iet, 1);
MODULE_DEPEND(iet, tldev, 1, 1, 2);
#endif

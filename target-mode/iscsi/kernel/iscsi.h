/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#ifndef __ISCSI_H__
#define __ISCSI_H__

#include "iscsi_hdr.h"
#include "iet_u.h"
#ifdef FREEBSD
#include "crc32c.h"
#endif

struct iscsi_sess_param {
	int initial_r2t;
	int immediate_data;
	int max_connections;
	int max_recv_data_length;
	int max_xmit_data_length;
	int max_burst_length;
	int first_burst_length;
	int default_wait_time;
	int default_retain_time;
	int max_outstanding_r2t;
	int data_pdu_inorder;
	int data_sequence_inorder;
	int error_recovery_level;
	int header_digest;
	int data_digest;
	int ofmarker;
	int ifmarker;
	int ofmarkint;
	int ifmarkint;
};

struct iscsi_trgt_param {
	int wthreads;
	int target_type;
	int queued_cmnds;
	int nop_interval;
	int nop_timeout;
};

struct tio {
	u32 pg_cnt;

	pgoff_t idx;
	u32 offset;
	u32 size;

	pagestruct_t **pvec;
	atomic_t count;
};

struct network_thread_info {
	kproc_t *task;
	unsigned long flags;
	struct list_head active_conns;

	spinlock_t nthread_lock;
	wait_queue_head_t nthread_wait;
#ifdef LINUX
	void (*old_state_change)(struct sock *);
	void (*old_data_ready)(struct sock *, int);
	void (*old_write_space)(struct sock *);
#endif
};

struct iscsi_cmnd;

struct target_type {
	int id;
	int (*execute_cmnd) (struct iscsi_cmnd *);
};

enum iscsi_device_state {
	IDEV_RUNNING,
	IDEV_DEL,
};

struct iscsi_target {
	struct list_head t_list;
	u32 tid;

	char name[ISCSI_NAME_LEN];

	struct iscsi_sess_param sess_param;
	struct iscsi_trgt_param trgt_param;

	struct list_head session_list;

	/* Prevents races between add/del session and adding UAs */
	spinlock_t session_list_lock;

	struct network_thread_info nthread_info;

	mutex_t target_sem;

	spinlock_t ctio_lock;

	/* quadstor fields */
	atomic_t del;
	atomic_t disabled;
	void *tdevice;
};

struct iscsi_queue {
	spinlock_t queue_lock;
	struct iscsi_cmnd *ordered_cmnd;
	struct list_head wait_list;
	int active_cnt;
};

struct iet_volume {
	u32 lun;

	enum iscsi_device_state l_state;
	atomic_t l_count;

	struct iscsi_target *target;
	struct list_head list;

	struct iscsi_queue queue;

	u8 scsi_id[SCSI_ID_LEN];
	u8 scsi_sn[SCSI_SN_LEN + 1];

	u32 blk_shift;
	u64 blk_cnt;

	u64 reserve_sid;
	spinlock_t reserve_lock;

	unsigned long flags;

	struct iotype *iotype;
	void *private;
};

enum lu_flags {
	LU_READONLY,
	LU_ASYNC,
};

#define LUReadonly(lu) test_bit(LU_READONLY, &(lu)->flags)
#define SetLUReadonly(lu) set_bit(LU_READONLY, &(lu)->flags)

#define LUAsync(lu) test_bit(LU_ASYNC, &(lu)->flags)
#define SetLUAsync(lu) set_bit(LU_ASYNC, &(lu)->flags)

#define IET_HASH_ORDER		8
#define	cmnd_hashfn(itt)	hash_long((itt), IET_HASH_ORDER)

struct iscsi_session {
	struct list_head list;
	struct iscsi_target *target;
	completion_t *done;
	char *initiator;
	u64 sid;

	u32 exp_cmd_sn;
	u32 max_cmd_sn;

	struct iscsi_sess_param param;
	u32 max_queued_cmnds;

	struct list_head conn_list;

	struct list_head pending_list;

	spinlock_t cmnd_hash_lock;
	struct list_head cmnd_hash[1 << IET_HASH_ORDER];

	u32 next_ttt;
};

enum connection_state_bit {
	CONN_ACTIVE,
	CONN_CLOSING,
	CONN_WSPACE_WAIT,
	CONN_NEED_NOP_IN,
};

#define ISCSI_CONN_IOV_MAX	(((256 << 10) >> PAGE_SHIFT) + 1)

struct qsio_scsiio;
struct qsio_accept_tio;

struct iscsi_conn {
	struct list_head list;			/* list entry in session list */
	struct iscsi_session *session;		/* owning session */

	u16 cid;
	unsigned long state;

	u32 stat_sn;
	u32 exp_stat_sn;

	int hdigest_type;
	int ddigest_type;

	struct list_head poll_list;
	struct file *file;
	struct socket *sock;
	spinlock_t list_lock;
	atomic_t nr_cmnds;
	atomic_t nr_busy_cmnds;
	struct list_head pdu_list;		/* in/outcoming pdus */
	struct list_head write_list;		/* list of data pdus to be sent */
	callout_t nop_timer;

	struct iscsi_cmnd *read_cmnd;
	struct msghdr read_msg;
	struct iovec read_iov[ISCSI_CONN_IOV_MAX];
	u32 read_size;
	u32 read_overflow;
	int read_state;
	u32 read_offset;
	struct qsio_scsiio *read_ctio;

	struct iscsi_cmnd *write_cmnd;
	struct iovec write_iov[ISCSI_CONN_IOV_MAX];
	struct iovec *write_iop;
	struct tio *write_tcmnd;
	struct qsio_scsiio *ctio;
	u32 write_size;
	u32 write_offset;
	int write_state;
#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	struct crypto_tfm *rx_digest_tfm;
	struct crypto_tfm *tx_digest_tfm;
#else
	struct hash_desc rx_hash;
	struct hash_desc tx_hash;
#endif
	struct scatterlist hash_sg[ISCSI_CONN_IOV_MAX];
#else
	struct chksum_ctx rx_ctx;
	struct chksum_ctx tx_ctx; 
#endif
};

struct iscsi_pdu {
	struct iscsi_hdr bhs;
	void *ahs;
	unsigned int ahssize;
	unsigned int datasize;
};

struct seq_file;
typedef void (iet_show_info_t)(struct seq_file *seq, struct iscsi_target *target);

struct iscsi_cmnd {
	struct list_head list;
	struct list_head conn_list;
	unsigned long flags;
	struct iscsi_conn *conn;

	struct iscsi_pdu pdu;
	struct list_head pdu_list;

	struct list_head hash_list;

	struct tio *tio;
	struct qsio_scsiio *ctio;

	u8 status;

	callout_t timer;

	u32 r2t_sn;
	u32 r2t_length;
	u32 is_unsolicited_data;
	u32 target_task_tag;
	u32 outstanding_r2t;

	u32 hdigest;
	u32 ddigest;

	u32 write_iov_offset;
	u32 write_iov_len;

	uint16_t start_pg_idx;
	uint16_t start_pg_offset;
	uint16_t orig_start_pg_idx;
	uint16_t orig_start_pg_offset;
	uint16_t end_pg_idx;
	uint16_t end_pg_offset;
	uint16_t read_pg_idx;
	uint16_t read_pg_offset;

	struct iscsi_cmnd *req;
};

#define ISCSI_OP_SCSI_REJECT	ISCSI_OP_VENDOR1_CMD
#define ISCSI_OP_PDU_REJECT	ISCSI_OP_VENDOR2_CMD
#define ISCSI_OP_DATA_REJECT	ISCSI_OP_VENDOR3_CMD
#define ISCSI_OP_SCSI_ABORT	ISCSI_OP_VENDOR4_CMD

/* iscsi.c */
extern struct iscsi_cmnd *cmnd_alloc(struct iscsi_conn *, int);
extern void cmnd_rx_start(struct iscsi_cmnd *);
extern void cmnd_rx_end(struct iscsi_cmnd *);
extern void cmnd_tx_start(struct iscsi_cmnd *);
extern void cmnd_tx_end(struct iscsi_cmnd *);
extern void cmnd_release(struct iscsi_cmnd *, int);
extern void send_data_rsp(struct iscsi_cmnd *, int (*)(struct iscsi_cmnd *));
extern void send_scsi_rsp(struct iscsi_cmnd *, int (*)(struct iscsi_cmnd *));
extern void send_nop_in(struct iscsi_conn *);

struct thread;
/* conn.c */
extern struct iscsi_conn *conn_lookup(struct iscsi_session *, u16);
extern int conn_add(struct iscsi_session *, struct conn_info *);
extern int conn_del(struct iscsi_session *, struct conn_info *);
extern int conn_free(struct iscsi_conn *);
extern void conn_close(struct iscsi_conn *);
extern void conn_info_show(struct seq_file *, struct iscsi_session *);

/* nthread.c */
extern int nthread_init(struct iscsi_target *);
extern int nthread_start(struct iscsi_target *);
extern int nthread_stop(struct iscsi_target *);
#if 0
extern void nthread_wakeup(struct iscsi_target *);
#endif

enum daemon_state_bit {
	D_ACTIVE,
	D_DATA_READY,
	D_THR_EXIT,
};

static inline void nthread_wakeup(struct iscsi_target *target)
{
	struct network_thread_info *info = &target->nthread_info;
	chan_wakeup_condition(info->nthread_wait, set_bit(D_DATA_READY, &info->flags));
}

#ifdef LINUX
/*
 * @locking: grabs the target's nthread_lock to protect it from races with
 * iet_write_space()
 */
static inline void set_conn_wspace_wait(struct iscsi_conn *conn)
{
	struct network_thread_info *info = &conn->session->target->nthread_info;
	struct sock *sk = conn->sock->sk;

	spin_lock_bh(&info->nthread_lock);

	if (sk_stream_wspace(sk) < sk_stream_min_wspace(sk))
	{
		set_bit(CONN_WSPACE_WAIT, &conn->state);
	}

	spin_unlock_bh(&info->nthread_lock);
}
#else
static inline int
sk_write_space_available(struct iscsi_conn *conn)
{
	struct file filetmp;
	int error, avail = 0;

	filetmp.f_data = conn->sock;
	filetmp.f_cred = NULL;

	error = soo_ioctl(&filetmp, FIONSPACE, &avail, NULL, curthread);
	if (error)
		return 0;

	return (avail > 0) ? 1 : 0; 
}

static inline void set_conn_wspace_wait(struct iscsi_conn *conn)
{
	struct network_thread_info *info = &conn->session->target->nthread_info;

	spin_lock_bh(&info->nthread_lock);
	if (!sk_write_space_available(conn))
		set_bit(CONN_WSPACE_WAIT, &conn->state);
	spin_unlock_bh(&info->nthread_lock);
}
#endif

/* target.c */
#if 0
extern int target_lock(struct iscsi_target *, int);
#endif
static inline int target_lock(struct iscsi_target *target, int interruptible)
{
	int err = 0;

	if (interruptible)
		err = sx_xlock_interruptible(&target->target_sem);
	else
		sx_xlock(&target->target_sem);

	return err;
}

#define target_unlock(t)	sx_xunlock(&t->target_sem)
#if 0 
inline void target_unlock(struct iscsi_target *target)
{
	up(&target->target_sem);
}
extern void target_unlock(struct iscsi_target *);
#endif
struct iscsi_target *target_lookup_by_id(u32);
extern int target_add(struct target_info *);
extern int target_del(u32 id);
extern struct seq_operations iet_seq_op;

/* config.c */
extern int iet_procfs_init(void);
extern void iet_procfs_exit(void);

/* session.c */
extern struct file_operations session_seq_fops;
extern struct iscsi_session *session_lookup(struct iscsi_target *, u64);
extern int session_add(struct iscsi_target *, struct session_info *);
extern int session_del(struct iscsi_target *, u64);
extern int __session_del(struct iscsi_target *target, struct iscsi_session *session);

/* tio.c */
extern int tio_init(void);
extern void tio_exit(void);
extern struct tio *tio_alloc(int);
extern void tio_get(struct tio *);
extern void tio_put(struct tio *);
extern void tio_set(struct tio *, u32, loff_t);
extern int tio_read(struct iet_volume *, struct tio *);
extern int tio_write(struct iet_volume *, struct tio *);
extern int tio_sync(struct iet_volume *, struct tio *);

/* params.c */
extern int iscsi_param_set(struct iscsi_target *, struct iscsi_param_info *, int);

/* event.c */
extern int event_send(u32, u64, u32, u32, int);
extern int event_init(void);
extern void event_exit(void);

#ifdef FREEBSD
/* mmap */
extern void iet_mmap_exit(void);
extern int iet_mmap_init(void);
int iet_poll(struct cdev *dev, int events, struct thread *td);
d_mmap_t iet_mmap;
int iet_ioctl(struct cdev *dev, unsigned long cmd, caddr_t arg, int fflag, struct thread *td);
#endif

#define get_pgcnt(size, offset)	((((size) + ((offset) & ~PAGE_CACHE_MASK)) + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT)

static inline void iscsi_cmnd_get_length(struct iscsi_pdu *pdu)
{
#if defined(__BIG_ENDIAN)
	pdu->ahssize = pdu->bhs.length.ahslength * 4;
	pdu->datasize = pdu->bhs.length.datalength;
#elif defined(__LITTLE_ENDIAN)
	pdu->ahssize = (pdu->bhs.length & 0xff) * 4;
	pdu->datasize = be32_to_cpu(pdu->bhs.length & ~0xff);
#else
#error
#endif
}

static inline void iscsi_cmnd_set_length(struct iscsi_pdu *pdu)
{
#if defined(__BIG_ENDIAN)
	pdu->bhs.length.ahslength = pdu->ahssize / 4;
	pdu->bhs.length.datalength = pdu->datasize;
#elif defined(__LITTLE_ENDIAN)
	pdu->bhs.length = cpu_to_be32(pdu->datasize) | (pdu->ahssize / 4);
#else
#error "Invalid endian arch"
#endif
}

#define cmnd_hdr(cmnd) ((struct iscsi_scsi_cmd_hdr *) (&((cmnd)->pdu.bhs)))
#define cmnd_ttt(cmnd) cpu_to_be32((cmnd)->pdu.bhs.ttt)
#define cmnd_itt(cmnd) cpu_to_be32((cmnd)->pdu.bhs.itt)
#define cmnd_opcode(cmnd) ((cmnd)->pdu.bhs.opcode & ISCSI_OPCODE_MASK)
#define cmnd_scsicode(cmnd) cmnd_hdr(cmnd)->scb[0]
#define cmnd_immediate(cmnd) ((cmnd)->pdu.bhs.opcode & ISCSI_OP_IMMEDIATE)

#define	SECTOR_SIZE_BITS	9

enum cmnd_flags {
	CMND_hashed,
	CMND_queued,
	CMND_final,
	CMND_waitio,
	CMND_close,
	CMND_lunit,
	CMND_pending,
	CMND_tmfabort,
	CMND_rxstart,
	CMND_timer_active,
	CMND_sendabort,
};

#define set_cmnd_hashed(cmnd)	set_bit(CMND_hashed, &(cmnd)->flags)
#define cmnd_hashed(cmnd)	test_bit(CMND_hashed, &(cmnd)->flags)

#define set_cmnd_queued(cmnd)	set_bit(CMND_queued, &(cmnd)->flags)
#define cmnd_queued(cmnd)	test_bit(CMND_queued, &(cmnd)->flags)

#define set_cmnd_final(cmnd)	set_bit(CMND_final, &(cmnd)->flags)
#define cmnd_final(cmnd)	test_bit(CMND_final, &(cmnd)->flags)

#define set_cmnd_waitio(cmnd)	set_bit(CMND_waitio, &(cmnd)->flags)
#define cmnd_waitio(cmnd)	test_bit(CMND_waitio, &(cmnd)->flags)

#define set_cmnd_close(cmnd)	set_bit(CMND_close, &(cmnd)->flags)
#define cmnd_close(cmnd)	test_bit(CMND_close, &(cmnd)->flags)

#define set_cmnd_lunit(cmnd)	set_bit(CMND_lunit, &(cmnd)->flags)
#define cmnd_lunit(cmnd)	test_bit(CMND_lunit, &(cmnd)->flags)

#define set_cmnd_pending(cmnd)	set_bit(CMND_pending, &(cmnd)->flags)
#define clear_cmnd_pending(cmnd)	clear_bit(CMND_pending, &(cmnd)->flags)
#define cmnd_pending(cmnd)	test_bit(CMND_pending, &(cmnd)->flags)

#define set_cmnd_tmfabort(cmnd)	set_bit(CMND_tmfabort, &(cmnd)->flags)
#define cmnd_tmfabort(cmnd)	test_bit(CMND_tmfabort, &(cmnd)->flags)

#define set_cmnd_sendabort(cmnd)	set_bit(CMND_sendabort, &(cmnd)->flags)
#define cmnd_sendabort(cmnd)	test_bit(CMND_sendabort, &(cmnd)->flags)

#define set_cmnd_rxstart(cmnd)	set_bit(CMND_rxstart, &(cmnd)->flags)
#define cmnd_rxstart(cmnd)	test_bit(CMND_rxstart, &(cmnd)->flags)
#define set_cmnd_timer_active(cmnd)  set_bit(CMND_timer_active, &(cmnd)->flags)
#define clear_cmnd_timer_active(cmnd) \
	                        clear_bit(CMND_timer_active, &(cmnd)->flags)
#define cmnd_timer_active(cmnd) test_bit(CMND_timer_active, &(cmnd)->flags)

#if 0
#define VENDOR_ID	"IET"
#define PRODUCT_ID	"VIRTUAL-DISK"
#define PRODUCT_REV	"0"
#endif

struct iscsi_cmnd *iscsi_cmnd_create_rsp_cmnd(struct iscsi_cmnd *cmnd, int final);
u32 cmnd_read_size(struct iscsi_cmnd *cmnd);
u32 cmnd_write_size(struct iscsi_cmnd *cmnd);
void iscsi_cmnd_init_write(struct iscsi_cmnd *cmnd, int dec_busy);
void iscsi_cmnds_init_write(struct list_head *send, int dec_busy);
u32 translate_lun(u16 * data);
void exit_tx(struct iscsi_conn *conn, int res);
void close_conn(struct iscsi_conn *conn);
void target_del_all(void);
#endif	/* __ISCSI_H__ */

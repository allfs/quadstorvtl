#ifndef QS_COREDEFS_H_
#define QS_COREDEFS_H_

/* We only run on x86_64 archs */

#define USHRT_MAX		65535
#define UINT_MAX		4294967295U
#define UINT64_MAX		0xffffffffffffffffUL
#define UINTPTR_MAX		UINT64_MAX

#define T_SEQUENTIAL		0x01
#define T_CHANGER		0x08

/* Signed types */
typedef char			__int8_t;
typedef short			__int16_t;
typedef int			__int32_t;
typedef long			__int64_t;

typedef __int8_t		int8_t;
typedef __int16_t		int16_t;
typedef __int32_t		int32_t;
typedef __int64_t		int64_t;

/* Unsigned types */
typedef unsigned char		__uint8_t;
typedef unsigned short		__uint16_t;
typedef unsigned int		__uint32_t;
typedef unsigned long		__uint64_t;

typedef __uint8_t		uint8_t;
typedef __uint16_t		uint16_t;
typedef __uint32_t		uint32_t;
typedef __uint64_t		uint64_t;

typedef __uint8_t		u_int8_t;
typedef __uint16_t		u_int16_t;
typedef __uint32_t		u_int32_t;
typedef __uint64_t		u_int64_t;

typedef __uint8_t		u_char;
typedef __uint16_t		u_short;
typedef __uint32_t		u_int;
typedef __uint64_t		u_long;

typedef uint64_t size_t;
typedef int64_t ssize_t;
typedef int64_t off_t;
typedef int pid_t;

#include "sysdefs/atomic.h"
#include "sysdefs/endian.h"
#include "asmdefs.h"

/*
 * General byte order swapping functions.
 */
#define bswap16(x)		__bswap16(x)
#define bswap32(x)		__bswap32(x)
#define bswap64(x)		__bswap64(x)

#define htobe16(x)		bswap16((x))
#define htobe32(x)		bswap32((x))
#define htobe64(x)		bswap64((x))
#define htole16(x)		((uint16_t)(x))
#define htole32(x)		((uint32_t)(x))
#define htole64(x)		((uint64_t)(x))

#define be16toh(x)		bswap16((x))
#define be32toh(x)		bswap32((x))
#define be64toh(x)		bswap64((x))
#define le16toh(x)		((uint16_t)(x))
#define le32toh(x)		((uint32_t)(x))
#define le64toh(x)		((uint64_t)(x))


typedef int allocflags_t;
typedef void pagestruct_t;
typedef void uma_t;
typedef void sx_t;
typedef void mtx_t;
typedef void sock_t;
typedef void iodev_t;
typedef void g_geom_t;
typedef void g_consumer_t;
typedef void kproc_t;
typedef void cv_t;
typedef void bio_t;

#define NULL    	(0L)
#define offsetof(s, m)	((size_t)(&(((s *)0)->m)))
#define PAGE_SIZE	LBA_SIZE

#define Q_WAITOK	1
#define Q_NOWAIT	2
#define Q_NOWAIT_INTR	4
#define Q_ZERO		8
#define M_WAITOK	Q_WAITOK

#define VM_ALLOC_ZERO	Q_ZERO

#define M_QUADSTOR		0
#define M_DEVBUF		0
#define M_TSEGMENT		0
#define M_BLKENTRY		0
#define M_QCACHE		0
#define M_DRIVE			0
#define M_TAPE			0
#define M_TAPE_PARTITION	0
#define M_TMAPS			0
#define M_MCHANGERELEMENT	0
#define M_MCHANGER		0
#define M_SUPERBLK		0
#define M_TCACHE		0
#define M_CBS			0
#define M_PGLIST		0
#define M_SENSEINFO		0
#define M_CTIODATA		0
#define M_BINDEX		0
#define M_BINT			0
#define M_BDEVGROUP		0
#define M_RESERVATION		0
#define M_DEVQ			0
#define M_GDEVQ			0
#define M_WRKMEM		0

/* SCSI defs */
#define SSD_MIN_SIZE			18
/*
 * Status Byte
 */
#define SCSI_STATUS_OK			0x00
#define SCSI_STATUS_CHECK_COND		0x02
#define SCSI_STATUS_COND_MET		0x04
#define SCSI_STATUS_BUSY		0x08
#define SCSI_STATUS_INTERMED		0x10
#define SCSI_STATUS_INTERMED_COND_MET	0x14
#define SCSI_STATUS_RESERV_CONFLICT	0x18
#define SCSI_STATUS_CMD_TERMINATED	0x22    /* Obsolete in SAM-2 */
#define SCSI_STATUS_QUEUE_FULL		0x28
#define SCSI_STATUS_ACA_ACTIVE		0x30
#define SCSI_STATUS_TASK_ABORTED	0x40

#define MSG_SIMPLE_TASK			0x20 /* O/O */ /* SPI3 Terminology */
#define MSG_HEAD_OF_QUEUE_TASK		0x21 /* O/O */ /* SPI3 Terminology */
#define MSG_ORDERED_TASK		0x22 /* O/O */ /* SPI3 Terminology */
#define MSG_ACA_TASK			0x24 /* 0/0 */ /* SPI3 */

/*
 * Opcodes
 */

#define	TEST_UNIT_READY		0x00
#define	REQUEST_SENSE		0x03
#define	READ_6			0x08
#define	WRITE_6			0x0A
#define	INQUIRY			0x12
#define	MODE_SELECT_6		0x15
#define	MODE_SENSE_6		0x1A
#define	START_STOP_UNIT		0x1B
#define	START_STOP		0x1B
#define	RESERVE      		0x16
#define	RELEASE      		0x17
#define	RECEIVE_DIAGNOSTIC	0x1C
#define	SEND_DIAGNOSTIC		0x1D
#define	PREVENT_ALLOW		0x1E
#define	READ_CAPACITY		0x25
#define	READ_10			0x28
#define	WRITE_10		0x2A
#define	POSITION_TO_ELEMENT	0x2B
#define	SYNCHRONIZE_CACHE	0x35
#define	READ_DEFECT_DATA_10	0x37
#define	WRITE_BUFFER            0x3B
#define	READ_BUFFER             0x3C
#define	CHANGE_DEFINITION	0x40
#define	LOG_SELECT		0x4C
#define	LOG_SENSE		0x4D
#define	MODE_SELECT_10		0x55
#define	MODE_SENSE_10		0x5A
#define	ATA_PASS_16		0x85
#define	READ_16			0x88
#define	WRITE_16		0x8A
#define	SERVICE_ACTION_IN	0x9E
#define	REPORT_LUNS		0xA0
#define	ATA_PASS_12		0xA1
#define	MAINTENANCE_IN		0xA3
#define	MAINTENANCE_OUT		0xA4
#define	MOVE_MEDIUM     	0xA5
#define	READ_12			0xA8
#define	WRITE_12		0xAA
#define	READ_ELEMENT_STATUS	0xB8
#define VERIFY			0x2f

#define	T_DIRECT		0x00

#define SSD_CURRENT_ERROR	0x70
#define SSD_DEFERRED_ERROR	0x71
#define	SSD_KEY_NO_SENSE	0x00
#define	SSD_KEY_RECOVERED_ERROR	0x01
#define	SSD_KEY_NOT_READY	0x02
#define	SSD_KEY_MEDIUM_ERROR	0x03
#define	SSD_KEY_HARDWARE_ERROR	0x04
#define	SSD_KEY_ILLEGAL_REQUEST	0x05
#define	SSD_KEY_UNIT_ATTENTION	0x06
#define	SSD_KEY_DATA_PROTECT	0x07
#define	SSD_KEY_BLANK_CHECK	0x08
#define	SSD_KEY_Vendor_Specific	0x09
#define	SSD_KEY_COPY_ABORTED	0x0a
#define	SSD_KEY_ABORTED_COMMAND	0x0b		
#define	SSD_KEY_EQUAL		0x0c
#define	SSD_KEY_VOLUME_OVERFLOW	0x0d
#define	SSD_KEY_MISCOMPARE	0x0e
#define SSD_KEY_RESERVED	0x0f			

void *zalloc(size_t size, int type, int flags);
void free(void *ptr, int mtype);

#include <exportdefs.h>
/* kcbs defs */
extern struct qs_kern_cbs kcbs;

#define ticks		((*kcbs.get_ticks)())
#define ticks_to_msecs	(*kcbs.ticks_to_msecs)
#define msecs_to_ticks	(*kcbs.msecs_to_ticks)
#define sprintf		(*kcbs.sprintf)	
#define snprintf	(*kcbs.snprintf)	
#ifdef DEBUG
#define debug_info	(*kcbs.debug_info)
#else
#define debug_info(fmt,args...)	do {} while (0)
#endif
#define debug_print(fmt,args...) (*kcbs.debug_print)("%s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#define debug_warn(fmt,args...) (*kcbs.debug_warn)("WARN: %s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#define vm_pg_alloc	(*kcbs.vm_pg_alloc)
#define vm_pg_free	(*kcbs.vm_pg_free)
#define vm_pg_address	(*kcbs.vm_pg_address)
#define vm_pg_ref	(*kcbs.vm_pg_ref)
#define vm_pg_unref	(*kcbs.vm_pg_unref)
#define vm_pg_map	(*kcbs.vm_pg_map)
#define vm_pg_unmap	(*kcbs.vm_pg_unmap)
#define malloc		(*kcbs.malloc)
#define __uma_zalloc	(*kcbs.uma_zalloc)
#define uma_zfree	(*kcbs.uma_zfree)
#define uma_zcreate	(*kcbs.uma_zcreate)
#define __uma_zdestroy	(*kcbs.uma_zdestroy)
#define get_availmem 	(*kcbs.get_availmem)
#define mtx_alloc 	(*kcbs.mtx_alloc)
#define mtx_free 	(*kcbs.mtx_free)
#define mtx_lock 	(*kcbs.mtx_lock)
#define mtx_lock_intr 	(*kcbs.mtx_lock_intr)
#define mtx_unlock 	(*kcbs.mtx_unlock)
#define mtx_unlock_intr	(*kcbs.mtx_unlock_intr)
#define sx_alloc 	(*kcbs.shx_alloc)
#define sx_free 	(*kcbs.shx_free)
#define sx_xlock 	(*kcbs.shx_xlock)
#define sx_xunlock 	(*kcbs.shx_xunlock)
#define sx_slock 	(*kcbs.shx_slock)
#define sx_sunlock 	(*kcbs.shx_sunlock)
#define sx_xlocked 	(*kcbs.shx_xlocked)
#define bdev_start 	(*kcbs.bdev_start)
#define bdev_marker 	(*kcbs.bdev_marker)
#define cv_alloc	(*kcbs.cv_alloc)
#define cv_wait		(*kcbs.cv_wait)
#define cv_timedwait	(*kcbs.cv_timedwait)
#define cv_wait_sig	(*kcbs.cv_wait_sig)
#define cv_free		(*kcbs.cv_free)
#define pause		(*kcbs.pause)
#define printf		(*kcbs.printf)
#define kernel_thread_check	(*kcbs.kernel_thread_check)
#define sched_prio	(*kcbs.sched_prio)
#define get_cpu_count	(*kcbs.get_cpu_count)
#define sock_create	(*kcbs.sock_create)
#define sock_connect	(*kcbs.sock_connect)
#define sock_close	(*kcbs.sock_close)
#define sock_free	(*kcbs.sock_free)
#define sock_bind	(*kcbs.sock_bind)
#define sock_listen	(*kcbs.sock_listen)
#define sock_accept	(*kcbs.sock_accept)
#define sock_read	(*kcbs.sock_read)
#define sock_write	(*kcbs.sock_write)
#define sock_write_page	(*kcbs.sock_write_page)
#define sock_has_write_space	(*kcbs.sock_has_write_space)
#define sock_has_read_data	(*kcbs.sock_has_read_data)
#define sock_nopush	(*kcbs.sock_nopush)
#define kern_panic	(*kcbs.kern_panic)
#define processor_yield	(*kcbs.processor_yield)
#define kproc_create	(*kcbs.kproc_create)
#define copyout		(*kcbs.copyout)
#define copyin		(*kcbs.copyin)
#define bio_free_pages	(*kcbs.bio_free_pages)
#define bio_add_page	(*kcbs.bio_add_page)
#define bio_free_page	(*kcbs.bio_free_page)
#define bio_set_command	(*kcbs.bio_set_command)
#define bio_get_command	(*kcbs.bio_get_command)
#define bio_get_caller	(*kcbs.bio_get_caller)
#define bio_get_length	(*kcbs.bio_get_length)
#define bio_get_page	(*kcbs.bio_get_page)
#define bio_get_max_pages	(*kcbs.bio_get_max_pages)
#define bio_unmap	(*kcbs.bio_unmap)
#define send_bio	(*kcbs.send_bio)
#define g_destroy_bio	(*kcbs.g_destroy_bio)
#define bdev_unmap_support (*kcbs.bdev_unmap_support)

void memcpy(void *dst, const void *src, unsigned len);
void sys_memset(void *b, int c, int len);
void bcopy(const void *src, void *dst, size_t len);
void bzero(void *b, size_t len);
int bcmp(const void *b1, const void *b2, size_t len);

#define memcmp(sp1, sp2, szi)		bcmp(sp1, sp2, szi)

int strcmp(const char *s1, const char *s2);
size_t strlen(const char *s);
char * strcpy(char *to, const char *from);
char * strncpy(char *dst, const char *src, size_t n);
struct bdevint;
bio_t *bio_get_new(struct bdevint *bint, void *end_bio_func, void *consuder, uint64_t b_start, int bio_vec_count, int rw);
void thread_start(void);
void thread_end(void);
struct tcache;
int tcache_need_new_bio(struct tcache *tcache, bio_t *bio, uint64_t b_start, struct bdevint *bint, int stat);

#define __sched_prio(td,pr)	sched_prio(pr)

#define kernel_thread_create(fn,dt,tsk,fmt,args...)		\
({								\
	int __ret = 0;						\
	__ret = kproc_create(fn,dt,&tsk, fmt,##args);		\
	__ret;							\
})

#define debug_check(x)				\
do {						\
	if ((x)) {				\
		printf("Warning at %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__);		\
		(*kcbs.debug_check)();		\
	}					\
} while (0)

/* slabs */
extern uma_t *chan_cache;
extern uma_t *compl_cache;

#define BIO_SECTOR(block,shift)	((block << (shift - 9)))
/* wait defs */
typedef struct wait_chan {
	mtx_t *chan_lock;
	cv_t *chan_cond;
} wait_chan_t;

typedef struct wait_compl {
	mtx_t *chan_lock;
	cv_t *chan_cond;
	int done;
} wait_compl_t;


static inline void
wait_chan_init(wait_chan_t *chan, const char *name)
{
	chan->chan_lock = mtx_alloc(name);
	chan->chan_cond = cv_alloc(name);
}

static inline wait_chan_t *
wait_chan_alloc(char *name)
{
	wait_chan_t *chan;

	chan = __uma_zalloc(chan_cache, Q_WAITOK, sizeof(*chan));
	wait_chan_init(chan, name);
	return chan;
}

static inline void
wait_chan_free(wait_chan_t *chan)
{
	mtx_free(chan->chan_lock);
	cv_free(chan->chan_cond);
	uma_zfree(chan_cache, chan);
}

static inline void
wait_compl_init(wait_compl_t *chan, const char *name)
{
	chan->chan_lock = mtx_alloc(name);
	chan->chan_cond = cv_alloc(name);
	chan->done = 0;
}

static inline wait_compl_t *
wait_completion_alloc(char *name)
{
	wait_compl_t *chan;

	chan = __uma_zalloc(compl_cache, Q_WAITOK, sizeof(*chan));
	wait_compl_init(chan, name);
	return chan;
}

static inline void
init_wait_completion(wait_compl_t *chan)
{
	mtx_lock(chan->chan_lock);
	chan->done = 0;
	mtx_unlock(chan->chan_lock);
}

static inline void
wait_completion_free(wait_compl_t *chan)
{
	mtx_free(chan->chan_lock);
	cv_free(chan->chan_cond);
	uma_zfree(compl_cache, chan);
}

#define wait_on_chan_locked(chn, condition)			\
do {								\
	while (!(condition)) {					\
		cv_wait_sig(chn->chan_cond, chn->chan_lock, 0);	\
	}							\
} while (0)

#define wait_on_chan(chn, condition)				\
do {								\
	unsigned long flags;					\
	mtx_lock_intr(chn->chan_lock, &flags);			\
	while (!(condition)) {					\
		cv_wait(chn->chan_cond, chn->chan_lock, &flags, 0);\
	}							\
	mtx_unlock_intr(chn->chan_lock, &flags);		\
} while (0)

#define wait_on_chan_intr(chn, condition)				\
do {								\
	unsigned long flags;					\
	mtx_lock_intr(chn->chan_lock, &flags);			\
	while (!(condition)) {					\
		cv_wait(chn->chan_cond, chn->chan_lock, &flags, 1);\
	}							\
	mtx_unlock_intr(chn->chan_lock, &flags);		\
} while (0)

#define wait_on_chan_timeout(chn, condition, timo)		\
({								\
	unsigned long flags;					\
	long __ret = timo;						\
	mtx_lock_intr(chn->chan_lock, &flags);			\
	while (!(condition)) {					\
		__ret = cv_timedwait(chn->chan_cond, chn->chan_lock, &flags, __ret);\
		if (!__ret)					\
			break;					\
	}							\
	mtx_unlock_intr(chn->chan_lock, &flags);		\
	__ret;							\
})

#define wait_on_chan_interruptible(chn, condition)		\
do {								\
	mtx_lock(chn->chan_lock);				\
	while (!(condition)) {					\
		cv_wait_sig(chn->chan_cond, chn->chan_lock, 1);	\
	}							\
	mtx_unlock(chn->chan_lock);				\
} while (0)

#define wait_on_chan_uncond(chan)				\
do {								\
	cv_wait_sig(chan->chan_cond, chan->chan_lock, 0);	\
} while (0)

static inline void
chan_wakeup_one_unlocked(wait_chan_t *chan)
{
	(*kcbs.wakeup_one_unlocked)(chan->chan_cond);
}

static inline void
chan_wakeup_one(wait_chan_t *chan)
{
	(*kcbs.wakeup_one)(chan->chan_cond, chan->chan_lock);
}

static inline void
chan_wakeup_one_nointr(wait_chan_t *chan)
{
	(*kcbs.wakeup_one_nointr)(chan->chan_cond, chan->chan_lock);
}

static inline void
chan_wakeup_unlocked(wait_chan_t *chan)
{
	(*kcbs.wakeup_unlocked)(chan->chan_cond);
}

static inline void
chan_wakeup(wait_chan_t *chan)
{
	(*kcbs.wakeup)(chan->chan_cond, chan->chan_lock);
}

static inline void
chan_wakeup_nointr(wait_chan_t *chan)
{
	(*kcbs.wakeup_nointr)(chan->chan_cond, chan->chan_lock);
}

static inline void
wait_complete(wait_compl_t *chan)
{
	(*kcbs.wakeup_one_compl)(chan->chan_cond, chan->chan_lock, &chan->done);
}

static inline void
mark_complete(wait_compl_t *chan)
{
	chan->done = 1;
}

static inline void
wait_complete_all(wait_compl_t *chan)
{
	(*kcbs.wakeup_compl)(chan->chan_cond, chan->chan_lock, &chan->done);
}

#define chan_lock(chan)		mtx_lock((chan)->chan_lock)
#define chan_unlock(chan)	mtx_unlock((chan)->chan_lock)
#define chan_lock_intr(chan,flgs)	mtx_lock_intr((chan)->chan_lock, flgs)
#define chan_unlock_intr(chan,flgs)	mtx_unlock_intr((chan)->chan_lock, flgs)

static inline int 
kernel_thread_stop(kproc_t *task, int *flags, wait_chan_t *chan, int bit)
{
	unsigned long intr_flags;
	int retval;

	chan_lock_intr(chan, &intr_flags);
	atomic_set_bit(bit, flags);
	chan_unlock_intr(chan, &intr_flags);
	retval = (*kcbs.kernel_thread_stop)(task, flags, chan, bit);
	chan_lock_intr(chan, &intr_flags);
	atomic_clear_bit(bit, flags);
	chan_unlock_intr(chan, &intr_flags);
	return retval;
}

#define wait_for_done(cmpl)	wait_on_chan((cmpl), (cmpl)->done)
#define wait_for_done_timeout(cmpl, timo)	wait_on_chan_timeout((cmpl), (cmpl)->done, timo)

/* We work against the following kernel defs */
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))

extern void close_block_device(iodev_t *iodev);
extern iodev_t *open_block_device(char *devpath, uint64_t *size, uint32_t *sector_size, uint32_t *max_sectors, int *error);

#define sx_xlocked_check(lk)		0

#define SSD_ERRCODE		0x7F
#define SSD_ERRCODE_VALID	0x80    
#define SSD_ILI			0x20
#define SSD_EOM			0x40
#define SSD_FILEMARK		0x80
#define SSD_CURRENT_ERROR	0x70
#define SSD_DEFERRED_ERROR	0x71

#define REWIND			0x01
#define READ_BLOCK_LIMITS	0x05
#define WRITE_FILEMARKS		0x10
#define SPACE			0x11
#define ERASE			0x19
#define LOAD_UNLOAD		0x1B
#define LOCATE			0x2B
#define READ_POSITION		0x34


#endif

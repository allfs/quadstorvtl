#ifndef IETBSD_H_
#define IETBSD_H_ 1

#include <bsddefs.h>
#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/fcntl.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/ucred.h>
#include <sys/namei.h>
#include <sys/disk.h>
#include <sys/sglist.h>
#include <sys/kthread.h>
#include <sys/random.h>
#include <sys/poll.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/socket.h>
#include <sys/sockopt.h>
#include <sys/socketvar.h>
#include <sys/syscallsubr.h>
#include <sys/file.h>
#include <sys/filio.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_xpt.h>
#include <cam/cam_periph.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_sa.h>
#include <cam/scsi/scsi_da.h>
#include <cam/scsi/scsi_message.h>
#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_extern.h>
#include <vm/vm_pageout.h>
#include <vm/vm_pager.h>
#include <vm/vm_reserv.h>
#include <vm/uma.h>
#include <machine/atomic.h>
#include <machine/_inttypes.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <geom/geom.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef uint8_t __u8;
typedef int8_t __s8;
typedef uint16_t __u16;
typedef int16_t __s16;
typedef uint32_t __u32;
typedef int32_t __s32;
typedef uint64_t __u64;
typedef int64_t __s64;
typedef __u32 __be32;
typedef __u32 __le32;

typedef unsigned long pgoff_t;
typedef struct sx mutex_t;

MALLOC_DECLARE(M_IET);
MALLOC_DECLARE(M_IETCONN);
MALLOC_DECLARE(M_IETAHS);
MALLOC_DECLARE(M_IETTARG);
MALLOC_DECLARE(M_IETSESS);
MALLOC_DECLARE(M_IETTIO);

#define wake_up 	chan_wakeup_one

#if BYTE_ORDER == BIG_ENDIAN
#define __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN	BIG_ENDIAN
#elif BYTE_ORDER == LITTLE_ENDIAN
#define __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN	LITTLE_ENDIAN
#else
#error "Invalid byte order"
#endif

#define PAGE_CACHE_SHIFT	PAGE_SHIFT
#define PAGE_CACHE_MASK		~PAGE_MASK
#define PAGE_CACHE_SIZE		PAGE_SIZE

#define READ		BIO_READ
#define WRITE		BIO_WRITE

#define	__cpu_to_be16	htobe16
#define	__cpu_to_be32	htobe32
#define	__cpu_to_be64	htobe64
#define	__cpu_to_le16	htole16
#define	__cpu_to_le32	htole32
#define	__cpu_to_le64	htole64
#define __be16_to_cpu	be16toh
#define __be32_to_cpu	be32toh
#define __be64_to_cpu	be64toh
#define __le16_to_cpu	le16toh
#define __le32_to_cpu	le32toh
#define __le64_to_cpu	le64toh

#define cpu_to_be16	__cpu_to_be16
#define cpu_to_be32	__cpu_to_be32
#define cpu_to_be64	__cpu_to_be64
#define cpu_to_le16	__cpu_to_le16
#define cpu_to_le32	__cpu_to_le32
#define cpu_to_le64	__cpu_to_le64
#define be16_to_cpu	__be16_to_cpu
#define be32_to_cpu	__be32_to_cpu
#define be64_to_cpu	__be64_to_cpu
#define le16_to_cpu	__le16_to_cpu
#define le32_to_cpu	__le32_to_cpu
#define le64_to_cpu	__le64_to_cpu

#define printk		printf
#define KERN_WARNING
#define KERN_CRIT

#define SAM_STAT_GOOD		SCSI_STATUS_OK
#define SAM_STAT_CHECK_CONDITION	SCSI_STATUS_CHECK_COND
#define dump_stack()	do {} while (0)
#define BUG()		do {} while (0)
#define clear_page(page)  bzero((page), PAGE_SIZE)
#define del_timer_sync		callout_drain

/* is s2<=s1<=s3 ? */
static inline int between(__u32 seq1, __u32 seq2, __u32 seq3)
{
	return seq3 - seq2 >= seq1 - seq2;
}

static inline int before(__u32 seq1, __u32 seq2)
{
	return (__s32)(seq1-seq2) < 0;
}

static inline int after(__u32 seq1, __u32 seq2)
{
	return (__s32)(seq2-seq1) < 0;
}

#define ILLEGAL_REQUEST	SSD_KEY_ILLEGAL_REQUEST
#define ABORTED_COMMAND	SSD_KEY_ABORTED_COMMAND
#define VERIFY		0x2f
#if 0
#define jiffies		0
#endif
#define HZ		hz
#define get_fs()	do {} while (0)
#define set_fs(x)	do {} while (0)
#define ERESTARTSYS	ERESTART

#define init_waitqueue_head(h)	wait_chan_init(h, "iet")
typedef struct wait_chan wait_queue_head_t;
#define wait_event_interruptible	wait_on_chan_interruptible
#define wait_event			wait_on_chan
#define yield	uio_yield
#define timer_pending callout_pending
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

#define copy_from_user(ka,ua,ln) copyin(ua,ka,ln)
#define copy_to_user(ua,ka,ln) copyout(ka,ua,ln)
#define strnicmp	strncasecmp
#define kstrdup(s,f)	strdup(s, M_IET)

#define DECLARE_COMPLETION_ONSTACK(d)		\
	completion_t d;				\
	memset(&d, 0, sizeof(d));		\
	init_completiont(&d, "stack compl");

extern struct module *ietmod;
#define THIS_MODULE	ietmod
static inline int 
try_module_get(struct module *mod)
{
	MOD_XLOCK;
	module_reference(mod);
	MOD_XUNLOCK;
	return 1;
}

static inline void
module_put(struct module *mod)
{
	MOD_XLOCK;
	module_release(mod);
	MOD_XUNLOCK;
}

static inline void
uio_fill(struct uio *uio, struct iovec *iov, int iovcnt, ssize_t resid, int rw)
{
	uio->uio_iov = iov;
	uio->uio_iovcnt = iovcnt;
	uio->uio_offset = 0;
	uio->uio_resid = resid;
	uio->uio_rw = rw;
	uio->uio_segflg = UIO_SYSSPACE;
	uio->uio_td = curthread;
}

static inline void 
map_result(int *result, struct uio *uio, int len, int waitall)
{
	int res = *result;

	if (res) {
		if (uio->uio_resid != len) {
			res = (len - uio->uio_resid);
		}
		else
			res = -(res);
	} else {
		res = len - uio->uio_resid;
		if (!res && !waitall)
		{
			res = -(EAGAIN);
		}
	}
	*result = res;
}

#define BITS_PER_LONG	__LONG_BIT

#define PRIME 0x9e37fffffffc0001UL

static inline unsigned long hash_long(unsigned long val, int bits)
{
	return ((val * PRIME) >> (BITS_PER_LONG - bits));
}


struct list_head {
	struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define QS_LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *list)
{
	list->next = list;
	list->prev = list;
}

static inline void list_add(struct list_head *entry, struct list_head *head)
{
	struct list_head *next = head->next;

	next->prev = entry;
	entry->next = next;
	entry->prev = head;
	head->next = entry;
}

static inline void list_add_tail(struct list_head *entry, struct list_head *head)
{
	struct list_head *prev = head->prev;

	head->prev = entry;
	entry->next = head;
	entry->prev = prev;
	prev->next = entry;
}

static inline void list_del(struct list_head *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

static inline void list_del_init(struct list_head *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
	entry->next = entry;
	entry->prev = entry; /* not required */
}

static inline int list_empty(const struct list_head *head)
{
	return head->next == head;
}

#define container_of(p, stype, field) ((stype *)(((uint8_t *)(p)) - offsetof(stype, field)))

#define list_entry(ptr,type,member) \
	container_of(ptr, type, member)

#define list_for_each(pos, head) \
	for (pos = (head)->next; pos != (head); \
        	pos = pos->next)

#define list_for_each_safe(pos, n, head) \
	for (pos = (head)->next, n = pos->next; pos != (head); \
		pos = n, n = pos->next)


#define list_for_each_entry(pos, head, member) \
	for (pos = list_entry((head)->next, __typeof(*pos), member); \
		&pos->member != (head);	\
		pos = list_entry(pos->member.next, __typeof(*pos), member))

#define list_for_each_entry_safe(pos, n, head, member)                  \
	for (pos = list_entry((head)->next, __typeof(*pos), member),      \
		n = list_entry(pos->member.next, __typeof(*pos), member); \
		&pos->member != (head);                                    \
		pos = n, n = list_entry(n->member.next, __typeof(*n), member))

#endif

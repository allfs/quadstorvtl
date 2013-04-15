#ifndef QS_LINUXDEFS_H_
#define QS_LINUXDEFS_H_

#include <linux/types.h>
#include <linux/blkdev.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/list.h> 
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/list.h>
#include <linux/slab.h> 
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/net.h>
#include <scsi/scsi.h>

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,23))
#define sg_set_page(sg,pg,l,o) do { 	\
	(sg)->page = pg; 		\
	(sg)->length = (l);  		\
	(sg)->offset = (o); 		\
} while (0)
#endif


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29))
#define NIP6(addr) \
	ntohs((addr).s6_addr16[0]), \
	ntohs((addr).s6_addr16[1]), \
	ntohs((addr).s6_addr16[2]), \
	ntohs((addr).s6_addr16[3]), \
	ntohs((addr).s6_addr16[4]), \
	ntohs((addr).s6_addr16[5]), \
	ntohs((addr).s6_addr16[6]), \
	ntohs((addr).s6_addr16[7])
#define NIP6_FMT "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x"
#define NIP6_SEQFMT "%04x%04x%04x%04x%04x%04x%04x%04x"
#endif

#define htobe16(x)      __cpu_to_be16((x))
#define htobe32(x)      __cpu_to_be32((x))
#define htobe64(x)      __cpu_to_be64((x))
#define htole16(x)      __cpu_to_le16((x))
#define htole32(x)      __cpu_to_le32((x))
#define htole64(x)      __cpu_to_le64((x))

#define be16toh(x)      __be16_to_cpu((x))
#define be32toh(x)      __be32_to_cpu((x))
#define be64toh(x)      __be64_to_cpu((x))
#define le16toh(x)      __le16_to_cpu((x))
#define le32toh(x)      __le32_to_cpu((x))
#define le64toh(x)      __le64_to_cpu((x))

typedef gfp_t allocflags_t;

#ifdef DEBUG_LOCATE
#define DEBUG_INFO_LOCATE(fmt,args...)	printk(KERN_INFO "%s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#else
#define DEBUG_INFO_LOCATE(fmt,args...)
#endif
#ifdef ENABLE_DEBUG
#define DEBUG_INFO(fmt,args...)		printk(KERN_INFO fmt, ##args)
#define DEBUG_INFO_NEW(fmt,args...)	printk(KERN_INFO "%s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#else
#define DEBUG_INFO(fmt,args...)
#define DEBUG_INFO_NEW(fmt,args...)	printk(KERN_INFO "%s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#endif

/* These are always on */
#define DEBUG_CRIT(fmt,args...)	printk(KERN_CRIT "crit: "fmt, ##args)
#define DEBUG_WARN(fmt,args...)	printk(KERN_WARNING "warn: "fmt, ##args)
#define DEBUG_WARN_NEW(fmt,args...)	printk(KERN_WARNING "WARN:%s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#define DEBUG_CRIT_NEW(fmt,args...)	printk(KERN_CRIT "CRIT:%s:%d" fmt, __FUNCTION__, __LINE__, ##args)
#ifdef ENABLE_STATS
#define DEBUG_STATS(fmt,args...)	printk(KERN_INFO fmt, ##args)
#else
#define DEBUG_STATS(fmt,args...)
#endif

#ifdef WARN_ON
#define DEBUG_BUG_ON(cond)	WARN_ON(unlikely(cond))
#else
#define DEBUG_BUG_ON(cond)	BUG_ON(unlikely(cond))
#endif

enum {
	Q_WAITOK	= 0x1,
	Q_NOWAIT	= 0x2,
	Q_NOWAIT_INTR	= 0x4,
	Q_ZERO   	= 0x8,
};

#define M_WAITOK (GFP_KERNEL | __GFP_NOFAIL)
#define M_NOWAIT GFP_KERNEL
#define M_NOWAIT_INTR GFP_ATOMIC
#define M_ZERO	 __GFP_ZERO
#define VM_ALLOC_ZERO __GFP_ZERO
#define SSD_MIN_SIZE		18
#define SSD_CURRENT_ERROR	0x70

typedef wait_queue_head_t wait_chan_t;
typedef wait_queue_head_t cv_t;

static inline void
wait_chan_init(wait_chan_t *chan, const char *name)
{
	init_waitqueue_head(chan);
}

#define wait_on_chan(chan,condition)	wait_event(chan,condition)
#define wait_on_chan_interruptible(chan,condition)	wait_event_interruptible(chan,condition)
#define wait_on_chan_timeout(chan,condition,timeout)	wait_event_timeout(chan,condition,timeout)

#define chan_wakeup_one(chan)	wake_up(chan)
#define chan_wakeup(chan)	wake_up_all(chan)
#define chan_wakeup_interruptible(chan)	wake_up_interruptible(chan)

#define chan_wakeup_condition(chn, condition)			\
do {								\
	condition;						\
	wake_up_all(&(chn));					\
} while (0)

#define chan_wakeup_one_condition(chn, condition)			\
do {								\
	condition;						\
	wake_up(&(chn));					\
} while (0)



typedef struct page pagestruct_t;
typedef struct bio bio_t;

#define free(ptr,type)	kfree(ptr)
#define malloc(s,type,flags)	kmalloc(s,flags)
#define zalloc(s,type,flags)	kzalloc(s,flags)

typedef struct block_device iodev_t;
typedef struct mutex sx_t;
typedef spinlock_t mtx_t;
typedef struct sys_sock {
	struct socket *sock;
	struct sockaddr_storage saddr;
	int saddr_len;
	void *priv;
	void *state_change;
	void *data_ready;
	void *write_space;
} sock_t;

#define mtx_lock		spin_lock
#define mtx_unlock		spin_unlock
#define mtx_lock_irqsave	spin_lock_irqsave
#define mtx_unlock_irqrestore	spin_unlock_irqrestore
#define spin_lock_initt(x,y)	spin_lock_init(x)
#define mtx_lock_initt(x,y)	spin_lock_init(x)

enum {
	MT_DEF,
	MT_SPIN,
};

#define sx_init(mt,nm) mutex_init((mt))
#define sx_xlock mutex_lock
#define sx_try_xlock mutex_trylock
#define sx_slock mutex_lock
#define sx_xlocked mutex_is_locked
#define sx_xlock_interruptible mutex_lock_interruptible
#define sx_xunlock mutex_unlock
#define sx_sunlock mutex_unlock

#define SSD_KEY_ABORTED_COMMAND ABORTED_COMMAND
#define SSD_KEY_BLANK_CHECK BLANK_CHECK
#define SSD_KEY_VOLUME_OVERFLOW	VOLUME_OVERFLOW
#define SSD_KEY_HARDWARE_ERROR	HARDWARE_ERROR
#define SSD_KEY_ILLEGAL_REQUEST	ILLEGAL_REQUEST
#define SSD_KEY_NOT_READY	NOT_READY
#define SSD_KEY_MEDIUM_ERROR	MEDIUM_ERROR
#define SSD_KEY_UNIT_ATTENTION	UNIT_ATTENTION
#define SSD_KEY_NO_SENSE	NO_SENSE
#define SSD_KEY_MISCOMPARE	MISCOMPARE
#define MODE_SENSE_6	MODE_SENSE
#define MODE_SELECT_6	MODE_SELECT
#define PREVENT_ALLOW	ALLOW_MEDIUM_REMOVAL


typedef struct kmem_cache slab_cache_t;
typedef struct kmem_cache uma_t;
typedef struct completion completion_t;
#define init_completiont(x,y)	init_completion(x)

#define printf printk
#define slab_cache_create		kmem_cache_create
#define slab_cache_destroy		kmem_cache_destroy 
static inline void *
slab_cache_alloc(struct kmem_cache * cachep, allocflags_t flags, size_t len)
{
	void *ret;

	ret = kmem_cache_alloc(cachep, flags & ~(__GFP_ZERO));
	if (ret && (flags | __GFP_ZERO))
		memset(ret, 0, len);
	return ret;
}

#define slab_cache_free			kmem_cache_free
#define page_alloc(flags)	alloc_page(GFP_KERNEL | flags)
#define page_free		__free_page
typedef struct timer_list callout_t;
typedef struct task_struct kproc_t;

#define strncasecmp strnicmp

#define QS_LIST_HEAD	LIST_HEAD

#define T_SEQUENTIAL	TYPE_TAPE
#define T_DIRECT	TYPE_DISK
#define T_CHANGER	TYPE_MEDIUM_CHANGER
#define T_PROCESSOR	TYPE_PROCESSOR
#define copyin(ua,ka,ln)	copy_from_user(ka,ua,ln)
#define copyout(ka,ua,ln)	copy_to_user(ua,ka,ln)

#define callout_drain		del_timer_sync
#define callout_reset(tim,timo,fn,data)	mod_timer(tim,timo);

typedef struct scatterlist sglist_t;

typedef struct inode vnode_t;

#define kproc_suspend(t,tim)	kthread_stop(t)
#define pause(s,m)	msleep(m)
#define strdup(s,mt)	kstrdup(s, GFP_KERNEL|__GFP_NOFAIL);
#define strtoul		simple_strtoul
#define strtouq		simple_strtoull

#define kernel_thread_create(fn,dt,tsk,fmt,args...)		\
({								\
	int __ret = 0;						\
	tsk = kthread_run(fn,dt,fmt,##args);			\
	if (IS_ERR(tsk))					\
	{							\
		__ret = -1;					\
	}							\
	__ret;							\
})

enum {
	CALLOUT_MPSAFE	= 0x01,
};

#define callout_init(x,mp)	init_timer(x)

#define callout_exec(c,t,f,a)					\
do {								\
	if (timer_pending((c)))					\
		break;						\
	del_timer_sync((c));					\
	(c)->data = (unsigned long)a;				\
	(c)->function = f;					\
	(c)->expires = jiffies + msecs_to_jiffies(t);		\
	add_timer((c));						\
} while(0)

#define ticks	jiffies
#define ticks_to_msecs	jiffies_to_msecs
#define read_random	get_random_bytes

#define PHYS_TO_VM_PAGE	virt_to_page
#define vtophys(p)	(p)
typedef uint8_t *	vm_offset_t; 

#define bcopy(s,d,l)	memcpy(d,s,l)
#define bzero(p,l)	memset(p,0,l)

#define PRIu64	"llu"
#define PRIx64	"llx"

/*
 * Status Byte
 */
#define	SCSI_STATUS_OK			0x00
#define	SCSI_STATUS_CHECK_COND		0x02
#define	SCSI_STATUS_COND_MET		0x04
#define	SCSI_STATUS_BUSY		0x08
#define SCSI_STATUS_INTERMED		0x10
#define SCSI_STATUS_INTERMED_COND_MET	0x14
#define SCSI_STATUS_RESERV_CONFLICT	0x18
#define SCSI_STATUS_CMD_TERMINATED	0x22	/* Obsolete in SAM-2 */
#define SCSI_STATUS_QUEUE_FULL		0x28
#define SCSI_STATUS_ACA_ACTIVE		0x30
#define SCSI_STATUS_TASK_ABORTED	0x40

struct pgdata {
	pagestruct_t *page;
	uint16_t pg_len;
	uint16_t pg_offset;
} __attribute__ ((__packed__));

static inline unsigned long 
pgdata_page_address(struct pgdata *pgdata)
{
	return (unsigned long)page_address(pgdata->page);
}

#define PRIBIO		-20
#define PINOD		-20
#define PVFS		-20
#define PSOCK		-10
#define curthread	current

#define MSG_SIMPLE_TASK		SIMPLE_QUEUE_TAG	
#define MSG_ORDERED_TASK	ORDERED_QUEUE_TAG	
#define MSG_HEAD_OF_QUEUE_TASK	HEAD_OF_QUEUE_TAG	
#define __sched_prio(td,pr)	set_user_nice(current, pr)
#define mp_ncpus		num_online_cpus()

typedef struct list_head dlist_t;
#define page_offset(data)     ((unsigned long)data & ~PAGE_MASK)

#define module_reference(md)		try_module_get(md)
#define module_release(md)		module_put(md)

#endif

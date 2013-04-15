#ifndef QS_BSDDEFS_H_
#define QS_BSDDEFS_H_

#include <sys/types.h>
#include <sys/module.h>
#include <sys/systm.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/endian.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/bio.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/ucred.h>
#include <sys/namei.h>
#include <sys/disk.h>
#include <sys/buf.h>
#include <sys/sglist.h>
#include <sys/kthread.h>
#include <sys/random.h>
#include <sys/poll.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/stack.h>
#include <sys/stat.h>
#include <sys/linker.h>
#include <sys/unistd.h>
#include <sys/lock.h>
#include <sys/sx.h>
#include <sys/proc.h>
#include <sys/sf_buf.h>
#include <sys/smp.h>
#include <sys/md5.h>
#include <cam/cam.h>
#include <cam/cam_ccb.h>
#include <cam/cam_xpt.h>
#include <cam/cam_xpt_sim.h>
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
#include <geom/geom.h>

static __inline void
clear_bit(int b, volatile void *p)
{
	atomic_clear_int(((volatile int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static __inline void
set_bit(int b, volatile void *p)
{
	atomic_set_int(((volatile int *)p) + (b >> 5), 1 << (b & 0x1f));
}

static __inline int
test_bit(int b, volatile void *p)
{
	return ((volatile int *)p)[b >> 5] & (1 << (b & 0x1f));
}

static __inline int
test_and_clear_bit(int b, volatile void *p)
{
	return (atomic_cmpset_int(((volatile int *)p), ((*((volatile int *)p)) | (1 << b)), ((*((volatile int *)p)) & ~(1 << b))));
}

#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)

#define min_t(type, x, y) ({				\
	type X = (x);					\
	type Y = (y);					\
	X < Y ? X: Y; })

#define max_t(type, x, y) ({				\
	type X = (x);					\
	type Y = (y);					\
	X > Y ? X: Y; })

#define SCSI_SENSE_BUFFERSIZE	96

typedef int allocflags_t;
typedef struct vm_page pagestruct_t;
typedef struct bio bio_t;

#define DEBUG_BUG_ON(x)				\
do {						\
	if ((x)) {				\
		struct stack st;		\
		printf("Warning at %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__);		\
		stack_save(&st);		\
		stack_print(&st);		\
	}					\
} while (0)

#ifdef DEBUG
#define DEBUG_INFO	printf
#else
#define DEBUG_INFO(fmt,args...)	do {} while (0)
#endif

#define DEBUG_CRIT(fmt,args...)	printf("CRIT: %s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#define DEBUG_INFO_NEW(fmt,args...)	printf("INFO: %s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#define DEBUG_CRIT_NEW		DEBUG_CRIT
#define DEBUG_INFO_LOCATE	DEBUG_INFO
#define DEBUG_WARN(fmt,args...)	printf("WARN: %s:%d " fmt, __FUNCTION__, __LINE__, ##args)
#define DEBUG_WARN_NEW		DEBUG_WARN

static inline void * 
zalloc(unsigned long size, struct malloc_type *type, int flags)
{
	void *ptr;

	ptr = malloc(size, type, flags | M_ZERO);
	return ptr;
}

typedef struct {
	volatile unsigned int   val;
} atomic_t;

#define ATOMIC_INIT(i)  { (i) }

#define atomic_read(v)                  ((v)->val)
#define atomic_set(v, i)                ((v)->val = (i))

#define atomic_add(i, v)                atomic_add_int(&(v)->val, (i))
#define atomic_inc(v)                   atomic_add_int(&(v)->val, 1)
#define atomic_dec(v)                   atomic_subtract_int(&(v)->val, 1)
#define atomic_sub(i, v)                atomic_subtract_int(&(v)->val, (i))
#define atomic_dec_and_test(v)          (atomic_fetchadd_int(&(v)->val, -1) == 1)

struct wait_chan {
	struct mtx chan_lock;
	struct cv chan_cond;
	int done;
};

typedef struct wait_chan wait_chan_t;

static inline void
wait_chan_init(wait_chan_t *chan, const char *name)
{
	mtx_init(&chan->chan_lock, name, NULL, MTX_DEF);
	cv_init(&chan->chan_cond, name);
}

static inline void
init_completiont(wait_chan_t *chan, const char *name)
{
	if (!mtx_initialized(&chan->chan_lock))
		wait_chan_init(chan, name);
	mtx_lock(&chan->chan_lock);
	chan->done = 0;
	mtx_unlock(&chan->chan_lock);
}

static inline int
mtx_lock_interruptible(struct mtx *mtx)
{
	mtx_lock(mtx);
	return 0;
}

static inline int
sx_xlock_interruptible(struct sx *sx)
{
	sx_xlock(sx);
	return 0;
}

#define __wait_on_chan(chn, condition)				\
do {								\
	mtx_lock(&chn->chan_lock);				\
	while (!(condition)) {					\
		cv_wait(&chn->chan_cond, &chn->chan_lock);	\
	}							\
	mtx_unlock(&chn->chan_lock);				\
} while (0)

#define __wait_on_chan_interruptible(chn, condition)		\
({								\
	int __ret = 0;						\
	mtx_lock(&chn->chan_lock);				\
	while (!(condition)) {					\
		__ret = cv_wait_sig(&chn->chan_cond, &chn->chan_lock);	\
		if (__ret)					\
			break;					\
	}							\
	mtx_unlock(&chn->chan_lock);				\
	__ret;							\
})

#define wait_on_chan(chan, condition)				\
do {								\
	wait_chan_t *chanptr = &(chan);				\
	if (!(condition))					\
		__wait_on_chan(chanptr, condition);		\
} while (0)

#define __chan_wakeup_condition(chn, condition)			\
do {								\
	mtx_lock(&chn->chan_lock);				\
	condition;						\
	cv_broadcast(&chn->chan_cond);				\
	mtx_unlock(&chn->chan_lock);				\
} while (0)

#define __chan_wakeup_one_condition(chn, condition)		\
do {								\
	mtx_lock(&chn->chan_lock);				\
	condition;						\
	cv_signal(&chn->chan_cond);				\
	mtx_unlock(&chn->chan_lock);				\
} while (0)

#define chan_wakeup_condition(chan, condition)			\
do {								\
	wait_chan_t *chanptr = &(chan);				\
	__chan_wakeup_condition(chanptr, condition);		\
} while (0)

#define chan_wakeup_one_condition(chan, condition)		\
do {								\
	wait_chan_t *chanptr = &(chan);				\
	__chan_wakeup_one_condition(chanptr, condition);	\
} while (0)

static inline void
chan_wakeup_one(wait_chan_t *chan)
{
	mtx_lock(&chan->chan_lock);
	cv_signal(&chan->chan_cond);
	mtx_unlock(&chan->chan_lock);
}

static inline void
chan_wakeup(wait_chan_t *chan)
{
	mtx_lock(&chan->chan_lock);
	cv_broadcast(&chan->chan_cond);
	mtx_unlock(&chan->chan_lock);
}

#define chan_wakeup_interruptible(chan)	chan_wakeup(chan)

typedef struct mtx spinlock_t;
typedef struct mtx mtx_t;

static inline void
mtx_lock_initt(mtx_t *lock, char *name)
{
	mtx_init(lock, name, NULL, MTX_DEF);
}

#define spin_lock_initt mtx_lock_initt

/* Needed for Linux's equivalent of spin_lock_irqsave/irqrestore */
#define mtx_lock_irqsave(l,flg)		mtx_lock(l)
#define mtx_unlock_irqrestore(l,flg)	mtx_unlock(l)

#define spin_lock mtx_lock
#define spin_lock_irq(l) spin_lock(l)
#define spin_lock_irqsave(l,flg) spin_lock(l)
#define spin_lock_bh(l) spin_lock(l)


#define spin_unlock mtx_unlock
#define spin_unlock_irq(l) spin_unlock(l)
#define spin_unlock_irqrestore(l,flg) spin_unlock(l)
#define spin_unlock_bh(l) spin_unlock(l)

#define wait_on_chan_interruptible(chan, condition)			\
({									\
	wait_chan_t *chanptr = &(chan);					\
	int __ret;							\
	__ret = __wait_on_chan_interruptible(chanptr, condition);	\
	__ret;								\
})

static inline void
wait_for_completion(wait_chan_t *chan)
{
	__wait_on_chan(chan, chan->done);
}

static inline int 
wait_for_completion_interruptible(wait_chan_t *chan)
{
	int ret;
	ret = __wait_on_chan_interruptible(chan, chan->done);
	return ret;
}

static inline void
complete(wait_chan_t *chan)
{
	mtx_lock(&chan->chan_lock);
	chan->done = 1;
	cv_signal(&chan->chan_cond);
	mtx_unlock(&chan->chan_lock);
}

static inline void
complete_all(wait_chan_t *chan)
{
	mtx_lock(&chan->chan_lock);
	chan->done = 1;
	cv_broadcast(&chan->chan_cond);
	mtx_unlock(&chan->chan_lock);
}

#define __GFP_NOFAIL	0
#define __GFP_HIGHMEM	0

typedef struct vnode iodev_t;
typedef struct cdev scsidev_t;
typedef struct wait_chan completion_t;
typedef struct sx sx_t;
typedef struct cv cv_t;

#define __user

typedef long long loff_t;
typedef struct uma_zone slab_cache_t;
typedef struct uma_zone uma_t;

#define slab_cache_create	uma_zcreate
#define slab_cache_destroy	uma_zdestroy 
#define slab_cache_alloc(cachep, aflags, len)	uma_zalloc(cachep, aflags)
#define slab_cache_free		uma_zfree

#define page_address(pgad)	((caddr_t)(PHYS_TO_DMAP(VM_PAGE_TO_PHYS((vm_page_t)pgad))))

static inline pagestruct_t*
page_alloc(allocflags_t flags)
{
	pagestruct_t *__ret;
	__ret = vm_page_alloc(NULL, 0, VM_ALLOC_NOOBJ | VM_ALLOC_WIRED | flags);
	if (__ret && flags == VM_ALLOC_ZERO && !(__ret->flags & PG_ZERO))
		memset(page_address(__ret), 0, PAGE_SIZE);
	return __ret;
}

static inline void
page_free(pagestruct_t *pp)
{
	vm_page_lock_queues();
	DEBUG_BUG_ON(!pp->wire_count);
	if (!pp->wire_count) {
		vm_page_unlock_queues();
	}
	vm_page_unwire(pp, 0);
	if (pp->wire_count == 0 && pp->object == NULL)
		vm_page_free(pp);
	vm_page_unlock_queues();
}

#define put_page	page_free
#define DMA_TO_DEVICE	CAM_DIR_OUT
#define DMA_FROM_DEVICE CAM_DIR_IN
#define DMA_NONE	CAM_DIR_NONE

typedef struct callout callout_t;
typedef struct proc kproc_t;
#define msecs_to_jiffies(x)	(x)

typedef struct iovec sglist_t;

#define TASK_RUNNING 0
#define kthread_should_stop()	((0))
#define kthread_stop(x)						\
({								\
	int __ret = 0;						\
	__ret;							\
})

#define __set_current_state(x) do {} while (0);
#define flush_dcache_page(x) do {} while (0);

typedef struct vnode vnode_t;

#define kernel_thread_create(fn,dt,tsk,fmt,args...)		\
({								\
	int __ret = 0;						\
	__ret = kproc_create(fn,dt,&tsk,0,0,fmt,##args);	\
	__ret;							\
})

#define callout_exec(c,t,f,a)					\
do {								\
	if (callout_pending(c) || callout_active(c))		\
		break;						\
	callout_reset(c, t, f, a);				\
} while(0)

#define ticks_to_msecs(tk)	(((unsigned long)(tk * 1000))/hz)

#define __sched_prio(tdr,prio)					\
({								\
	thread_lock(tdr);					\
	sched_prio(tdr,prio);					\
	thread_unlock(tdr);					\
})

#define LOGICAL_UNIT_RESET	0x17
#define TARGET_RESET		0x0c
#define ABORT_TASK		0x0d
#define ABORT_TASK_SET		0x06
#define CLEAR_TASK_SET		0x0e

#define pgdata_page_address(pgdta)	page_address((pgdta)->page)

struct pgdata {
	pagestruct_t *page;
	uint16_t pg_len;
	uint16_t pg_offset;
} __attribute__ ((__packed__));

typedef void sock_t;

#define jiffies		ticks
#define KERN_INFO	""
#define jiffies_to_msecs(x)	((uint32_t)((x)/1000))

#define Q_WAITOK	M_WAITOK
#define Q_NOWAIT	M_NOWAIT
#define Q_ZERO		M_ZERO

#if __FreeBSD_version >= 900032
#define uio_yield()	kern_yield(PRI_UNCHANGED)
#endif

#endif

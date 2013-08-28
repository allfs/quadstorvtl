#ifndef QUADSTOR_DEVQ_H_
#define QUADSTOR_DEVQ_H_

#ifdef FREEBSD 
#include "bsddefs.h"
#else
#include "linuxdefs.h"
#endif
#include "exportdefs.h"

struct qs_devq {
	struct qsio_hdr *(*next_ccb) (struct qs_devq *);
	struct ccb_list pending_queue;
	wait_chan_t devq_wait;
	kproc_t *task;
	int flags;
	mtx_t devq_lock;
};

struct qs_devq * devq_init(uint32_t base_id, const char *name);
void devq_exit(struct qs_devq *devq);

#define DEVQ_SHUTDOWN		0x02

static inline void
devq_insert_ccb(struct qs_devq *devq, struct qsio_hdr *ccb_h)
{
	unsigned long flags;

	mtx_lock_irqsave(&devq->devq_lock, flags);
	STAILQ_INSERT_TAIL(&devq->pending_queue, ccb_h, c_list);
	mtx_unlock_irqrestore(&devq->devq_lock, flags);
	chan_wakeup_one(&devq->devq_wait);
	return;
}

#ifdef FREEBSD
static inline int
kernel_thread_check(int *flags, int bit)
{
	if (test_bit(bit, flags))
		return 1;
	else
		return 0;
}
#else
#define kernel_thread_check(a,b)	(kthread_should_stop())
#endif

#ifdef FREEBSD
static inline int
kernel_thread_check1(wait_chan_t *chan, int *flags, int bit)
{
	int ret;

	mtx_lock(&chan->chan_lock);
	if (test_bit(bit, flags))
		ret = 1;
	else
		ret = 0;
	mtx_unlock(&chan->chan_lock);
	return ret;
}
#else
#define kernel_thread_check1(a,b,c)	(kthread_should_stop())
#endif

#ifdef FREEBSD
static inline int 
kernel_thread_stop(kproc_t *task, int *flags, wait_chan_t *chan, int bit)
{
	mtx_lock(&chan->chan_lock);
	set_bit(bit, flags);
	cv_broadcast(&chan->chan_cond);
	msleep(task, &chan->chan_lock, 0, "texit", 0);
	mtx_unlock(&chan->chan_lock);
	return 0;
}
#else
#define kernel_thread_stop(tk,a,b,c)	kthread_stop(tk)
#endif

#endif

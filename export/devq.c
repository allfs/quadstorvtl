#include "devq.h"
#ifdef FREEBSD 
#include "ldev_bsd.h"
MALLOC_DEFINE(M_DEVQ, "ldev devq", "QUADStor ldev devq allocs");
#else
#include "ldev_linux.h"
#endif

static inline struct qsio_hdr *
get_next_ccb(struct qs_devq *devq)
{
	struct qsio_hdr *ccb;
#ifdef LINUX
	unsigned long flags;
#endif

	mtx_lock_irqsave(&devq->devq_lock, flags);
	ccb = STAILQ_FIRST(&devq->pending_queue);
	if (ccb)
		STAILQ_REMOVE_HEAD(&devq->pending_queue, c_list);
	mtx_unlock_irqrestore(&devq->devq_lock, flags);

	return ccb;
}

/* process_queue returns only after draining the queue */
static inline void
devq_process_queue(struct qs_devq *devq)
{
	struct qsio_hdr *ccb_h;

	while ((ccb_h = get_next_ccb(devq)) != NULL)
	{
		/* process the commands.  */
		ldev_proc_cmd((struct qsio_scsiio *)ccb_h); 
	}
}

#ifdef LINUX
static int
devq_thread(void *data)
#else
static void 
devq_thread(void *data)
#endif
{
	struct qs_devq *devq;

	devq = (struct qs_devq *)data;

	__sched_prio(curthread, PVFS);
	__set_current_state(TASK_RUNNING);

	for (;;) {
		wait_on_chan_interruptible(devq->devq_wait, !STAILQ_EMPTY(&devq->pending_queue) || kernel_thread_check(&devq->flags, DEVQ_SHUTDOWN));

		devq_process_queue(devq);
		if (unlikely(kernel_thread_check(&devq->flags, DEVQ_SHUTDOWN)))
		{
			break;
		}
	}

#ifdef LINUX
	return 0;
#else
	kproc_exit(0);
#endif
}

void
devq_exit(struct qs_devq *devq)
{
	int err;

	err = kernel_thread_stop(devq->task, &devq->flags, &devq->devq_wait, DEVQ_SHUTDOWN);
	if (err) {
		DEBUG_WARN_NEW("Shutting down devq thread failed\n");
		return;
	}

	free(devq, M_DEVQ);
}

struct qs_devq *
devq_init(uint32_t base_id, const char *name)
{
	struct qs_devq *devq;
	int retval;

	devq = zalloc(sizeof(struct qs_devq), M_DEVQ, M_WAITOK);
	if (unlikely(!devq)) {
		return NULL;
	}

	devq->next_ccb = get_next_ccb;
	wait_chan_init(&devq->devq_wait, "qs devq wait");
	STAILQ_INIT(&devq->pending_queue);
	mtx_lock_initt(&devq->devq_lock, "qs devq");

	retval = kernel_thread_create(devq_thread, devq, devq->task, "%s%d", name, base_id);
	if(retval != 0) {
		free(devq, M_DEVQ);
		return NULL;
	}
	return devq;
}

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

#include "devq.h"
#include "tdevice.h"
#include "mchanger.h"
#include "tdrive.h"

static struct qsio_hdr *
get_next_ccb(struct qs_devq *devq)
{
	struct qsio_hdr *ccb;

	chan_lock(devq->devq_wait);
	ccb = STAILQ_FIRST(&devq->pending_queue);
	if (ccb)
		STAILQ_REMOVE_HEAD(&devq->pending_queue, c_list);
	chan_unlock(devq->devq_wait);
	return ccb;
}

/* process_queue returns only after draining the queue */
static void
devq_process_queue(struct qs_devq *devq)
{
	struct qsio_hdr *ccb_h;

	while ((ccb_h = get_next_ccb(devq)) != NULL) {
		(*devq->proc_cmd)(devq->tdevice, ccb_h);
		debug_check(!atomic_read(&devq->pending_cmds));
		atomic_dec(&devq->pending_cmds);
	}
}

#ifdef FREEBSD 
static void devq_thread(void *data)
#else
static int devq_thread(void *data)
#endif
{
	struct qs_devq *devq;

	devq = (struct qs_devq *)data;

	for (;;)
	{
		wait_on_chan_interruptible(devq->devq_wait, !STAILQ_EMPTY(&devq->pending_queue) || kernel_thread_check(&devq->flags, DEVQ_EXIT));

		devq_process_queue(devq);

		if (unlikely(kernel_thread_check(&devq->flags, DEVQ_EXIT)))
		{
			break;
		}

	}
#ifdef FREEBSD 
	kproc_exit(0);
#else
	return 0;
#endif
}


void
devq_exit(struct qs_devq *devq)
{
	int err;

	err = kernel_thread_stop(devq->task, &devq->flags, devq->devq_wait, DEVQ_EXIT);
	if (err)
	{
		debug_warn("Shutting down devq thread failed\n");
		return;
	}	

	wait_chan_free(devq->devq_wait);
	free(devq, M_DEVQ);
}

struct qs_devq *
devq_init(uint32_t bus_id, uint32_t target_id, struct tdevice *tdevice, const char *name, void (*proc_cmd) (void *, void *))
{
	struct qs_devq *devq;
	int retval;

	devq = zalloc(sizeof(struct qs_devq), M_DEVQ, Q_WAITOK);
	if (unlikely(!devq))
	{
		return NULL;
	}

	devq->tdevice = tdevice;
	devq->devq_wait = wait_chan_alloc("devq wait");
	devq->proc_cmd = proc_cmd;
	STAILQ_INIT(&devq->pending_queue);
	retval = kernel_thread_create(devq_thread, devq, devq->task, "%s%03u%02u", name, bus_id, target_id);
	if(retval != 0)
	{
		wait_chan_free(devq->devq_wait);
		free(devq, M_DEVQ);
		return NULL;
	}
	return devq;
}

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

#include "gdevq.h"
#include "bdevgroup.h"

struct blkentry_clist pending_comp_queue = STAILQ_HEAD_INITIALIZER(pending_comp_queue);
static SLIST_HEAD(, qs_cdevq) cdevq_list = SLIST_HEAD_INITIALIZER(cdevq_list);
wait_chan_t *devq_comp_wait;

static inline struct blk_entry *
get_next_comp_entry(void)
{
	struct blk_entry *entry;

	chan_lock(devq_comp_wait);
	entry = STAILQ_FIRST(&pending_comp_queue);
	if (entry)
		STAILQ_REMOVE_HEAD(&pending_comp_queue, c_list);
	chan_unlock(devq_comp_wait);
	return entry;
}

static void
blk_entry_compress(struct blk_entry *entry, struct qs_cdevq *devq)
{
	struct pgdata **cpglist = NULL;
	pagestruct_t **cpages, **upages;
	uint8_t *uaddr = NULL, *caddr = NULL;
	int cpglist_cnt, retval, comp_size, i, actual_cnt;

	if (entry->block_size < LBA_SIZE)
		return;

	cpages = malloc(sizeof(pagestruct_t *) * entry->pglist_cnt, M_GDEVQ, Q_WAITOK);
	upages = malloc(sizeof(pagestruct_t *) * entry->pglist_cnt, M_GDEVQ, Q_WAITOK);

	cpglist = pgdata_allocate(entry->block_size, 1, &cpglist_cnt, Q_NOWAIT, 1);
	if (unlikely(!cpglist)) {
		goto err;
	}

	map_pglist_pages(entry->pglist, entry->pglist_cnt, upages);
	uaddr = vm_pg_map(upages, entry->pglist_cnt);
	if (!uaddr)
		goto err;

	map_pglist_pages(cpglist, cpglist_cnt, cpages);
	caddr = vm_pg_map(cpages, cpglist_cnt);
	if (!caddr)
		goto err;

	retval = qs_deflate_block(uaddr, entry->block_size, caddr, &comp_size, devq->wrkmem, COMP_ALG_LZ4);
	if (retval != 0)
		goto err;

	vm_pg_unmap(caddr, cpglist_cnt);
	vm_pg_unmap(uaddr, entry->pglist_cnt);
	free(upages, M_GDEVQ);
	free(cpages, M_GDEVQ);

	actual_cnt = pgdata_get_count(comp_size, 1);
	for (i = actual_cnt; i < cpglist_cnt; i++) {
		pgdata_free(cpglist[i]);
	}

	entry->cpglist = cpglist;
	entry->comp_size = comp_size;
	entry->cpglist_cnt = actual_cnt;
	return;
err:
	if (uaddr)
		vm_pg_unmap(uaddr, entry->pglist_cnt);
	if (caddr)
		vm_pg_unmap(caddr, cpglist_cnt);
	if (cpglist)
		pglist_free(cpglist, cpglist_cnt);
	free(upages, M_GDEVQ);
	free(cpages, M_GDEVQ);
}

static void
devq_process_comp_queue(struct qs_cdevq *devq)
{
	struct blk_entry *entry;

	while ((entry = get_next_comp_entry()) != NULL) {
		blk_entry_compress(entry, devq);
		wait_complete_all(entry->completion);
	}
}

#ifdef FREEBSD 
static void cdevq_thread(void *data)
#else
static int cdevq_thread(void *data)
#endif
{
	struct qs_cdevq *devq;

	devq = (struct qs_cdevq *)data;
	__sched_prio(curthread, QS_PRIO_INOD);

	for (;;)
	{
		wait_on_chan_interruptible(devq_comp_wait, !STAILQ_EMPTY(&pending_comp_queue) || kernel_thread_check(&devq->exit_flags, GDEVQ_EXIT));
		devq_process_comp_queue(devq);

		if (unlikely(kernel_thread_check(&devq->exit_flags, GDEVQ_EXIT)))
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

static struct qs_cdevq *
init_cdevq(int id)
{
	struct qs_cdevq *devq;
	int retval;

	devq = zalloc(sizeof(struct qs_cdevq), M_GDEVQ, Q_NOWAIT);
	if (unlikely(!devq)) {
		debug_warn("Failed to allocate a new devq\n");
		return NULL;
	}
	devq->id = id;

	retval = kernel_thread_create(cdevq_thread, devq, devq->task, "cdevq_%d", id);
	if (unlikely(retval != 0)) {
		debug_warn("Failed to run devq\n");
		free(devq, M_GDEVQ);
		return NULL;
	}
	return devq;
}

int
init_gdevq_threads(void)
{
	int i;
	struct qs_cdevq *cdevq;
	int cpu_count = get_cpu_count();

	devq_comp_wait = wait_chan_alloc("gdevq comp wait");

	for (i = 0; i < cpu_count; i++) {
		cdevq = init_cdevq(i);
		if (unlikely(!cdevq)) {
			debug_warn("Failed to init cdevq at i %d\n", i);
			return -1;
		}
		SLIST_INSERT_HEAD(&cdevq_list, cdevq, c_list);
	}

	return 0;
}

void
exit_gdevq_threads(void)
{
	struct qs_cdevq *cdevq;
	int err;

	while ((cdevq = SLIST_FIRST(&cdevq_list)) != NULL) {
		SLIST_REMOVE_HEAD(&cdevq_list, c_list);
		err = kernel_thread_stop(cdevq->task, &cdevq->exit_flags, devq_comp_wait, GDEVQ_EXIT);
		if (err) {
			debug_warn("Shutting down qs cdevq failed\n");
			continue;
		}
		free(cdevq, M_GDEVQ);
	}

	wait_chan_free(devq_comp_wait);
}

/*
 * Copyright (C) 2002-2003 Ardis Technolgies <roman@ardistech.com>
 *
 * Released under the terms of the GNU GPL v2.0.
 */

#include "iscsi.h"
#include "digest.h"
#include "iscsi_dbg.h"
#include "seq_list.h"

#define	MAX_NR_TARGETS	(1UL << 30)

static QS_LIST_HEAD(target_list);
mutex_t target_list_sem;
static u32 next_target_id;
static u32 nr_targets;

static struct iscsi_sess_param default_session_param = {
	.initial_r2t = 0,
	.immediate_data = 1,
	.max_connections = 1,
	.max_recv_data_length = 262144,
	.max_xmit_data_length = 262144,
	.max_burst_length = 262144,
	.first_burst_length = 262144,
	.default_wait_time = 2,
	.default_retain_time = 20,
	.max_outstanding_r2t = 1,
	.data_pdu_inorder = 1,
	.data_sequence_inorder = 1,
	.error_recovery_level = 0,
	.header_digest = DIGEST_ALL,
	.data_digest = DIGEST_ALL,
	.ofmarker = 0,
	.ifmarker = 0,
	.ofmarkint = 2048,
	.ifmarkint = 2048,
};

static struct iscsi_trgt_param default_target_param = {
	.wthreads = DEFAULT_NR_WTHREADS,
	.target_type = 0,
	.queued_cmnds = DEFAULT_NR_QUEUED_CMNDS,
};

static struct iscsi_target *__target_lookup_by_id(u32 id)
{
	struct iscsi_target *target;

	list_for_each_entry(target, &target_list, t_list) {
		if (target->tid == id)
			return target;
	}
	return NULL;
}

static struct iscsi_target *__target_lookup_by_name(char *name)
{
	struct iscsi_target *target;
	size_t len = strlen(name);

	list_for_each_entry(target, &target_list, t_list) {
		size_t len1 = strlen(target->name);
		if (len != len1)
			continue;
		if (!strnicmp(target->name, name, len))
			return target;
	}
	return NULL;
}

struct iscsi_target *target_lookup_by_id(u32 id)
{
	struct iscsi_target *target;

	sx_xlock(&target_list_sem);
	target = __target_lookup_by_id(id);
	sx_xunlock(&target_list_sem);

	return target;
}

static int target_thread_start(struct iscsi_target *target)
{
	int err;

	if ((err = nthread_start(target)) < 0)
		return err;

	return err;
}

static void target_thread_stop(struct iscsi_target *target)
{
	nthread_stop(target);
}

static int iscsi_target_create(struct target_info *info, u32 tid)
{
	int err = -EINVAL, len;
	char *name = info->name;
	struct iscsi_target *target;

	dprintk(D_SETUP, "%u %s\n", tid, name);

	if (!(len = strlen(name))) {
		eprintk("The length of the target name is zero %u\n", tid);
		return err;
	}

	if (!try_module_get(THIS_MODULE)) {
		eprintk("Fail to get module %u\n", tid);
		return err;
	}

	target = zalloc(sizeof(*target), M_IETTARG, M_NOWAIT);
	if (!target) {
		err = -ENOMEM;
		goto out;
	}

	target->tid = info->tid = tid;

	memcpy(&target->sess_param, &default_session_param, sizeof(default_session_param));
	memcpy(&target->trgt_param, &default_target_param, sizeof(default_target_param));

	strncpy(target->name, name, sizeof(target->name) - 1);

	sx_init(&target->target_sem, "iet");
	spin_lock_initt(&target->session_list_lock, "target session list");
	spin_lock_initt(&target->ctio_lock, "target ctio");

	INIT_LIST_HEAD(&target->session_list);

	nthread_init(target);

	if ((err = target_thread_start(target)) < 0) {
		target_thread_stop(target);
		goto out;
	}

	list_add(&target->t_list, &target_list);

	return 0;
out:
	free(target, M_IETTARG);
	module_put(THIS_MODULE);

	return err;
}

int target_add(struct target_info *info)
{
	int err = -EEXIST;
	u32 tid = info->tid;
	struct iscsi_target *target;

	sx_xlock(&target_list_sem);

	if (nr_targets > MAX_NR_TARGETS) {
		err = -EBUSY;
		goto out;
	}

	if (tid && (target = __target_lookup_by_id(tid)))
	{
		info->tid = target->tid;
		err = 0;
		goto out;
	}

	if (tid && (target = __target_lookup_by_name(info->name)))
	{
		info->tid = target->tid;
		err = 0;
		goto out;
	}

	if (!tid) {
		do {
			if (!++next_target_id)
				++next_target_id;
		} while (__target_lookup_by_id(next_target_id));

		tid = next_target_id;
	}

	if (!(err = iscsi_target_create(info, tid)))
		nr_targets++;
out:
	sx_xunlock(&target_list_sem);

	return err;
}

static void target_destroy(struct iscsi_target *target)
{
	dprintk(D_SETUP, "%u\n", target->tid);

	target_thread_stop(target);

	free(target, M_IETTARG);

	module_put(THIS_MODULE);
}

static struct iscsi_session *
session_get(struct iscsi_target *target, struct list_head *lhead)
{
	struct iscsi_session *ret = NULL;
	spin_lock(&target->session_list_lock);
	if (!list_empty(lhead))
		ret = list_entry(lhead->next, struct iscsi_session, list); 
	spin_unlock(&target->session_list_lock);
	return ret;
}

static void
__target_del(struct iscsi_target *target)
{
	struct iscsi_session *session;

	target_lock(target, 0);
	atomic_set(&target->del, 1);
	while ((session = session_get(target, &target->session_list))) {
		__session_del(target, session);
	}

	list_del(&target->t_list);
	nr_targets--;

	target_unlock(target);
	target_destroy(target);
}

int target_del(u32 id)
{
	struct iscsi_target *target;
	int err = 0;

	sx_xlock(&target_list_sem);

	if (!(target = __target_lookup_by_id(id))) {
		err = -ENOENT;
		goto out;
	}

	__target_del(target);
out:
	sx_xunlock(&target_list_sem);
	return err;
}

void target_del_all(void)
{
	struct iscsi_target *target, *tmp;

	sx_xlock(&target_list_sem);

	if (!list_empty(&target_list))
		DEBUG_INFO_NEW("Removing all connections, sessions and targets\n");

	list_for_each_entry_safe(target, tmp, &target_list, t_list) {
		__target_del(target);
	}

	next_target_id = 0;

	sx_xunlock(&target_list_sem);
}

#ifdef LINUX
static void *iet_seq_start(struct seq_file *m, loff_t *pos)
{
	int err;

	/* are you sure this is to be interruptible? */
	err = sx_xlock_interruptible(&target_list_sem);
	if (err < 0)
		return ERR_PTR(err);

	return seq_list_start(&target_list, *pos);
}

static void *iet_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	return seq_list_next(v, &target_list, pos);
}

static void iet_seq_stop(struct seq_file *m, void *v)
{
	sx_xunlock(&target_list_sem);
}

static int iet_seq_show(struct seq_file *m, void *p)
{
	iet_show_info_t *func = (iet_show_info_t *)m->private;
	struct iscsi_target *target =
		list_entry(p, struct iscsi_target, t_list);
	int err;

	/* relly, interruptible?  I'd think target_lock(target, 0)
	 * would be more appropriate. --lge */
	err = target_lock(target, 1);
	if (err < 0)
		return err;

	seq_printf(m, "tid:%u name:%s\n", target->tid, target->name);

	func(m, target);

	target_unlock(target);

	return 0;
}

struct seq_operations iet_seq_op = {
	.start = iet_seq_start,
	.next = iet_seq_next,
	.stop = iet_seq_stop,
	.show = iet_seq_show,
};
#endif

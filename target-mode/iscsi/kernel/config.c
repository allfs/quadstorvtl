/*
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 *
 * This code is licenced under the GPL.
 */

#include "iscsi.h"
#include "iscsi_dbg.h"

mutex_t ioctl_sem;

#ifdef LINUX
struct proc_entries {
	const char *name;
	struct file_operations *fops;
};

static struct proc_entries iet_proc_entries[] =
{
	{"session", &session_seq_fops},
};

static struct proc_dir_entry *proc_iet_dir;

void iet_procfs_exit(void)
{
	int i;

	if (!proc_iet_dir)
		return;

	for (i = 0; i < ARRAY_SIZE(iet_proc_entries); i++)
		remove_proc_entry(iet_proc_entries[i].name, proc_iet_dir);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
	proc_remove(proc_iet_dir);
#else
	remove_proc_entry(proc_iet_dir->name, proc_iet_dir->parent);
#endif
}

int iet_procfs_init(void)
{
	int i;
	struct proc_dir_entry *ent;

	if (!(proc_iet_dir = proc_mkdir("net/iet", NULL)))
		goto err;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24))
	proc_iet_dir->owner = THIS_MODULE;
#endif

	for (i = 0; i < ARRAY_SIZE(iet_proc_entries); i++) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0))
		ent = proc_create(iet_proc_entries[i].name, 0, proc_iet_dir,
					iet_proc_entries[i].fops);
		if (!ent)
			goto err;
#else
		ent = create_proc_entry(iet_proc_entries[i].name, 0, proc_iet_dir);
		if (ent)
			ent->proc_fops = iet_proc_entries[i].fops;
		else
			goto err;
#endif
	}

	return 0;

err:
	if (proc_iet_dir)
		iet_procfs_exit();

	return -ENOMEM;
}
#endif

#ifdef LINUX
static int
iet_copy_to_user(void *ptr, void *info, int size)
{
	int err;
	err = copy_to_user(ptr, info, size);
	return err;
}
static int
iet_copy_from_user(void *info, void *ptr, int size)
{
	int err;
	err = copy_from_user(info, ptr, size);
	return err;
}
#else
static int
iet_copy_to_user(void *ptr, void *info, int size)
{
	memcpy(ptr, info, size);
	return 0;
}
static int
iet_copy_from_user(void *info, void *ptr, int size)
{
	memcpy(info, ptr, size);
	return 0;
}
#endif

static int get_module_info(unsigned long ptr)
{
	struct module_info info;
	int err;

	snprintf(info.version, sizeof(info.version), "%s", IET_VERSION_STRING);

	err = iet_copy_to_user((void *) ptr, &info, sizeof(info));
	if (err)
		return -EFAULT;
	return 0;
}

static int get_conn_info(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct iscsi_session *session;
	struct conn_info info;
	struct iscsi_conn *conn;

	if ((err = iet_copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	session = session_lookup(target, info.sid);
	if (!session)
		return -ENOENT;
	conn = conn_lookup(session, info.cid);
	if (!conn)
		return -ENOENT;

	info.cid = conn->cid;
	info.stat_sn = conn->stat_sn;
	info.exp_stat_sn = conn->exp_stat_sn;

	if (iet_copy_to_user((void *) ptr, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int add_conn(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct iscsi_session *session;
	struct conn_info info;

	if ((err = iet_copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	session = session_lookup(target, info.sid);
	if (!session)
		return -ENOENT;

	return conn_add(session, &info);
}

static int del_conn(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct iscsi_session *session;
	struct conn_info info;

	if ((err = iet_copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	session = session_lookup(target, info.sid);
	if (!session)
		return -ENOENT;

	return conn_del(session, &info);
}

static int get_session_info(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct iscsi_session *session;
	struct session_info info;

	if ((err = iet_copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	session = session_lookup(target, info.sid);

	if (!session)
		return -ENOENT;

	info.exp_cmd_sn = session->exp_cmd_sn;
	info.max_cmd_sn = session->max_cmd_sn;

	if (iet_copy_to_user((void *) ptr, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static int add_session(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct session_info info;

	if ((err = iet_copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	return session_add(target, &info);
}

static int del_session(struct iscsi_target *target, unsigned long ptr)
{
	int err;
	struct session_info info;

	if ((err = iet_copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	return session_del(target, info.sid);
}

static int iscsi_param_config(struct iscsi_target *target, unsigned long ptr, int set)
{
	int err;
	struct iscsi_param_info info;

	if ((err = iet_copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	err = iscsi_param_set(target, &info, set);
	if (err < 0 || set)
		return err;

	if (!set)
		err = iet_copy_to_user((void *) ptr, &info, sizeof(info));

	return err;
}

static int add_target(unsigned long ptr)
{
	int err;
	struct target_info info;

	if ((err = iet_copy_from_user(&info, (void *) ptr, sizeof(info))) < 0)
		return err;

	if (!(err = target_add(&info)))
		err = iet_copy_to_user((void *) ptr, &info, sizeof(info));

	return err;
}

#ifdef LINUX
static long ioctl(struct file *file, unsigned int cmd, unsigned long arg)
#else
int iet_ioctl(struct cdev *dev, unsigned long cmd, caddr_t arg, int fflag, struct thread *td)
#endif
{
	struct iscsi_target *target = NULL;
	long err;
	u32 id;

	err = sx_xlock_interruptible(&ioctl_sem);
	if (err < 0)
		return err;

	if (cmd == GET_MODULE_INFO) {
		err = get_module_info((unsigned long)arg);
		goto done;
	}

	if ((err = iet_copy_from_user(&id, (u32 *) (unsigned long)arg, sizeof(u32))) != 0)
		goto done;

	if (cmd == DEL_TARGET) {
		err = 0;
#if 0
		err = target_del(id);
#endif
		goto done;
	}

	target = target_lookup_by_id(id);

#if 0
	if (cmd == ADD_TARGET)
		if (target) {
			err = -EEXIST;
			eprintk("Target %u already exist!\n", id);
			goto done;
		}
#endif

	switch (cmd) {
	case ADD_TARGET:
		err = add_target((unsigned long)arg);
		goto done;
	}

	if (!target) {
		eprintk("can't find the target %u\n", id);
		err = -EINVAL;
		goto done;
	}

	if ((err = target_lock(target, 1)) < 0) {
		eprintk("interrupted %ld %d\n", err, (int)cmd);
		goto done;
	}

	switch (cmd) {
	case ADD_SESSION:
		err = add_session(target, (unsigned long)arg);
		break;

	case DEL_SESSION:
		err = del_session(target, (unsigned long)arg);
		break;

	case GET_SESSION_INFO:
		err = get_session_info(target, (unsigned long)arg);
		break;

	case ISCSI_PARAM_SET:
		err = iscsi_param_config(target, (unsigned long)arg, 1);
		break;

	case ISCSI_PARAM_GET:
		err = iscsi_param_config(target, (unsigned long)arg, 0);
		break;

	case ADD_CONN:
		err = add_conn(target, (unsigned long)arg);
		break;

	case DEL_CONN:
		err = del_conn(target, (unsigned long)arg);
		break;

	case GET_CONN_INFO:
		err = get_conn_info(target, (unsigned long)arg);
		break;
	default:
		eprintk("invalid ioctl cmd %x\n", (unsigned int)cmd);
		err = -EINVAL;
	}

	if (target)
		target_unlock(target);

done:
	sx_xunlock(&ioctl_sem);
#ifdef FREEBSD
	if (err < 0)
		err = -(err);
#endif
	return err;
}

#ifdef LINUX
extern struct file_operations ctr_fops;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11))
struct file_operations ctr_fops = {
	.owner		= THIS_MODULE,
	.ioctl		= ioctl,
};
#else
struct file_operations ctr_fops = {
	.owner		= THIS_MODULE,
	.unlocked_ioctl	= ioctl,
	.compat_ioctl	= ioctl,
};
#endif
#endif

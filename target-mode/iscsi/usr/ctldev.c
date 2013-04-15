/*
 * (C) 2004 - 2005 FUJITA Tomonori <tomof@acm.org>
 *
 * This code is licenced under the GPL.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "iscsid.h"

extern int ctrl_fd;

static int ctrdev_open(void)
{
	int ctlfd;
#ifdef LINUX
	FILE *f;
	char devname[256];
	char buf[256];
	int devn;

	if (!(f = fopen("/proc/devices", "r"))) {
		log_error("cannot open control path to the driver: %m");
		return -1;
	}

	devn = 0;
	while (!feof(f)) {
		if (!fgets(buf, sizeof (buf), f)) {
			break;
		}
		if (sscanf(buf, "%d %s", &devn, devname) != 2) {
			continue;
		}
		if (!strcmp(devname, "iscsit")) {
			break;
		}
		devn = 0;
	}

	fclose(f);
	if (!devn) {
		log_error("cannot find iscsictl in /proc/devices - "
		     "make sure the kernel module is loaded");
		return -1;
	}

	unlink(CTL_DEVICE);
	if (mknod(CTL_DEVICE, (S_IFCHR | 0600), (devn << 8))) {
		log_error("cannot create %s: %m", CTL_DEVICE);
		return -1;
	}
#endif

	ctlfd = open(CTL_DEVICE, O_RDWR);
	if (ctlfd < 0) {
		log_error("cannot open %s: %m", CTL_DEVICE);
		return -1;
	}

	return ctlfd;
}

static int iet_module_info(struct module_info *info)
{
	int err;

	err = ioctl(ctrl_fd, GET_MODULE_INFO, info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl GET_MODULE_INFO: %m");

	return (err < 0) ? -errno : 0;
}

static int iscsi_target_create(u32 *tid, char *name)
{
	struct target_info info;
	int err;

	memset(&info, 0, sizeof(info));

	snprintf(info.name, sizeof(info.name), "%s", name);
	info.tid = *tid;

	err = ioctl(ctrl_fd, ADD_TARGET, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl ADD_TARGET: %m");
	else if (!err)
		*tid = info.tid;

	return (err < 0) ? -errno : 0;
}

static int iscsi_target_destroy(u32 tid)
{
	struct target_info info;
	int err;

	memset(&info, 0, sizeof(info));
	info.tid = tid;

	err = ioctl(ctrl_fd, DEL_TARGET, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl DEL_TARGET: %m");

	return (err < 0) ? -errno : 0;
}

static int iscsi_lunit_create(u32 tid, u32 lun, char *args)
{
	struct volume_info info;
	char *p;
	int err;

	memset(&info, 0, sizeof(info));

	info.tid = tid;
	info.lun = lun;

	while (isspace(*args))
		args++;
	for (p = args + (strlen(args) - 1); isspace(*p); p--)
		*p = '\0';

	info.args_ptr = (unsigned long)args;
	info.args_len = strlen(args);

	err = ioctl(ctrl_fd, ADD_VOLUME, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl ADD_VOLUME: %m");

	return (err < 0) ? -errno : 0;
}

static int iscsi_lunit_destroy(u32 tid, u32 lun)
{
	struct volume_info info;
	int err;

	memset(&info, 0, sizeof(info));
	info.tid = tid;
	info.lun = lun;

	err = ioctl(ctrl_fd, DEL_VOLUME, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl DEL_VOLUME: %m");

	return (err < 0) ? -errno : 0;
}

static int iscsi_conn_destroy(u32 tid, u64 sid, u32 cid)
{
	struct conn_info info;
	int err;

	info.tid = tid;
	info.sid = sid;
	info.cid = cid;

	err = ioctl(ctrl_fd, DEL_CONN, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl DEL_CONN: %m");

	return (err < 0) ? -errno : 0;
}

static int iscsi_param_get(u32 tid, u64 sid, int type, struct iscsi_param *param)
{
	struct iscsi_param_info info;
	int err, i;

	memset(&info, 0, sizeof(info));
	info.tid = tid;
	info.sid = sid;
	info.param_type = type;

	err = ioctl(ctrl_fd, ISCSI_PARAM_GET, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl ISCSI_PARAM_GET: %m");
	else if (!err) {
		if (type == key_session)
			for (i = 0; i < session_key_last; i++)
				param[i].val = info.session_param[i];
		else
			for (i = 0; i < target_key_last; i++)
				param[i].val = info.target_param[i];
	}

	return (err < 0) ? -errno : 0;
}

static int iscsi_param_set(u32 tid, u64 sid, int type, u32 partial, struct iscsi_param *param)
{
	struct iscsi_param_info info;
	int i, err;

	memset(&info, 0, sizeof(info));
	info.tid = tid;
	info.sid = sid;
	info.param_type = type;
	info.partial = partial;

	if (info.param_type == key_session)
		for (i = 0; i < session_key_last; i++)
			info.session_param[i] = param[i].val;
	else
		for (i = 0; i < target_key_last; i++)
			info.target_param[i] = param[i].val;

	err = ioctl(ctrl_fd, ISCSI_PARAM_SET, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl ISCSI_PARAM_SET: %m");

	return (err < 0) ? -errno : 0;
}

static int iscsi_session_create(u32 tid, u64 sid, u32 exp_cmd_sn, u32 max_cmd_sn, char *name)
{
	struct session_info info;
	int err;

	memset(&info, 0, sizeof(info));

	info.tid = tid;
	info.sid = sid;
	info.exp_cmd_sn = exp_cmd_sn;
	info.max_cmd_sn = max_cmd_sn;
	strncpy(info.initiator_name, name, sizeof(info.initiator_name) - 1);

	err = ioctl(ctrl_fd, ADD_SESSION, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl ADD_SESSION: %m");

	return (err < 0) ? -errno : 0;
}

static int iscsi_session_destroy(u32 tid, u64 sid)
{
	struct session_info info;
	int err;

	memset(&info, 0, sizeof(info));

	info.tid = tid;
	info.sid = sid;

	err = ioctl(ctrl_fd, DEL_SESSION, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl DEL_SESSION: %m");

	return (err < 0) ? -errno : 0;
}

static int iscsi_conn_create(u32 tid, u64 sid, u32 cid, u32 stat_sn, u32 exp_stat_sn,
			     int fd, u32 hdigest, u32 ddigest)
{
	struct conn_info info;
	int err;

	memset(&info, 0, sizeof(info));

	info.tid = tid;
	info.sid = sid;
	info.cid = cid;
	info.stat_sn = stat_sn;
	info.exp_stat_sn = exp_stat_sn;
	info.fd = fd;
	info.header_digest = hdigest;
	info.data_digest = ddigest;

	err = ioctl(ctrl_fd, ADD_CONN, &info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl ADD_CONN: %m");

	return (err < 0) ? -errno : 0;
}

static int iscsi_session_info(struct session_info *info)
{
	int err;

	err = ioctl(ctrl_fd, GET_SESSION_INFO, info);
	if (err < 0 && errno == EFAULT)
		log_error("error calling ioctl GET_SESSION_INFO: %m");

	return (err < 0) ? -errno : 0;
}

struct iscsi_kernel_interface ioctl_ki = {
	.ctldev_open = ctrdev_open,
	.module_info = iet_module_info,
	.lunit_create = iscsi_lunit_create,
	.lunit_destroy = iscsi_lunit_destroy,
	.param_get = iscsi_param_get,
	.param_set = iscsi_param_set,
	.target_create = iscsi_target_create,
	.target_destroy = iscsi_target_destroy,
	.session_create = iscsi_session_create,
	.session_destroy = iscsi_session_destroy,
	.session_info = iscsi_session_info,
	.conn_create = iscsi_conn_create,
	.conn_destroy = iscsi_conn_destroy,
};

struct iscsi_kernel_interface *ki = &ioctl_ki;

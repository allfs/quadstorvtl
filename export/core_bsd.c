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

#include <bsddefs.h>
#include <ioctldefs.h>
#include <exportdefs.h>

sx_t ioctl_lock;

static struct qs_kern_cbs kcbs;

MALLOC_DEFINE(M_COREBSD, "corebsd", "QUADStor allocations");
static int
coremod_ioctl(struct cdev *dev, unsigned long cmd, caddr_t arg, int fflag, struct thread *td)
{
	void __user *userp = (void __user *)arg;
	int retval = 0;
	struct bdev_info *bdev_info;
	struct mdaemon_info mdaemon_info;
	struct group_conf *group_conf;
	struct vdeviceinfo *deviceinfo;
	struct vcartridge *vcartridge;
	struct fc_rule_config fc_rule_config;

	sx_xlock(&ioctl_lock);
	switch(cmd) {
	case TLTARGIOCDAEMONSETINFO:
		memcpy(&mdaemon_info, arg, sizeof(mdaemon_info));
		(*kcbs.mdaemon_set_info)(&mdaemon_info);
		break;
	case TLTARGIOCADDFCRULE:
	case TLTARGIOCREMOVEFCRULE:
		memcpy(&fc_rule_config, arg, sizeof(fc_rule_config));
		if (cmd == TLTARGIOCADDFCRULE)
			retval = (*kcbs.target_add_fc_rule)(&fc_rule_config);
		else if (cmd == TLTARGIOCREMOVEFCRULE)
			retval = (*kcbs.target_remove_fc_rule)(&fc_rule_config);
		else
			retval = -1;
		break;
	case TLTARGIOCNEWBLKDEV:
	case TLTARGIOCDELBLKDEV:
	case TLTARGIOCGETBLKDEV:
	case TLTARGIOCUNMAPCONFIG:
		bdev_info = malloc(sizeof(*bdev_info), M_COREBSD, M_WAITOK);
		if (!bdev_info) {
			retval = -ENOMEM;
			break;
		}

		memcpy(bdev_info, arg, sizeof(*bdev_info));
		if (cmd == TLTARGIOCNEWBLKDEV)
			retval = (*kcbs.bdev_add_new)(bdev_info);
		else if (cmd == TLTARGIOCDELBLKDEV)
			retval = (*kcbs.bdev_remove)(bdev_info);
		else if (cmd == TLTARGIOCGETBLKDEV)
			retval = (*kcbs.bdev_get_info)(bdev_info);
		else if (cmd == TLTARGIOCUNMAPCONFIG)
			retval = (*kcbs.bdev_unmap_config)(bdev_info);
		memcpy(userp, bdev_info, sizeof(*bdev_info));
		free(bdev_info, M_COREBSD);
		break;
	case TLTARGIOCNEWDEVICE:
	case TLTARGIOCDELETEDEVICE:
	case TLTARGIOCMODDEVICE:
	case TLTARGIOCGETDEVICEINFO:
	case TLTARGIOCLOADDRIVE:
	case TLTARGIOCRESETSTATS:
		deviceinfo = malloc(sizeof(*deviceinfo), M_COREBSD, M_WAITOK);
		if (!deviceinfo) {
			retval = -ENOMEM;
			break;
		}

		memcpy(deviceinfo, arg, sizeof(*deviceinfo));
		if (cmd == TLTARGIOCNEWDEVICE)
			retval = (*kcbs.vdevice_new)(deviceinfo);
		else if (cmd == TLTARGIOCDELETEDEVICE)
			retval = (*kcbs.vdevice_delete)(deviceinfo);
		else if (cmd == TLTARGIOCMODDEVICE)
			retval = (*kcbs.vdevice_modify)(deviceinfo);
		else if (cmd == TLTARGIOCGETDEVICEINFO)
			retval = (*kcbs.vdevice_info)(deviceinfo);
		else if (cmd == TLTARGIOCLOADDRIVE)
			retval = (*kcbs.vdevice_load)(deviceinfo);
		else if (cmd == TLTARGIOCRESETSTATS)
			retval = (*kcbs.vdevice_reset_stats)(deviceinfo);

		memcpy(userp, deviceinfo, sizeof(*deviceinfo));
		free(deviceinfo, M_COREBSD);
		break;
	case TLTARGIOCNEWVCARTRIDGE:
	case TLTARGIOCLOADVCARTRIDGE:
	case TLTARGIOCDELETEVCARTRIDGE:
	case TLTARGIOCGETVCARTRIDGEINFO:
	case TLTARGIOCRELOADEXPORT:
		vcartridge = malloc(sizeof(*vcartridge), M_COREBSD, M_WAITOK);
		if (!vcartridge) {
			retval = -ENOMEM;
			break;
		}
		memcpy(vcartridge, arg, sizeof(*vcartridge));
		if (cmd == TLTARGIOCNEWVCARTRIDGE)
			retval = (*kcbs.vcartridge_new)(vcartridge);
		else if (cmd == TLTARGIOCLOADVCARTRIDGE)
			retval = (*kcbs.vcartridge_load)(vcartridge);
		else if (cmd == TLTARGIOCDELETEVCARTRIDGE)
			retval = (*kcbs.vcartridge_delete)(vcartridge);
		else if (cmd == TLTARGIOCGETVCARTRIDGEINFO)
			retval = (*kcbs.vcartridge_info)(vcartridge);
		else if (cmd == TLTARGIOCRELOADEXPORT)
			retval = (*kcbs.vcartridge_reload)(vcartridge);
		memcpy(userp, vcartridge, sizeof(*vcartridge));
		free(vcartridge, M_COREBSD);
		break;
	case TLTARGIOCCHECKDISKS:
		retval = (*kcbs.coremod_check_disks)();
		break;
	case TLTARGIOCLOADDONE:
		retval = (*kcbs.coremod_load_done)();
		break;
	case TLTARGIOCUNLOAD:
		retval = (*kcbs.coremod_exit)();
		break;
	case TLTARGIOCADDGROUP:
	case TLTARGIOCDELETEGROUP:
	case TLTARGIOCRENAMEGROUP:
		group_conf = malloc(sizeof(*group_conf), M_COREBSD, M_WAITOK);
		if (!group_conf) {
			retval = -ENOMEM;
			break;
		}
		memcpy(group_conf, arg, sizeof(*group_conf));
		if (cmd == TLTARGIOCADDGROUP)
			retval = (*kcbs.bdev_add_group)(group_conf);
		else if (cmd == TLTARGIOCDELETEGROUP)
			retval = (*kcbs.bdev_delete_group)(group_conf);
		else if (cmd == TLTARGIOCRENAMEGROUP)
			retval = (*kcbs.bdev_rename_group)(group_conf);
		free(group_conf, M_COREBSD);
		break;
	default:
		break;
	}
	sx_xunlock(&ioctl_lock);

	if (retval == -1)
		retval = (EIO);
	else if (retval < 0)
		retval = -(retval);
	return retval;
}

struct cdev *tldev;
static struct cdevsw tldev_csw = {
	.d_version = D_VERSION,
	.d_ioctl = coremod_ioctl,
};

static int coremod_init(void)
{
	int retval;

	sx_init(&ioctl_lock, "core ioctl lck");

	retval = vtkern_interface_init(&kcbs);
	if (retval != 0) {
		return -1;
	}

	tldev = make_dev(&tldev_csw, 0, UID_ROOT, GID_WHEEL, 0550, "vtiodev");
	return 0; 
}

static void
coremod_exit(void)
{
	sx_xlock(&ioctl_lock);
	vtkern_interface_exit();
	sx_xunlock(&ioctl_lock);

	destroy_dev(tldev);
}

static struct module *coremod;

int
vtdevice_register_interface(struct qs_interface_cbs *icbs)
{
	int retval;

	MOD_XLOCK;
	module_reference(coremod);
	MOD_XUNLOCK;
	retval = __device_register_interface(icbs);
	if (retval != 0) {
		MOD_XLOCK;
		module_release(coremod);
		MOD_XUNLOCK;
	}
	return retval;
}

void
vtdevice_unregister_interface(struct qs_interface_cbs *icbs)
{
	int retval;

	retval = __device_unregister_interface(icbs);
	if (retval == 0) {
		MOD_XLOCK;
		module_release(coremod);
		MOD_XUNLOCK;
	}
}

static int
event_handler(struct module *module, int event, void *arg) {
	int retval = 0;
	switch (event) {
	case MOD_LOAD:
		retval = coremod_init();
		if (retval == 0)
			coremod = module;
		else
			retval = EINVAL;
		break;
	case MOD_UNLOAD:
		coremod_exit();
		break;
	default:
		retval = EOPNOTSUPP;
		break;
	}
        return retval;
}

static moduledata_t tldev_info = {
    "vtldev",    /* module name */
     event_handler,  /* event handler */
     NULL            /* extra data */
};

DECLARE_MODULE(tldev, tldev_info, SI_SUB_DRIVERS, SI_ORDER_MIDDLE);
MODULE_VERSION(tldev, 1);

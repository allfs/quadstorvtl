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

#include <stdarg.h>
#include <apicommon.h>
#include <vdevice.h>

#ifdef FREEBSD
static int
iodev_check(void )
{
	return 0;
}
#else
static int iodev;
static int
iodev_check(void )
{
	FILE *fp;
	char devname[256];
	char buf[256];
	int major = 0, tmp;

	if (iodev)
		return 0;

	fp = fopen("/proc/devices", "r");
	if (!fp) {
		DEBUG_WARN("Cannot open /proc/devices\n");
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (sscanf(buf, "%d %s", &tmp, devname) != 2) {
			continue;
		}

		if (strcmp(devname, TL_DEV_NAME))
			continue;
		major = tmp;
		break;
	}
	fclose(fp);

	if (!major) {
		DEBUG_WARN("Cannot locate iodev in /proc/devices");
		return -1;
	}

	unlink(TL_DEV);
	if (mknod(TL_DEV, (S_IFCHR | 0600), (major << 8))) {
		DEBUG_WARN("Cannot create iodev\n");
		return -1;
	}
	iodev = 1;
	return 0;
}
#endif

int
tl_ioctl2(char *dev, unsigned long int request, void *arg)
{
	int fd;
	int retval;

	if (iodev_check() < 0)
		return -1;

	if ((fd = open(dev, O_RDONLY)) < 0) {
		DEBUG_WARN_NEW("failed to open %s errno %d %s\n", dev, errno, strerror(errno));
		return -1;
	}
	retval = ioctl(fd, request, arg);
	close(fd);
	if (retval != 0) {
		DEBUG_WARN_NEW("failed to exect cmd %lu errno %d %s\n", request, errno, strerror(errno));
	}
	return retval;
}

int
tl_ioctl(unsigned long int request, void *arg)
{
	return tl_ioctl2(TL_DEV, request, arg);
}

int
tl_ioctl_void(unsigned long int request)
{
	int fd;
	int retval;

	if (iodev_check() < 0)
		return -1;

	if ((fd = open(TL_DEV, O_RDONLY)) < 0)
		return -1;

	retval = ioctl(fd, request);
	close(fd);
	return retval;
}

struct vcartridge *
parse_vcartridge(FILE *fp)
{
	struct vcartridge *vinfo;
	char buf[100];

	vinfo = malloc(sizeof(struct vcartridge));
	if (!vinfo)
	{
		DEBUG_ERR("Cannot alloc for a new volume info struct\n");
		return NULL;
	}

	memset(vinfo, 0, sizeof(struct vcartridge));

	if (fscanf(fp, "tl_id: %d\n", &vinfo->tl_id) != 1)
	{
		DEBUG_ERR("Cannot get tl_id\n");
		goto err;
	}

	if (fscanf(fp, "tape_id: %u\n", &vinfo->tape_id) != 1)
	{
		DEBUG_ERR("Cannot get tl_id\n");
		goto err;
	}

	if (fscanf(fp, "worm: %d\n", &vinfo->worm) != 1)
	{
		DEBUG_ERR("Cannot get worm\n");
		goto err;
	}

	if (fscanf(fp, "type: %hhu\n", &vinfo->type) != 1)
	{
		DEBUG_ERR("Cannot get type\n");
		goto err;
	}

	if (fscanf(fp, "elem_type: %hhu\n", &vinfo->elem_type) != 1)
	{
		DEBUG_ERR("Cannot get elem_type\n");
		goto err;
	}

	if (fscanf(fp, "elem_address: %hu\n", &vinfo->elem_address) != 1)
	{
		DEBUG_ERR("Cannot get elem_address\n");
		goto err;
	}

	if (fscanf(fp, "group_name: %[^\n]\n", vinfo->group_name) != 1)
	{
		DEBUG_ERR("Cannot get pool name\n");
		goto err;
	}

	if (fscanf(fp, "label: %[^\n]\n", vinfo->label) != 1)
	{
		DEBUG_ERR("Cannot get label\n");
		goto err;
	}

	if (fscanf(fp, "size: %"PRIu64"\n", &vinfo->size) != 1)
	{
		DEBUG_ERR("Cannot get size\n");
		goto err;
	}

	if (fscanf(fp, "used: %"PRIu64"\n", &vinfo->used) != 1)
	{
		DEBUG_ERR("Cannot get used\n");
		goto err;
	}

	if (fscanf(fp, "vstatus: %u\n", &vinfo->vstatus) != 1)
	{
		DEBUG_ERR("Cannot get vstatus\n");
		goto err;
	}

	if (fscanf(fp, "loaderror: %d\n", &vinfo->loaderror) != 1)
	{
		DEBUG_ERR("Cannot get load status\n");
		goto err;
	}

	buf[0] = 0;
	fgets(buf, sizeof(buf), fp);
	if (strcmp(buf, "</vcartridge>\n") != 0)
	{
		DEBUG_ERR("Invalid buf %s\n", buf);
		goto err;
	}

	return vinfo;
err:
	free(vinfo);
	return  NULL;
}

#define LOG_BUFFER_SIZE		(1024 * 1024)

int
get_voltype(int drivetype)
{
	return slottype_from_drivetype(drivetype);
}

struct tdriveconf *
parse_driveconf(FILE *fp)
{
	struct tdriveconf *driveconf;
	char buf[100];

	driveconf = malloc(sizeof (struct tdriveconf));

	if (!driveconf)
	{
		DEBUG_ERR("Unable to allocate for a new drive struct\n");
		return NULL;
	}

	memset(driveconf, 0, sizeof(struct tdriveconf));
	TAILQ_INIT(&driveconf->vdevice.vol_list);

	if (fscanf(fp, "name: %[^\n]\n", driveconf->vdevice.name) != 1)
	{
		DEBUG_ERR("Unable to get drive name\n");
		goto err;
	} 

	if (fscanf(fp, "serialnumber: %[^\n]\n", driveconf->vdevice.serialnumber) != 1)
	{
		DEBUG_ERR("Unable to get drive serialnumber\n");
		goto err;
	} 

	if (fscanf(fp, "type: %d\n", &driveconf->type) != 1)
	{
		DEBUG_ERR("Unable to get drive type\n");
		goto err;
	} 

	if (fscanf(fp, "tl_id: %d\n", &driveconf->vdevice.tl_id) != 1)
	{
		DEBUG_ERR("Unable to get drive tl_id\n");
		goto err;
	} 

	if (fscanf(fp, "target_id: %u\n", &driveconf->vdevice.target_id) != 1)
	{
		DEBUG_ERR("Unable to get drive target_id\n");
		goto err;
	} 

	if (fscanf(fp, "tape_label: %[^\n]\n", driveconf->tape_label) != 1)
	{
		DEBUG_ERR("Unable to get drive tape_label\n");
		goto err;
	} 

	fgets(buf, sizeof(buf), fp);	
	if (strcmp(buf, "</drive>\n"))
	{
		DEBUG_ERR("Got invalid buffer %s\n", buf);
		goto err;
	}

	return driveconf;
err:
	free(driveconf);
	return NULL;
}

struct vtlconf *
parse_vtlconf(FILE *fp)
{
	struct vtlconf *vtlconf;
	struct tdriveconf *driveconf;
	char buf[256];

	vtlconf = malloc(sizeof (struct vtlconf));

	if (!vtlconf)
	{
		DEBUG_ERR("Unable to allocate for a new vtl struct\n");
		return NULL;
	}

	memset(vtlconf, 0, sizeof(struct vtlconf));
	TAILQ_INIT(&vtlconf->drive_list);
	TAILQ_INIT(&vtlconf->vdevice.vol_list);

	if (fscanf(fp, "name: %[^\n]\n", vtlconf->vdevice.name) != 1)
	{
		DEBUG_ERR("Unable to find vtl name\n");
		goto err;
	} 

	if (fscanf(fp, "serialnumber: %[^\n]\n", vtlconf->vdevice.serialnumber) != 1)
	{
		DEBUG_ERR("Unable to find vtl serialnumber\n");
		goto err;
	} 

	if (fscanf(fp, "type: %d\n", &vtlconf->type) != 1)
	{
		DEBUG_ERR("Unable to find vtl type\n");
		goto err;
	} 

	if (fscanf(fp, "slots: %d\n", &vtlconf->slots) != 1)
	{
		DEBUG_ERR("Unable to find vtl slots\n");
		goto err;
	} 

	if (fscanf(fp, "ieports: %d\n", &vtlconf->ieports) != 1)
	{
		DEBUG_ERR("Unable to find vtl ieports\n");
		goto err;
	} 

	if (fscanf(fp, "drives: %d\n", &vtlconf->drives) != 1)
	{
		DEBUG_ERR("Unable to find vtl drives\n");
		goto err;
	} 

	if (fscanf(fp, "tl_id: %d\n", &vtlconf->vdevice.tl_id) != 1)
	{
		DEBUG_ERR("Unable to find vtl tl_id\n");
		goto err;
	} 

	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
		if (strcmp(buf, "</vtl>\n") == 0)
		{
			break;
		}

		driveconf = parse_driveconf(fp);
		if (!driveconf) {
			DEBUG_ERR("Error in getting driveconf for VTL %s\n", vtlconf->vdevice.name);
			goto err;
		}
		TAILQ_INSERT_TAIL(&vtlconf->drive_list, driveconf, q_entry);
	}

	return vtlconf;

err:
	free(vtlconf);
	return NULL;
}

struct vdevice *
parse_vdevice(FILE *fp)
{
	struct vcartridge *vcartridge;
	struct vdevice *vdevice;
	char buf[100];
	int type;

	if (fscanf(fp, "type: %d\n", &type) != 1)
	{
		DEBUG_ERR("Unable to get type\n");
		goto err;
	}

	fgets(buf, sizeof(buf), fp);
	if (type == T_CHANGER)
	{
		struct vtlconf *vtlconf;

		vtlconf = parse_vtlconf(fp);
		if (!vtlconf)
		{
			goto err;
		}
		vdevice = (struct vdevice *)vtlconf;
	}
	else
	{
		struct tdriveconf *driveconf;

		driveconf = parse_driveconf(fp);
		if (!driveconf)
		{
			goto err;
		}
		vdevice = (struct vdevice *)driveconf;
	}

	fgets(buf, sizeof(buf), fp);
	do {
		if (strcmp(buf, "<vcartridge>\n"))
			break;
		vcartridge = parse_vcartridge(fp);
		if (!vcartridge) {
			DEBUG_ERR("Error in getting driveconf for VTL %s\n", vdevice->name);
			goto err;
		}
		TAILQ_INSERT_TAIL(&vdevice->vol_list, vcartridge, q_entry);
	} while (fgets(buf, sizeof(buf), fp) != NULL);

	if (strcmp(buf, "</vdevice>\n")) {
		DEBUG_ERR("Invalid bufffer %s instead of trailing tag\n", buf);
		goto err;
	}

	vdevice->type = type;
	return vdevice;
err:
	return NULL;
}

static void
free_vtl_drives(struct vtlconf *vtlconf)
{
	struct tdriveconf *dconf;

	while ((dconf = TAILQ_FIRST(&vtlconf->drive_list))) {
		TAILQ_REMOVE(&vtlconf->drive_list, dconf, q_entry);
		free(dconf);
	}
}

void
free_vdevice(struct vdevice *vdevice)
{
	if (vdevice->type == T_CHANGER)
		free_vtl_drives((struct vtlconf *)vdevice);
	free(vdevice);
}

int
usage_percentage(uint64_t size, uint64_t used)
{
	if (used > size)
		return 100;

	return (100 - (((double)(size - used)/size) * 100));
}

void
get_transfer_rate(double bytes, long elapsed, char *buf)
{
	double trate = bytes/elapsed;

	if (trate >= (1024 * 1024 * 1024)) {
		sprintf(buf, "%.2f GB/s", (trate / (1024.00 * 1024.00 * 1024.00)));
	}
	else if (trate >= (1024 * 1024)) {
		sprintf(buf, "%.2f MB/s", (trate / (1024.00 * 1024.00)));
	}
	else {
		sprintf(buf, "%.2f KB/s", (trate / (1024.00)));
	}
}

void
get_data_str(double bytes, char *buf)
{
	if (bytes >= (1024 * 1024 * 1024)) {
		sprintf(buf, "%.2f GB", (bytes / (1024.00 * 1024.00 * 1024.00)));
	}
	else if (bytes >= (1024 * 1024)) {
		sprintf(buf, "%.2f MB", (bytes / (1024.00 * 1024.00)));
	}
	else {
		sprintf(buf, "%.2f KB", (bytes / (1024.00)));
	}
}

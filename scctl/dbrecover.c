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

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <tlclntapi.h>
#include <tlsrvapi.h>
#include <sqlint.h> 
#ifdef FREEBSD
#include <libgeom.h>
#else
#include <linux/types.h>
#endif
#include <rawdefs.h>
#include <ietadm.h>

extern struct tl_blkdevinfo *bdev_list[];
extern struct d_list disk_list;
extern struct group_info *group_list[];
extern struct vdevice *device_list[];
int testmode = 0;

#define LBA_SIZE 4096
#define atomic_test_bit(b, p)                                           \
({                                                                      \
        int __ret;                                                      \
        __ret = ((volatile int *)p)[b >> 5] & (1 << (b & 0x1f));        \
        __ret;                                                          \
})

struct physdisk *
locate_group_master(uint32_t group_id)
{
	struct physdisk *disk;

	TAILQ_FOREACH(disk, &disk_list, q_entry) {
		if (disk->ignore)
			continue;
		if (disk->group_id != group_id)
			continue;
		if (atomic_test_bit(GROUP_FLAGS_MASTER, &disk->group_flags))
			return disk;
	}
	return NULL;
}

int
read_raw_bint(struct physdisk *disk, struct raw_bdevint *raw_bint)
{
	int retval;
	char buf[4096];

	retval = read_from_device(disk->info.devname, buf, sizeof(buf), BDEV_META_OFFSET);
	if (retval < 0) {
		fprintf(stdout, "IO error while readin %s\n", disk->info.devname);
		return -1;
	}
	memcpy(raw_bint, buf, sizeof(*raw_bint));
	return 0;
}

int prompt_user(char *msg)
{
	int resp;

	fprintf(stdout, "%s", msg);
	fflush(stdout);
again:
	resp = getchar();
#ifdef FREEBSD
	fpurge(stdin);
#endif
	if ((char)(resp) == 'y')
		return 1;
	else if ((char)(resp) == 'n')
		return 0;
	else if ((char)(resp) == '\n')
		goto again;
	else if ((char)(resp) == ' ')
		goto again;
	else {
		fprintf(stdout, "Enter y/n ");
		fflush(stdout);
		goto again;
	}
}

static int
add_vtl_drive(struct vtlconf *vtlconf, struct drive_info *drive_info, int target_id, int testmode, char *errmsg)
{
	struct tdriveconf *driveconf;
	int retval;

	driveconf = tdriveconf_new(vtlconf->vdevice.tl_id, target_id, drive_info->name, drive_info->serialnumber);
	driveconf->type = drive_info->make;

	fprintf(stdout, "Adding drive %s for VTL %s\n", drive_info->name, vtlconf->vdevice.name);
	if (!testmode) {
		retval = sql_add_vtl_drive(vtlconf->vdevice.tl_id, driveconf);
		if (retval != 0) {
			sprintf(errmsg, "sql add vtl drive failed\n");
			return -1;
		}

		retval = ietadm_default_settings(&driveconf->vdevice, &vtlconf->vdevice);
		if (retval != 0) {
			sprintf(errmsg, "Setting drive iSCSI default failed\n");
			return -1;
		}
	}
	TAILQ_INSERT_TAIL(&vtlconf->drive_list, driveconf, q_entry); 
	return 0;
}

static struct vdevice *
add_vdrive(struct vtl_info *vtl_info, int testmode, char *errmsg)
{
	struct vdevice *vdevice;
	struct tdriveconf *driveconf;
	int retval;

	retval = vtl_name_exists(vtl_info->name);
	if (retval) {
		sprintf(errmsg, "A VTL with name %s already exists", vtl_info->name);
		return NULL;
	}

	driveconf = tdriveconf_new(vtl_info->tl_id, 0, vtl_info->name, vtl_info->serialnumber);
	if (!driveconf) {
		sprintf(errmsg, "Memory allocation failure\n");
		return NULL;
	}

	vdevice = (struct vdevice *)driveconf;
	driveconf->type = vtl_info->type;
	fprintf(stdout, "Adding VDrive %s\n", vtl_info->name);
	if (!testmode) {
		retval = sql_add_drive(driveconf);
		if (retval != 0) {
			sprintf(errmsg, "sql add vdrive failed\n");
			return NULL;
		}

		retval  = ietadm_default_settings(vdevice, NULL);
		if (retval != 0) {
			sprintf(errmsg, "Setting iSCSI default failed\n");
			return NULL;
		}
	}
	device_list[vtl_info->tl_id] = vdevice;
	return vdevice;
}

static struct vdevice *
add_vtl(struct vtl_info *vtl_info, int testmode, char *errmsg)
{
	struct vtlconf *vtlconf;
	struct vdevice *vdevice;
	struct drive_info *drive_info;
	int retval, i;

	retval = vtl_name_exists(vtl_info->name);
	if (retval) {
		sprintf(errmsg, "A VTL with name %s already exists", vtl_info->name);
		return NULL;
	}

	vtlconf = vtlconf_new(vtl_info->tl_id, vtl_info->name, vtl_info->serialnumber);
	if (!vtlconf) {
		sprintf(errmsg, "Memory allocation failure\n");
		return NULL;
	}
	vtlconf->slots = vtl_info->slots;
	vtlconf->ieports = vtl_info->ieports;
	vtlconf->type = vtl_info->type;
	vdevice = (struct vdevice *)vtlconf;

	fprintf(stdout, "Adding VTL %s\n", vtl_info->name);
	if (!testmode) {
		retval = sql_add_vtl(vtlconf);
		if (retval != 0) {
			sprintf(errmsg, "sql add vtl failed\n");
			return NULL;
		}

		retval  = ietadm_default_settings(vdevice, NULL);
		if (retval != 0) {
			sprintf(errmsg, "Setting iSCSI default failed\n");
			return NULL;
		}
	}

	drive_info = &vtl_info->drive_info[0];
	for (i = 0; i < vtl_info->drives; i++, drive_info++) {
		retval = add_vtl_drive(vtlconf, drive_info, i + 1, testmode, errmsg);
		if (retval != 0)
			return NULL;
	}
	device_list[vtl_info->tl_id] = vdevice;
	return vdevice;
}

struct vdevice *
check_vtl_info(struct raw_tape *raw_tape, int testmode)
{
	struct vdevice *vdevice;
	struct vtl_info *vtl_info = &raw_tape->vtl_info;
	char errmsg[256];

	vdevice = find_vdevice(vtl_info->tl_id, 0);
	if (vdevice) {
		vdevice->offline = 0;
		return vdevice;
	}

	if (raw_tape->device_type == T_CHANGER) {
		vdevice = add_vtl(vtl_info, testmode, errmsg);
		if (!vdevice) {
			fprintf(stderr, "Adding VTL failed. Err msg is %s\n", errmsg);
			exit(1);
		}
		return vdevice;
	}
	else {
		vdevice = add_vdrive(vtl_info, testmode, errmsg);
		if (!vdevice) {
			fprintf(stderr, "Adding VDrive failed. Err msg is %s\n", errmsg);
			exit(1);
		}
		return vdevice;
	} 
	return NULL;
}

static void
mark_volumes_offline(struct vlist *vol_list)
{
	struct vcartridge *vinfo;

	TAILQ_FOREACH(vinfo, vol_list, q_entry) {
		vinfo->loaderror = 2;
	}
}

static struct vcartridge *
__find_volume(struct vlist *vol_list, int tl_id, uint32_t tape_id)
{
	struct vcartridge *vinfo;

	TAILQ_FOREACH(vinfo, vol_list, q_entry) {
		if (vinfo->tape_id == tape_id)
			return vinfo;
	}
	return NULL;
}

void
scan_vcartridges()
{
	int i, j;
	int retval, offset;
	char buf[4096];
	struct raw_tape *raw_tape;
	PGconn *conn;
	struct group_info *group_info;
	struct tl_blkdevinfo *blkdev;
	struct physdisk *disk;
	struct vdevice *vdevice;
	struct vcartridge *vcartridge, vinfo;

	for (j = 1; j < TL_MAX_DISKS; j++) {
		blkdev = bdev_list[j];
		if (!blkdev)
			continue;
		disk = &blkdev->disk;
		if (!atomic_test_bit(GROUP_FLAGS_MASTER, &disk->group_flags)) {
			printf("not group master\n");
			continue;
		}

		group_info = blkdev->group;
		retval = sql_query_volumes(blkdev);
		if (retval != 0) {
			fprintf(stdout, "query volumes failed for %s\n", disk->info.devname);
			exit(1);
		}

		mark_volumes_offline(&blkdev->vol_list);

		for (i = 0; i < MAX_VTAPES; i++) {
			offset = VTAPES_OFFSET + (i * 4096);
			retval = read_from_device(disk->info.devname, buf, sizeof(buf), offset);
			if (retval < 0) {
				fprintf(stdout, "IO error while reading %s\n", disk->info.devname);
				exit(1);
			}

			raw_tape = (struct raw_tape *)(buf);
			if (!raw_tape->tape_id)
				continue;

			vdevice = check_vtl_info(raw_tape, testmode);
			if (!vdevice)
				exit(1);

			vcartridge = __find_volume(&blkdev->vol_list, vdevice->tl_id, raw_tape->tape_id);
			if (vcartridge) {
				vcartridge->loaderror = 0;
				continue;
			}

			fprintf(stdout, "Adding VCartridge %s tape id %u\n", raw_tape->label, raw_tape->tape_id);
			if (testmode)
				continue;

			memset(&vinfo, 0, sizeof(vinfo));
			strcpy(vinfo.label, raw_tape->label);
			vinfo.tl_id = vdevice->tl_id;
			vinfo.type = raw_tape->make;
			vinfo.size = raw_tape->size;
			vinfo.group_id = group_info->group_id;
			vinfo.tape_id = raw_tape->tape_id;
			vinfo.worm = raw_tape->worm;
			
			conn = pgsql_begin();
			if (!conn) {
				fprintf(stdout, "Unable to connect to db\n");
				exit(1);
			}

			retval = sql_add_vcartridge(conn, &vinfo);
			if (retval != 0) {
				fprintf(stdout, "Failed to add vcartridge to db\n");
				exit(1);
			}

			retval = pgsql_commit(conn);
			if (retval != 0) {
				fprintf(stdout, "Failed to commit transaction\n");
				exit(1);
			}
		}
	}
}

static struct group_info *
add_group(struct raw_bdevint *raw_bint, int testmode)
{
	struct group_info *group_info;
	PGconn *conn;
	int retval;

	group_info = alloc_buffer(sizeof(*group_info));
	if (!group_info)
		return NULL;

	conn = pgsql_begin();
	if (!conn) {
		free(group_info);
		return NULL;
	}
	strcpy(group_info->name, raw_bint->group_name);
	group_info->group_id = raw_bint->group_id;
	TAILQ_INIT(&group_info->bdev_list);
	if (atomic_test_bit(GROUP_FLAGS_WORM, &raw_bint->flags))
		group_info->worm = 1;

	fprintf(stdout, "Adding pool %s pool id %u\n", group_info->name, group_info->group_id);
	if (testmode) {
		group_list[group_info->group_id] = group_info;
		return group_info;
	}

	retval = sql_add_group(conn, group_info);
	if (retval != 0) {
		pgsql_rollback(conn);
		free(group_info);
		return NULL;
	}

	retval = pgsql_commit(conn);
	if (retval != 0) {
		free(group_info);
		return NULL;
	}

	group_list[group_info->group_id] = group_info;
	return group_info;
}

static int
__srv_disk_configured(struct physdisk *disk, struct group_info *group_info)
{
	struct physdisk *cur_disk;
	struct tl_blkdevinfo *blkdev;
	int j;

	for (j = 1; j < TL_MAX_DISKS; j++) {
		blkdev = bdev_list[j];
		if (!blkdev)
			continue;
		cur_disk = &blkdev->disk;

		if (device_equal(&cur_disk->info, &disk->info) == 0) {
			memcpy(&blkdev->disk, disk, offsetof(struct physdisk, q_entry));
			blkdev->group = group_info;
			blkdev->group_id = group_info->group_id;
			blkdev->offline = 0;
			TAILQ_INSERT_TAIL(&group_info->bdev_list, blkdev, g_entry);
			return 1;
		}
	}
	return 0;
}

int
check_disk(struct physdisk *disk, int formaster)
{
	struct physdisk *master_disk;
	PGconn *conn;
	struct group_info *group_info;
	struct raw_bdevint raw_bint;
	struct tl_blkdevinfo *blkdev;
	char *master_str = formaster ? "Master " : "";
	char msg[256];
	int retval;

	if (disk->ignore)
		return 0;

	retval = read_raw_bint(disk, &raw_bint);
	if (retval != 0) {
		fprintf(stdout, "Failed to read properties for %s\n", disk->info.devname);
		return 0;
	}

	if (memcmp(raw_bint.magic, "QUADSTOR", strlen("QUADSTOR")))
		return 0;

	if (memcmp(raw_bint.quad_prod, "VTL", strlen("VTL")))
		return 0;

	if (formaster && !atomic_test_bit(GROUP_FLAGS_MASTER, &raw_bint.group_flags))
		return 0;
	else if (!formaster && atomic_test_bit(GROUP_FLAGS_MASTER, &raw_bint.group_flags))
		return 0;

	if (!formaster) {
		master_disk = locate_group_master(raw_bint.group_id);
		if (!master_disk) {
			fprintf(stdout, "Cannot find master for pool %s. Skipping disk %s\n", raw_bint.group_name, disk->info.devname);
			return 0;
		}
	}
	group_info = find_group(raw_bint.group_id);
	if (!group_info) {
		group_info = add_group(&raw_bint, testmode);
		if (!group_info) {
			fprintf(stdout, "Adding back pool %s failed\n", raw_bint.group_name);
			exit(1);
		}
	}
	group_info->offline = 0;

	disk->group_flags = raw_bint.group_flags;
	disk->group_id = raw_bint.group_id;
	disk->bid = raw_bint.bid;
	memcpy(disk->mrid, raw_bint.mrid, TL_RID_MAX);

	if (__srv_disk_configured(disk, group_info))
		return 0;

	if (memcmp(raw_bint.vendor, disk->info.vendor, 8)) {
		fprintf(stdout, "Vendor mismatch %.8s %.8s\n", raw_bint.vendor, disk->info.vendor);
		return 0;
	}

	if (memcmp(raw_bint.product, disk->info.product, 16)) {
		fprintf(stdout, "Product mismatch %.16s %.16s\n", raw_bint.product, disk->info.product);
		return 0;
	}

	if (!raw_bint_serial_match(&raw_bint, disk->info.serialnumber, disk->info.serial_len)) {
		fprintf(stdout, "Serial number mismatch %.32s %.32s\n", raw_bint.serialnumber, disk->info.serialnumber);
		return 0;
	}

	if (raw_bint.flags & RID_SET) {
		fprintf(stdout, "Adding %sPhysical Disk  %s Vendor: %.8s Model: %.16s Serial Number: %.32s\n", master_str, disk->info.devname, disk->info.vendor, disk->info.product, disk->info.serialnumber);
	}
	else {
		fprintf(stdout, "Vendor: %.8s\n", disk->info.vendor);
		fprintf(stdout, "Model: %.16s\n", disk->info.product);
		fprintf(stdout, "Serial Number: %.32s\n", disk->info.serialnumber);
		sprintf(msg, "Add %sPhysical Disk with path %s ? ", master_str, disk->info.devname);
		retval = prompt_user(msg);
		if (retval != 1) {
			disk->ignore = 1;
			return 0;
		}
	}

	blkdev = blkdev_new(disk->info.devname);
	if (!blkdev) {
		fprintf(stdout, "Memory allocation failure\n");
		exit(1);
	}

	memcpy(&blkdev->disk, disk, offsetof(struct physdisk, q_entry));
	strcpy(blkdev->devname, disk->info.devname);
	blkdev->bid = raw_bint.bid;
	blkdev->group = group_info;
	blkdev->group_id = group_info->group_id;
	bdev_list[blkdev->bid] = blkdev;
	TAILQ_INSERT_TAIL(&group_info->bdev_list, blkdev, g_entry);
	if (testmode) {
		return 0;
	}

	conn = sql_add_blkdev(disk, raw_bint.bid, group_info->group_id);
	if (!conn) {
		fprintf(stdout, "Failed to update disk information for %s", disk->info.devname);
		exit(1);
	}

	retval = pgsql_commit(conn);
	if (retval != 0) {
		fprintf(stdout, "Failed to commit transaction %s", disk->info.devname);
		exit(1);
	}
	return 0;
}

static void
mark_vdevices_offline(void)
{
	struct vdevice *vdevice;
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		vdevice = device_list[i];
		if (!vdevice)
			continue;
		vdevice->offline = 2;
	} 
}

static void
mark_bdevs_offline(void)
{
	struct tl_blkdevinfo *blkdev;
	int i;

	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		blkdev->offline = 2;
	}
}

static void
mark_pools_offline(void)
{
	struct group_info *group_info;
	int i;

	for (i = 1; i < TL_MAX_POOLS; i++) {
		group_info = group_list[i];
		if (!group_info)
			continue;
		group_info->offline = 2;
	}
}

static void
prune_volumes(struct vlist *vol_list)
{
	struct vcartridge *vinfo, *next;
	PGconn *conn;

	conn = pgsql_begin();
	if (!conn)
		return;

	TAILQ_FOREACH_SAFE(vinfo, vol_list, q_entry, next) {
		if (vinfo->loaderror != 2)
			continue;
		TAILQ_REMOVE(vol_list, vinfo, q_entry);
		fprintf(stdout, "Removing vcartridge %s\n", vinfo->label);
		if (testmode)
			continue;
		sql_delete_vcartridge(conn, vinfo->tl_id, vinfo->tape_id);
	}
	pgsql_commit(conn);
}

static void
prune_bdev_volumes(void)
{
	struct tl_blkdevinfo *blkdev;
	int i;

	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		prune_volumes(&blkdev->vol_list);
	}
}

static void
prune_disks(void)
{
	struct tl_blkdevinfo *blkdev;
	struct physdisk *disk;
	int i;

	for (i = 1; i < TL_MAX_DISKS; i++) {
		blkdev = bdev_list[i];
		if (!blkdev)
			continue;
		if (blkdev->offline != 2)
			continue;

		disk = &blkdev->disk;
		fprintf(stdout, "Removing physical Disk  %s Vendor: %.8s Model: %.16s Serial Number: %.32s\n", disk->info.devname, disk->info.vendor, disk->info.product, disk->info.serialnumber);
		bdev_list[i] = NULL;
		if (testmode) {
			free(blkdev);
			continue;
		}
		sql_delete_blkdev(blkdev);
		free(blkdev);
	}
}

static void
prune_pools(void)
{
	struct group_info *group_info;
	int i;

	for (i = 1; i < TL_MAX_POOLS; i++) {
		group_info = group_list[i];
		if (!group_info)
			continue;
		if (group_info->offline != 2)
			continue;
		fprintf(stdout, "Removing storage pool %s\n", group_info->name);
		group_list[i] = NULL;
		if (testmode) {
			free(group_info);
			continue;
		}
		sql_delete_group(group_info->group_id);
		free(group_info);
	}
}

static void
prune_devices(void)
{
	struct vdevice *vdevice;
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		vdevice = device_list[i];
		if (!vdevice)
			continue;
		if (vdevice->offline != 2)
			continue;
		if (vdevice->type == T_CHANGER)
			fprintf(stdout, "Deleting VTL %s\n", vdevice->name);
		else
			fprintf(stdout, "Deleting VDrive %s\n", vdevice->name);
		device_list[i] = NULL;
		if (testmode) {
			free_vdevice(vdevice);
			continue;
		}
		sql_delete_vtl(vdevice->tl_id);
		free_vdevice(vdevice);
	}
}

int
main(int argc, char *argv[])
{
	struct physdisk *disk;
	int retval, fd;
	struct group_info *group_info;
	int c;

	if (geteuid() != 0) {
		fprintf(stdout, "This program can only be run as root\n");
		exit(1);
	}

	while ((c = getopt(argc, argv, "t")) != -1) {
		switch (c) {
		case 't':
			testmode = 1;
			break;
		default:
			fprintf(stdout, "Invalid option passed\n");
			exit(1);
		}
	}

	fd = open("/dev/null", O_WRONLY);
	if (fd >= 0)
		dup2(fd, 2);

	retval = sql_query_groups(group_list);
	if (retval != 0) {
		fprintf(stdout, "Error in getting configured pools\n");
		exit(1);
	}

	group_info = alloc_buffer(sizeof(*group_info));
	if (!group_info) {
		fprintf(stdout, "Memory allocation failure\n");
		exit(1);
	}

	group_info->group_id = 0;
	strcpy(group_info->name, DEFAULT_GROUP_NAME);
	TAILQ_INIT(&group_info->bdev_list);
	group_list[0] = group_info;

	tl_common_scan_physdisk();
	retval = sql_query_blkdevs(bdev_list);
	if (retval != 0) {
		fprintf(stdout, "Error in getting configured disks\n");
		exit(1);
	}

	retval = sql_query_vdevice(device_list);
	if (retval != 0) {
		fprintf(stdout, "Error in getting configured vdevices\n");
		exit(1);
	}

	mark_bdevs_offline();
	mark_pools_offline();
	mark_vdevices_offline();

	TAILQ_FOREACH(disk, &disk_list, q_entry) {
		check_disk(disk, 1);
	}

	TAILQ_FOREACH(disk, &disk_list, q_entry) {
		check_disk(disk, 0);
	}

	prune_disks();
	prune_pools();

	scan_vcartridges();

	prune_bdev_volumes();
	prune_devices();

	return 0;
}

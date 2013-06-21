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

#include <apicommon.h>
#include <physlib.h>
#include <netinet/in.h>
#include <pthread.h>
#include <ctype.h>
#include <assert.h>
#include <pgsql.h>
#include "sqlint.h"
#include "diskinfo.h"

#define DEFAULT_TIMEOUT   (1 * 60)
#define DEVICE_IDENTIFICATION_PAGE			0x83
#define UNIT_SERIAL_NUMBER_PAGE				0x80
/* Identifier types */
#define UNIT_IDENTIFIER_VENDOR_SPECIFIC		0x00
#define UNIT_IDENTIFIER_T10_VENDOR_ID		0x01
#define UNIT_IDENTIFIER_EUI64			0x02
#define UNIT_IDENTIFIER_NAA			0x03


struct d_list disk_list = TAILQ_HEAD_INITIALIZER(disk_list);  

static struct physdisk * tl_common_find_physdisk3(struct physdisk *disk);
static void add_disk(char *devname, char *vendor, char *product, char *serial, int serial_len, struct d_list *tmp_disk_list, int fake_ident, int controller_disk, int raid_disk, struct physdevice *device, int frompart);

struct ignore_dev {
	char *devname;
	TAILQ_ENTRY(ignore_dev) q_entry;
};
TAILQ_HEAD(ingore_dev_list, ignore_dev) ignore_dev_list = TAILQ_HEAD_INITIALIZER(ignore_dev_list);

static void
ignore_dev_list_free(void)
{
	struct ignore_dev *iter;

	while ((iter = TAILQ_FIRST(&ignore_dev_list)) != NULL) {
		TAILQ_REMOVE(&ignore_dev_list, iter, q_entry);
		free(iter);
	}
}

static int
ignore_dev_add(char *devname)
{
	struct ignore_dev *iter;

	if (strncmp(devname, "/dev", strlen("/dev")))
		return 0;

	iter = malloc(sizeof(*iter));
	if (!iter) {
		DEBUG_ERR_SERVER("Adding ignore devpath failed, memory allocation failure\n");
		return -1;
	}

	iter->devname = malloc(strlen(devname) + 1);
	if (!iter->devname) {
		free(iter);
		DEBUG_ERR_SERVER("Adding ignore devpath failed, memory allocation failure\n");
		return -1;
	}
	strcpy(iter->devname, devname);
	TAILQ_INSERT_TAIL(&ignore_dev_list, iter, q_entry); 
	return 0;
}

int
is_ignore_dev(char *devname)
{
	struct ignore_dev *iter;

	if (strncmp(devname, "/dev/", strlen("/dev/")))
		return 1;

	TAILQ_FOREACH(iter, &ignore_dev_list, q_entry) {
		if (strcmp(iter->devname, devname) == 0)
			return 1;
	}
	return 0;
}

static void
fake_device_identification(struct physdevice *device)
{
	memcpy(device->t10_id.vendor, device->vendor, 8);
	memcpy(device->t10_id.product, device->product, 16);
	memcpy(device->t10_id.serialnumber, device->serialnumber, 32);

	DEBUG_INFO("fake_device_identification: devname %s serialnumber %.32s vendor %.8s product %.16s", device->devname, device->serialnumber, device->vendor, device->product);
	device->idflags |= ID_FLAGS_T10;
}

static int
read_serial(char *devpath, char *serial)
{
	char buf[4096];
	struct raw_bdevint *bint;
	int retval;

	retval = read_from_device(devpath, buf, sizeof(buf), BDEV_META_OFFSET);
	if (retval < 0)
		return -1;

	bint = (struct raw_bdevint *)(buf);
	if (memcmp(bint->magic, "QUADSTOR", strlen("QUADSTOR"))) {
		return 0;
	}

	DEBUG_INFO("Got serial number :%.32s: magic %.8s vendor %.8s product %.16s dev %s \n", bint->serialnumber, bint->magic, bint->vendor, bint->product, devpath);
	memcpy(serial, bint->serialnumber, sizeof(bint->serialnumber));
	serial[32] = 0;
	return 1;
}

static int
serial_number_unique(char *serial, struct d_list *tmp_disk_list)
{
	struct physdisk *iter;

	TAILQ_FOREACH(iter, tmp_disk_list, q_entry) {
		if (strncmp(iter->info.serialnumber, "GEN", 3))
			continue;
		if (strncmp(iter->info.serialnumber, serial, strlen(serial)))
			continue;
		return 0;
	}
 
	TAILQ_FOREACH(iter, &disk_list, q_entry) {
		if (strncmp(iter->info.serialnumber, "GEN", 3))
			continue;
		if (strncmp(iter->info.serialnumber, serial, strlen(serial)))
			continue;
		return 0;
	}
 
	return 1;

}

#ifdef FREEBSD
static long int get_random()
{
	srandomdev();
	return random();
}
#else
static long int get_random()
{
	struct timeval tv;
	struct timezone tz;
	long int res;
	struct drand48_data buffer;

	gettimeofday(&tv, &tz);
	srand48_r(tv.tv_sec + tv.tv_usec, &buffer);
	lrand48_r(&buffer, &res);
	return res;
}
#endif

static void
gen_serial(char *vendor, char *product, char *ident, struct d_list *tmp_disk_list)
{
	long rd;
	int unique;

again:
	rd = get_random();
	sprintf(ident, "GEN%020ld", rd);
	DEBUG_INFO("Gen serial number :%.32s: vendor %.8s product %.16s \n", ident, vendor, product);
	unique = serial_number_unique(ident, tmp_disk_list);
	if (!unique) {
		DEBUG_INFO("Gen serial number :%.32s: vendor %.8s product %.16s, found another duplicate, generating again \n", ident, vendor, product);
		goto again;
	}
}

#ifdef FREEBSD
void
geom_get_properties(char *path, char *ident, int size, uint64_t *mediasize)
{
	int fd;

	memset(ident, 0, size);
	fd = g_open(path, 0);
	if (fd < 0)
		return;
	*mediasize = g_mediasize(fd);
	g_get_ident(fd, ident, size);
	g_close(fd);
	return;
}
#endif

static struct physdisk *
alloc_disk(char *devname, char *vendor, char *product, char *serialnumber, int serial_len, uint64_t size, int fake_ident, int controller_disk, int raid_disk, struct d_list *tmp_disk_list, int partid, struct physdevice *iddev)
{
	struct physdevice *device;
	struct physdisk *disk;
	char ident[DISK_IDENT_SIZE + 8];
	int retval;

	device = alloc_buffer(sizeof(*disk));
	if (!device) {
		DEBUG_ERR_SERVER("Memory allocation failure\n");
		return NULL;
	}

	disk = (struct physdisk *)device;
	memset(device->vendor, ' ', sizeof(device->vendor));
	memset(device->product, ' ', sizeof(device->product));
	memset(device->serialnumber, ' ', sizeof(device->serialnumber));
	strcpy(device->devname, devname);

#ifdef FREEBSD
	geom_get_properties(devname, ident, DISK_IDENT_SIZE, &size);
	if (ident[0] && !serial_len) {
		serialnumber = ident;
		serial_len = strlen(ident);
	}
#endif

	if (iddev) {
		device->idflags = iddev->idflags;
		memcpy(&device->t10_id, &iddev->t10_id, sizeof(device->t10_id));
		memcpy(&device->naa_id, &iddev->naa_id, sizeof(device->naa_id));
		memcpy(&device->eui_id, &iddev->eui_id, sizeof(device->eui_id));
		memcpy(&device->unknown_id, &iddev->unknown_id, sizeof(device->unknown_id));
		memcpy(&device->vspecific_id, &iddev->vspecific_id, sizeof(device->vspecific_id));
	}

	if (!serial_len) {
		retval = read_serial(devname, ident);
		if (retval < 0) {
			free(device);
			return NULL;
		}
		if (retval == 0)
			gen_serial(vendor, product, ident, tmp_disk_list);

		serial_len = strlen(ident);
		serialnumber = ident;
	}

	memcpy(device->vendor, vendor, strlen(vendor));
	memcpy(device->product, product, strlen(product));
	memcpy(device->serialnumber, serialnumber, serial_len);
	if (fake_ident)
		fake_device_identification(device);

	disk->size = size;
	disk->info.online = 1;
	disk->controllerdisk = controller_disk;
	disk->raiddisk = raid_disk;
	disk->partid = partid;
	TAILQ_INSERT_TAIL(tmp_disk_list, disk, q_entry); 
	return disk;
}

char *
buf_strip(char *buf)
{
	while (buf[0] && (buf[0] == ' ' || buf[0] == '\t' || buf[0] == '\n')) {
		buf++;
	}
	return buf;
}
static int
build_zdev_list(void)
{
	FILE *fp;
	char buf[512];
	char devname[256], tmp[256];
	struct stat stbuf;
	char *ptr;
	int retval;

	fp = popen("zpool status", "r");
	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (buf[0] != '\t')
			continue;
		ptr = buf_strip(buf);
		if (*ptr == '\0')
			continue;
		retval = sscanf(ptr, "%s", tmp);
		if (retval != 1)
			continue;
		sprintf(devname, "/dev/%s", tmp);
		if (stat(devname, &stbuf) < 0)
			continue;
		ignore_dev_add(devname);
	}
	pclose(fp);
	return 0;
}

#ifdef FREEBSD
static int
build_raid_list(void)
{
	char devname[256];
	FILE *fp;
	char buf[512];
	char *tmp;
	int retval;

	fp = popen("/sbin/gvinum list | grep \"^D\"", "r");
	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		tmp = strstr(buf, "State:");
		if (!tmp)
			continue;

		retval = sscanf(tmp, "%*s %*s %s", devname);
		if (retval != 1)
			continue;
		ignore_dev_add(devname);
	}
	pclose(fp);
	return 0;
}

static int
build_swap_list(void)
{
	char buf[512];
	char swapdev[256];
	FILE *fp;

	fp = popen("/usr/sbin/swapinfo", "r");

	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (sscanf(buf, "%s", swapdev) != 1)
			continue;
		if (strcmp(swapdev, "Device") == 0)
			continue;
		ignore_dev_add(swapdev);
	}
	pclose(fp);
	return 0;
}

static int
build_mount_list(void)
{
	struct statfs *mntinfo;
	struct statfs *ent;
	int count;

	count = getmntinfo(&mntinfo, MNT_WAIT);

	if (!count)
		return 0;

	for (ent = mntinfo; count--; ent++) {
		ignore_dev_add(ent->f_mntfromname);
	}
	return 0;
}
#else

#define MDADM_PROG	"/sbin/mdadm"

static int
build_mddev(char *name)
{
	char *tmp;
	char devname[256];

	tmp = strchr(name, '[');
	if (tmp)
		*tmp = 0;

	sprintf(devname, "/dev/%s", name);
	ignore_dev_add(devname);
	return 0;
}
static int
build_raid_list(void)
{
	char buf[2048];
	FILE *fp;
	char *ptr, *tmp;
	int skip;

	fp = fopen("/proc/mdstat", "r");
	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (buf[0] == ' ')
			continue;
		if (buf[strlen(buf) - 1] == '\n')
			buf[strlen(buf) - 1] = 0;

		if (strncmp(buf, "Personalities", strlen("Personalities")) == 0)
			continue;

		if (strncmp(buf, "unused", strlen("unused")) == 0)
			continue;

		ptr = buf;
		skip = 4;
		while (1) {
			tmp = strchr(ptr, ' ');
			if (!tmp) {
				build_mddev(ptr);
				break;
			}
			if (skip) {
				skip--;
				ptr = tmp+1;
				continue;
			}

			*tmp = 0;
			build_mddev(ptr);
			ptr = tmp+1;
		}
	}
	fclose(fp);
	return 0;
}

static int
__build_mount_list(char *path)
{
	FILE *fp;
	struct mntent *ent;

	fp = setmntent(path, "r");
	if (!fp)
		return 0;

	while ((ent = getmntent(fp))) {
		ignore_dev_add(ent->mnt_fsname);
	}
	endmntent(fp);
	return 0;
}

static int
build_mount_list(void)
{
	__build_mount_list(_PATH_MNTTAB);
	__build_mount_list(_PATH_MOUNTED);
	return 0;
}

static int
build_pvs(void)
{
	char buf[512];
	char devname[256];
	FILE *fp;

	fp = popen("pvs", "r");
	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (sscanf(buf, "%s", devname) != 1)
			continue;
		if (strcmp(buf, "PV") == 0)
			continue;
		ignore_dev_add(devname);
	}
	pclose(fp);
	return 0;

}

static int
build_swap_list(void)
{
	char buf[512];
	char swapdev[256];
	FILE *fp;

	fp = fopen("/proc/swaps", "r");

	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (sscanf(buf, "%s", swapdev) != 1)
			continue;
		if (strcmp(buf, "Filename") == 0)
			continue;
		ignore_dev_add(swapdev);
	}
	fclose(fp);
	return 0;
}

#endif

static int
is_blkdev_valid(char *devname, uint64_t *size, int israid)
{
	int retval;

	DEBUG_INFO("is_blkdev_valid: dev %s israid %d\n", devname, israid);
	retval = is_ignore_dev(devname);
	if (retval)
		return -1;

	if (size) {
		disk_getsize(devname, size);
		if (*size < MIN_PHYSDISK_SIZE) {
			DEBUG_INFO("Skipping disk %s as size %llu is less than %llu\n", devname, (unsigned long long)*size, (unsigned long long)MIN_PHYSDISK_SIZE);
			return -1;
		}
	}
	return 0;
}

static int
scan_partitions(char *start, char type, struct d_list *tmp_disk_list, char *vendor, char *product, char *serial, int serial_len, int fake_ident, int controller_disk, int raid_disk, struct physdevice *device, int frompart)
{
	FILE *fp;
	char cmd[512];
	char buf[512];
	char devname[256];
	char *tmp;
	uint64_t size;
	int retval = 0;
	int partid;

	if (frompart)
		sprintf(cmd, "ls -1 %s* | grep \"%s[a-z]\"", start, start);
	else if (type)
		sprintf(cmd, "ls -1 %s%c* | grep \"%s%c[0-9]\"", start, type, start, type);
	else
		sprintf(cmd, "ls -1 %s* | grep \"%s[0-9]\"", start, start);

	DEBUG_INFO("scan_partitions: cmd %s start %s type %c\n", cmd, start, type);

	fp = popen(cmd, "r");
	if (!fp) {
		DEBUG_ERR_SERVER("Unable to execute program %s\n", cmd);
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		buf[strlen(buf) - 1] = 0;
		if (strcmp(buf, start) == 0)
			continue;
		retval = 1;
		strcpy(devname, buf);
		if (type)
			tmp = buf + (strlen(start) + 1);
		else
			tmp = buf + (strlen(start));

		DEBUG_INFO("scan_partitions: devname %s tmp :%s: \n", devname, tmp);

		if (is_blkdev_valid(devname, &size, raid_disk) != 0) {
			DEBUG_INFO("Ignoring dev at %s as it has mounted partitions\n", devname);
			continue;
		}

		DEBUG_INFO("Dev at %s is valid\n", devname);
		partid = atoi(tmp);

		if (is_ignore_dev(devname) || !partid)
			continue;

#ifdef FREEBSD
		add_disk(devname, vendor, product, serial, serial_len, tmp_disk_list, fake_ident, controller_disk, raid_disk, device, 1);
#else
		alloc_disk(devname, vendor, product, serial, serial_len, size, fake_ident, controller_disk, raid_disk, tmp_disk_list, partid, device);
#endif
	}

	pclose(fp);
	return retval;
}

static inline void
partitions_list_insert(struct d_list *tmp_disk_list, struct d_list *p_disk_list)
{
	struct physdisk *device;

	while ((device = TAILQ_FIRST(p_disk_list)) != NULL) {
		TAILQ_REMOVE(p_disk_list, device, q_entry);
		TAILQ_INSERT_TAIL(tmp_disk_list, device, q_entry);
	}
}

static void 
add_disk(char *devname, char *vendor, char *product, char *serial, int serial_len, struct d_list *tmp_disk_list, int fake_ident, int controller_disk, int raid_disk, struct physdevice *device, int frompart)
{
	uint64_t size;
	int retval;

	DEBUG_INFO("add disk %s\n", devname);
	if (!frompart) {
		retval = scan_partitions(devname, 'p', tmp_disk_list, vendor, product, serial, serial_len, fake_ident, controller_disk, raid_disk, device, 0);

		if (retval != 0)
			return;
	}

#ifdef FREEBSD
	if (frompart)
		retval = scan_partitions(devname, 0, tmp_disk_list, vendor, product, serial, serial_len, fake_ident, controller_disk, raid_disk, device, 1);
	else
		retval = scan_partitions(devname, 's', tmp_disk_list, vendor, product, serial, serial_len, fake_ident, controller_disk, raid_disk, device, 0);
	if (retval != 0)
		return;
#else
	if (!raid_disk) {
		retval = scan_partitions(devname, 0, tmp_disk_list, vendor, product, serial, serial_len, fake_ident, controller_disk, raid_disk, device, 0);
		if (retval != 0)
			return;
	}
#endif

	if (is_blkdev_valid(devname, &size, raid_disk) != 0) {
		DEBUG_INFO("Ignoring dev at %s as it has mounted partitions\n", devname);
		return;
	}

	alloc_disk(devname, vendor, product, serial, serial_len, size, fake_ident, controller_disk, raid_disk, tmp_disk_list, 0, device);
}
 
void
print_cmd_response(uint8_t *buffer, int buffer_len)
{
#ifdef ENABLE_DEBUG
	char *str;
	int i;

	DEBUG_INFO("print_cmd_response: \n");
	str = malloc((buffer_len * 3 + 1));
	if (!str)
	{
		return;
	}

	for (i = 0; i < buffer_len; i++)
	{
		sprintf(str+(i * 3), "%02x ", buffer[i]);
	}
	DEBUG_INFO("%s\n", str);
	free(str);
#endif
}


void
parse_sense_buffer(uint8_t *sense, struct sense_info *sense_info)
{
	sense_info->sense_valid = 1;
	sense_info->response = sense[0];
	sense_info->sense_key = sense[2];
	sense_info->asc = sense[12];
	sense_info->ascq = sense[13];
	sense_info->information = ntohl(*((uint32_t *)(sense+3)));
}

int
copy_desc(struct element_info *einfo, uint8_t *buffer, uint8_t *element_data, int desc_len)
{
	uint8_t *desc = einfo->desc;
	int element_offset;
	int desc_offset;
	int min;

	memcpy(desc, element_data, 12);
	desc_offset = element_offset = 12;
	desc_len -= 12;

	if (buffer[9] & 0x80)
	{
		if ((desc_len - 36) < 0)
		{
			return -1;
		}

		memcpy(desc+desc_offset, element_data+element_offset, 36);
		element_offset += 36;
		desc_len -= 36;
	}
	desc_offset += 36; 

	if (buffer[9] & 0x40)
	{
		element_offset += 36;
		desc_len -= 36;
	}

	if (desc_len < 0)
	{
		return -1;
	}

	min = desc_len;
	if (min > (sizeof(einfo->desc) - desc_offset))
	{
		min = sizeof(einfo->desc) - desc_offset;
	}

	memcpy(desc+desc_offset, element_data+element_offset, min);
	return 0;
}

int
do_inquiry_page_code(struct physdevice *device, uint8_t page_code)
{
	unsigned char *buffer = NULL;
	int buffer_len = 196;
	unsigned char cmd[6]; /* 6 byte command */
	unsigned char sense[32];
	int retval = -1;
	int data_len, resid;
	struct scsi_request request;

	buffer = alloc_buffer(buffer_len);
	if (!buffer)
	{
		DEBUG_ERR_SERVER("Memory allocation failure for %d bytes\n", buffer_len);
		goto err;
	}

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = INQUIRY;
	cmd[1] |= 0x01; /* EVPD */
	cmd[2] |= page_code;
	cmd[4] = buffer_len;

	set_scsi_request(&request, device->devname, O_RDONLY, cmd, sizeof(cmd), buffer, buffer_len, NULL, 0, sense, sizeof(sense), DEFAULT_TIMEOUT);
	retval = send_scsi_request(&request);
	if (retval != 0)
	{
		DEBUG_ERR("send_scsi_request failed with res %d\n", retval);
		goto err;
	}


	if (request.scsi_status != SCSI_STATUS_OK)
	{
		DEBUG_ERR("Failed result status %d\n", request.scsi_status);
		retval = -1;
		goto err;
	}

	data_len = buffer_len;
	resid = request.resid;
	if (resid > 0)
	{
		data_len -= resid;
	}

	DEBUG_INFO("do_inquiry_all_pages: print inquiry response: page_code %x datta_len %d\n", page_code, data_len);
	print_cmd_response(buffer, data_len);
	retval = 0;
err:
	if (buffer)
	{
		free(buffer);
	}
	return retval;

}

int
do_inquiry(struct physdevice *device)
{
	unsigned char *buffer = NULL;
	int buffer_len = 196;
	unsigned char cmd[6]; /* 6 byte command */
	unsigned char sense[32];
	int retval = -1;
	int data_len, resid;
	struct scsi_request request;

	buffer = alloc_buffer(buffer_len);
	if (!buffer)
	{
		DEBUG_ERR_SERVER("Memory allocation failure for %d bytes\n", buffer_len);
		goto err;
	}

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = INQUIRY;
	cmd[1] |= 0x00; /* EVPD */
	cmd[4] = buffer_len;

	set_scsi_request(&request, device->devname, O_RDONLY, cmd, sizeof(cmd), buffer, buffer_len, NULL, 0, sense, sizeof(sense), DEFAULT_TIMEOUT); 
	retval = send_scsi_request(&request);
	if (retval != 0)
	{
		DEBUG_ERR("send_scsi_request failed with res %d\n", retval);
		goto err;
	}

	if (request.scsi_status != SCSI_STATUS_OK)
	{
		DEBUG_ERR("Failed result status %d\n", request.scsi_status);
		retval = -1;
		goto err;
	}

	data_len = buffer_len;
	resid = request.resid;
	if (resid > 0)
	{
		data_len -= resid;
	}

	DEBUG_INFO("print inquiry response: datta_len %d\n", data_len);
	print_cmd_response(buffer, data_len);
	retval = 0;
err:
	if (buffer)
	{
		free(buffer);
	}
	return retval;
}

int
do_unit_serial_number(char *devname, char *serialnumber, int *serial_len)
{
	unsigned char *buffer = NULL;
	int buffer_len = 196;
	unsigned char cmd[6]; /* 6 byte command */
	unsigned char sense[32];
	int retval = -1;
	int min;
	struct scsi_request request;

	buffer = alloc_buffer(buffer_len);
	if (!buffer)
	{
		DEBUG_ERR_SERVER("Memory allocation failure for %d bytes\n", buffer_len);
		goto err;
	}

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = INQUIRY;
	cmd[1] |= 0x01; /* EVPD */
	cmd[2] |= UNIT_SERIAL_NUMBER_PAGE;
	cmd[4] = buffer_len;

	set_scsi_request(&request, devname, O_RDONLY, cmd, sizeof(cmd), buffer, buffer_len, NULL, 0, sense, sizeof(sense), DEFAULT_TIMEOUT);
	retval = send_scsi_request(&request);
	if (retval != 0)
	{
		DEBUG_INFO("Reading serial number failed for device %s with res %d\n", devname, retval);
		goto err;
	}

	if (request.scsi_status != SCSI_STATUS_OK)
	{
		DEBUG_ERR_SERVER("Failed result status %d\n", request.scsi_status);
		retval = -1;
		goto err;
	}

	/* Ok now the buffer has the serial number */
	min = buffer[3];
	if (min > *serial_len)
		min = *serial_len;

	memcpy(serialnumber, buffer+4, min);
	*serial_len = min;
	retval = 0;
err:
	if (buffer)
		free(buffer);
	return retval;
}


int
do_device_identification(char *devname, struct physdevice *device)
{
	unsigned char *buffer = NULL;
	int buffer_len = 254;
	unsigned char cmd[6]; /* 6 byte command */
	unsigned char sense[32];
	int retval = -1;
	int offset = 0;
	int page_length;
	int data_len;
	int resid;
	struct identify_header *header;
	struct scsi_request request;

	DEBUG_INFO("Entered do_device_identification for devname %s\n", devname); 
	buffer = alloc_buffer(buffer_len);
	if (!buffer)
	{
		DEBUG_ERR_SERVER("Memory allocation failure for %d bytes\n", buffer_len);
		goto err;
	}

	memset(cmd, 0, sizeof(cmd));
	cmd[0] = INQUIRY;
	cmd[1] |= 0x01; /* EVPD */
	cmd[2] |= DEVICE_IDENTIFICATION_PAGE;
	cmd[4] = buffer_len;

	DEBUG_INFO("do_device_identification cmd is 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5]);

	set_scsi_request(&request, devname, O_RDONLY, cmd, sizeof(cmd), buffer, buffer_len, NULL, 0, sense, sizeof(sense), DEFAULT_TIMEOUT);
	retval = send_scsi_request(&request);
	if (retval != 0)
	{
		DEBUG_INFO("send_scsi_request failed with res %d\n", retval);
		goto err;
	}

	data_len = buffer_len;
	resid = request.resid;
	if (resid > 0)
	{
		data_len -= resid;
	}

	DEBUG_INFO("do_device_identification: resid %d data_len %d buffer_len %d\n", resid, data_len, buffer_len);

	if (request.scsi_status != SCSI_STATUS_OK)
	{
		DEBUG_ERR_SERVER("Device identifcation failed for %s with result status %d\n", devname, request.scsi_status);
		retval = -1;
		goto err;
	}

	DEBUG_INFO("do_device_identification: print_cmd_response\n");
	print_cmd_response(buffer, data_len);

	page_length = buffer[3];
	DEBUG_INFO("page info: pd type 0x%hhx\n", buffer[0]);
	DEBUG_INFO("pagecode 0x%hhx \n", buffer[1]);
	DEBUG_INFO("pagelength 0x%hhx\n", buffer[3]);

	if (page_length)
	{
		page_length -= 4;
	}

	offset = 4;
	DEBUG_INFO("orig page_length %d page_length now is %d\n", buffer[3], page_length);
	while (page_length > 0)
	{
		uint8_t *idbuffer;
		uint8_t id_len;
		uint8_t done = 0;
		int serial_min;

		header = (struct identify_header *)(buffer+offset);
		id_len = header->identifier_length;
		if (!id_len)
		{
			break;
		}
		idbuffer = (buffer + offset);
		done += sizeof(struct identify_header);
		DEBUG_INFO("do_device_identification: offset %d id_len %d done %d\n", offset, id_len, done);

		DEBUG_INFO("header->identifier_type 0x%x 0x%x\n", header->identifier_type, header->identifier_type & 0x0F);
		switch (header->identifier_type & 0x0F)
		{
			case UNIT_IDENTIFIER_T10_VENDOR_ID:
				DEBUG_INFO("do_device_identification: UNIT_IDENTIFER_T10_VENDOR_ID\n");
				memcpy(device->t10_id.vendor, idbuffer+done, 8);
				done += 8;
				memcpy(device->t10_id.product, idbuffer+done, 16);
				done += 16;
				serial_min = id_len - 24;
				if (serial_min < 0)
				{
					goto err;
				}

				if (serial_min > sizeof(device->t10_id.serialnumber))
				{
					serial_min = sizeof(device->t10_id.serialnumber);
				}
				memcpy(device->t10_id.serialnumber, idbuffer+done, serial_min);
				device->idflags |= ID_FLAGS_T10;
				DEBUG_INFO("do_device_identification: vendor %.8s, product %.16s, serialnumber %.32s::\n", device->t10_id.vendor, device->t10_id.product, device->t10_id.serialnumber); 
				break;
			case UNIT_IDENTIFIER_EUI64:
				DEBUG_INFO("do_device_identification: UNIT_IDENTIFIER_EUI64\n");
				memcpy(device->eui_id.eui_id, idbuffer+done, id_len);
				device->idflags |= ID_FLAGS_EUI;
				DEBUG_INFO("do_device_identification: id %.8s\n", device->eui_id.eui_id);
				break;
			case UNIT_IDENTIFIER_NAA:
				DEBUG_INFO("do_device_identification: UNIT_IDENTIFIER_NAA\n");
				memcpy(device->naa_id.naa_id, idbuffer+done, id_len);
				device->idflags |= ID_FLAGS_NAA;
				DEBUG_INFO("do_device_identification: id %.16s\n", device->naa_id.naa_id);
				break;
			case UNIT_IDENTIFIER_VENDOR_SPECIFIC:
				DEBUG_INFO("do_device_identification: UNIT_IDENTIFIER_VENDOR_SPECIFIC\n");
				memcpy(device->vspecific_id.vspecific_id, idbuffer+done, id_len);
				device->idflags |= ID_FLAGS_VSPECIFIC;
				DEBUG_INFO("do_device_identification: id %.16s\n", device->vspecific_id.vspecific_id);
				break;
			default:
				DEBUG_INFO("do_device_identification: vendor specfic or unknown\n");
				memcpy(device->unknown_id.unknown_id, idbuffer+done, id_len);
				device->idflags |= ID_FLAGS_UNKNOWN;
				break;
		}
		offset += (id_len + sizeof(struct identify_header));
		page_length -= (id_len + sizeof(struct identify_header));
	}

	retval = 0;
err:
	if (buffer)
	{
		free(buffer);
	}
	return retval;
}

int
do_test_unit_ready(char *devname, struct sense_info *sense_info)
{
	unsigned char cmd[6];
	unsigned char sense[32];
	int retval = -1;
	struct scsi_request request;

	DEBUG_INFO("Entered do_test_unit_ready\n");
	memset(cmd, 0, sizeof(cmd));
	cmd[0] = TEST_UNIT_READY; 

	DEBUG_INFO("do_test_unit_ready cmd is 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5]);
	set_scsi_request(&request, devname, O_RDWR, cmd, sizeof(cmd), NULL, 0, NULL, 0, sense, sizeof(sense), DEFAULT_TIMEOUT);
	retval = send_scsi_request(&request);
	if (retval != 0)
	{
		DEBUG_ERR_SERVER("scsi request failed with res %d\n", retval);
		goto err;
	}

	if (request.scsi_status != SCSI_STATUS_OK && request.scsi_status != SCSI_STATUS_CHECK_COND)
	{
		DEBUG_ERR_SERVER("Test unit ready failed for %s with result status %d\n", devname, request.scsi_status);
		retval = -1;
		goto err;
	}

	if (request.scsi_status == SCSI_STATUS_OK)
	{
		DEBUG_INFO("do_test_unit_ready: SCSI_PT_RESULT_GOOD\n");
		retval = 0;
	}
	else
	{
		DEBUG_INFO("do_test_unit_ready: SCSI_PT_RESULT_SENSE\n");
		parse_sense_buffer(sense, sense_info);
		retval = -1;
	}

err:
	return retval;
}

int
dump_device(FILE *fp, struct physdevice *device)
{
	fprintf(fp, "<type>%hhu</type>\n", device->type);
	fprintf(fp, "<online>%d</online>\n", device->online);
	fprintf(fp, "<ptags>\n");
	fwrite (device->vendor, 1, 8, fp);
	fwrite (device->product, 1, 16, fp);
	fwrite (device->serialnumber, 1, sizeof(device->serialnumber), fp);
	fprintf(fp, "\n</ptags>\n");
	if (device->online)
	{
		fprintf(fp, "<devname>%s</devname>\n", device->devname);
	}
	else
	{
		fprintf(fp, "<devname>Unknown</devname>\n");
	}	
	fprintf(fp, "<multipath>%d</multipath>\n", device->multipath);
	if (device->multipath)
	{
		fprintf(fp, "<mdevname>%s</mdevname>\n", device->mdevname);
	}
	fprintf(fp, "<idflags>%u</idflags>\n", device->idflags);

	if (device->idflags & ID_FLAGS_T10)
	{
		fprintf(fp, "<t10id>\n");
		fwrite (&device->t10_id, 1, sizeof(struct device_t10_id), fp);
		fprintf(fp, "\n</t10id>\n");
	}

	if (device->idflags & ID_FLAGS_NAA)
	{
		fprintf(fp, "<naa>\n");
		fwrite (&device->naa_id, 1, sizeof(struct device_naa_id), fp);
		fprintf(fp, "\n</naa>\n");
	}

	if (device->idflags & ID_FLAGS_EUI)
	{
		fprintf(fp, "<eui>\n");
		fwrite (&device->eui_id, 1, sizeof(struct device_eui_id), fp);
		fprintf(fp, "\n</eui>\n");
	}

	if (device->idflags & ID_FLAGS_VSPECIFIC)
	{
		fprintf(fp, "<vspecific>\n");
		fwrite (&device->vspecific_id, 1, sizeof(struct device_vspecific_id), fp);
		fprintf(fp, "\n</vspecific>\n");
	}

	if (device->idflags & ID_FLAGS_UNKNOWN)
	{
		fprintf(fp, "<unknown>\n");
		fwrite (&device->unknown_id, 1, sizeof(struct device_unknown_id), fp);
		fprintf(fp, "\n</unknown>\n");
	}
	return 0;
}

int dump_disk(FILE *fp, struct physdisk *disk, uint32_t bid)
{
	struct physdevice *device = (struct physdevice *)(disk);
	fprintf(fp, "bid: %u\n", bid);
	fprintf(fp, "partid: %u\n", disk->partid);
	fprintf(fp, "size: %"PRIu64"\n", disk->size);
	fprintf(fp, "used: %"PRIu64"\n", disk->used);
	fprintf(fp, "reserved: %"PRIu64"\n", disk->reserved);
	fprintf(fp, "raiddisk: %d\n", disk->raiddisk);
	fprintf(fp, "unmap: %d\n", disk->unmap);
	fprintf(fp, "write_cache: %d\n", disk->write_cache);
	if (disk->group_name[0])
		fprintf(fp, "group_name: %s\n", disk->group_name);
	else
		fprintf(fp, "group_name: Unknown\n");
	return dump_device(fp, device);
}

int
get_dev_numbers(struct physdevice *device, char *devnumber, int israid)
{
	char cmd[256];
	char alias[50];
	FILE *fp;
	char buf[30];

	device_get_alias(device->devname, alias);

	sprintf(cmd, "/bin/cat /sys/block/%s/dev", alias);
	fp = popen(cmd, "r");
	if (!fp)
	{
		return -1;
	}

	buf[0] = 0;
	fgets(buf, sizeof(buf), fp);
	pclose(fp);	
	if (!buf[0])
	{
		return -1;
	}

	buf[strlen(buf) - 1] = 0; /* \n */
	strcpy(devnumber, buf);
	return 0;
}

#ifdef LINUX
int
is_valid_mddev(char *mddev, char *raidtype)
{
	char cmd[256];
	char buf[1024];
	FILE *fp;
	int mdraid_disk = 0;
	char *tmp;

	sprintf(cmd, "%s --detail /dev/%s", MDADM_PROG, mddev);
	fp = popen(cmd, "r");
	if (!fp)
	{
		DEBUG_ERR_SERVER("Cannot execute command %s\n", cmd);
		return 0;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
		if ((tmp = strstr(buf, "Raid Level")))
		{
			tmp += (strlen("Raid Level : "));
			if (sscanf(tmp, "%[^\n]", raidtype) != 1)
				continue;
			mdraid_disk = 1;
			break;
		}
	}
	pclose(fp);
	return mdraid_disk;
}
#endif

static void 
mark_disks_offline(void)
{
	struct physdisk *iter;

	TAILQ_FOREACH(iter, &disk_list, q_entry) {
		iter->info.online = 0;
	}
}

static void
prune_offline_disks(void)
{
	struct physdisk *iter, *next;

	TAILQ_FOREACH_SAFE(iter, &disk_list, q_entry, next) {
		if (iter->info.online)
			continue;

		TAILQ_REMOVE(&disk_list, iter, q_entry);
	}
}

int
read_from_device(char *devpath, char *buf, int len, int offset)
{
	int fd;
	void *ptr;
	int retval;

	fd = open(devpath, O_RDONLY | O_DIRECT);
	if (fd < 0) {
		DEBUG_WARN_SERVER("Failed to open devpath %s for reading\n", devpath);
		return -1;
	}

	retval = posix_memalign(&ptr, len, len);
	if (retval != 0) {
		DEBUG_WARN_SERVER("posix memalign failed for %d\n", len);
		close(fd);
		return -1;
	}

	retval = lseek(fd, offset, SEEK_SET);
	if (retval != offset) {
		DEBUG_WARN_SERVER("for device %s seek failed to offset %d retval %d\n", devpath, offset, retval);
		close(fd);
		free(ptr);
		return -1;
	}

	retval = read(fd, ptr, len);
	close(fd);
	if (retval != len) {
		DEBUG_WARN_SERVER("for device %s read failed with errno %d retval %d len %d\n", devpath, errno, retval, len);
		free(ptr);
		return -1;
	}
	memcpy(buf, ptr, len);
	free(ptr);
	return 0;
}

int
tl_common_scan_zvol(struct d_list *tmp_disk_list)
{
	FILE *fp;
	char buf[512];
	char devname[256];
	char dirname[256];
	int len;
	char *tmp;

	fp = popen("ls -1R /dev/zvol/", "r");

	strcpy(dirname, "/dev/zvol");
	while (fgets(buf, sizeof(buf), fp) != NULL) {
		tmp = strrchr(buf, '\n');
		if (tmp)
			*tmp = 0;

		if (strncmp(buf, "/dev", strlen("/dev")) == 0) {
			len = strlen(buf);
			if (buf[len - 1] == ':')
				buf[len - 1] = 0;
			strcpy(dirname, buf);
			continue;
		}

		if (strlen(buf) == 0) {
			strcpy(dirname, "/dev/zvol");
			continue;
		}

		if (strcmp(dirname, "/dev/zvol") == 0)
			continue;

		sprintf(devname, "%s/%s", dirname, buf);
		add_disk(devname, "ZVOL", "ZVOL", NULL, 0, tmp_disk_list, 1, 1, 0, NULL, 0);
	}
	pclose(fp);
	return 0;
}
#ifdef FREEBSD
int
tl_common_scan_controller(char *start, char *vendor, char *product, struct d_list *tmp_disk_list)
{
	/* Scan the list of Physical Disks available on this system */
	FILE *fp;
	char cmd[256];
	char buf[512];
	char devname[256];

//	sprintf(cmd, "ls -1 /dev/%s[0-9]{,[0-9]}", start);
	sprintf(cmd, "ls -1 /dev/%s* 2> /dev/null | grep -v \"%s[0-9].*[a-z]\"", start, start);

	fp = popen(cmd, "r");

	if (!fp) {
		DEBUG_ERR_SERVER("Unable to execute program %s\n", cmd);
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {

		buf[strlen(buf) - 1] = 0;
		strcpy(devname, buf);

		DEBUG_INFO("Dev at %s is valid\n", devname);

		if (is_ignore_dev(devname))
			continue;

		add_disk(devname, vendor, product, NULL, 0, tmp_disk_list, 1 /*fake_ident*/, 1 /* controller_disk */, 0 /*raid disk */, NULL, 0);
	}
	pclose(fp);
	return 0;
}

int
tl_common_scan_raiddisk(struct d_list *tmp_disk_list)
{
	FILE *fp;
	char buf[512];
	char devname[512];
	char *tmp;
	
	fp = popen("/sbin/gvinum list | grep \"^V\" | cut -f 2 -d' '", "r");
	if (!fp)
	{
		DEBUG_WARN_SERVER("Cannot execute /sbin/gvinum list\n");
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
		tmp = strrchr(buf, '\n');
		if (tmp)
			*tmp = 0;

		sprintf(devname, "/dev/gvinum/%s", buf);

		if (is_ignore_dev(devname))
			continue;

		add_disk(devname, "GVINUM", "RAID",  NULL, 0, tmp_disk_list, 1, 0, 1, NULL, 0);
	}

	pclose(fp);

	return 0;
}
#else 
int
tl_common_scan_lvs(struct d_list *tmp_disk_list)
{
	FILE *fp;
	char lv[128], vg[128];
	char buf[512];
	char devname[256];
	int retval;

	fp = popen("lvs", "r");
	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		retval = sscanf(buf, "%s %s", lv, vg);
		if (retval != 2)
			continue;
		if (strcmp(lv, "LV") == 0)
			continue;
		sprintf(devname, "/dev/mapper/%s-%s", vg, lv);
		add_disk(devname, "LVM", "LV", NULL, 0, tmp_disk_list, 1, 1, 0, NULL, 0);
	}
	pclose(fp);
	return 0;
}
int
tl_common_scan_controller(char *start, char *vendor, char *product, char *grep, struct d_list *tmp_disk_list)
{
	/* Scan the list of Physical Disks available on this system */
	FILE *fp;
	char cmd[256];
	char buf[512];
	char devname[256];

	sprintf(cmd, "ls -1 %s/* 2> /dev/null | grep %s | grep -v p", start, grep);

	fp = popen(cmd, "r");

	if (!fp) {
		DEBUG_ERR_SERVER("Unable to execute program %s\n", cmd);
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {

		buf[strlen(buf) - 1] = 0;
		strcpy(devname, buf);

		if (is_ignore_dev(devname))
			continue;

		add_disk(devname, vendor, product, NULL, 0, tmp_disk_list, 1, 1, 0, NULL, 0);
	}
	pclose(fp);
	return 0;
}

int
tl_common_scan_raiddisk(struct d_list *tmp_disk_list)
{
	FILE *fp;
	char buf[512];
	char dev[256];
	char devname[512];
	int retval;
	char raidtype[32];

	fp = fopen("/proc/mdstat", "r");
	if (!fp)
	{
		DEBUG_WARN_SERVER("Cannot open mdstat for reading\n");
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		if (!strchr(buf, ':'))
			continue;

		if (strncmp(buf, "Personalities", strlen("Personalities")) == 0)
			continue;

		if (strncmp(buf, "unused devices", strlen("unused devices")) == 0)
			continue;

		retval = sscanf(buf, "%s :", dev);
		if (retval != 1) {
			DEBUG_INFO("Invalid buf %s\n", buf);
			continue;
		}

		if (!is_valid_mddev(dev, raidtype)) {
			DEBUG_INFO("Dev not a valid raiddev %s\n", dev);
			continue;
		}

		sprintf(devname, "/dev/%s", dev);

		if (is_ignore_dev(devname))
			continue;

		add_disk(devname, "RAID", raidtype, NULL, 0, tmp_disk_list, 1, 0, 1, NULL, 0);
	}

	fclose(fp);
	return 0;
}
#endif

#ifdef FREEBSD
static int 
get_disk_param(struct d_list *tmp_disk_list, char *devname, struct ata_params *param, int channel, int target)
{
	char product[32];

	DEBUG_INFO("get_disk_param: devname %s\n", devname);
	if (is_ignore_dev(devname))
		return 0;

	memset(product, 0, sizeof(product));
	memcpy(product, param->model, strlen((char *)param->model) < 16 ? strlen((char *)param->model) : 16);

	if (is_ignore_dev(devname))
		return 0;

	add_disk(devname, "ATA", product, (char *)param->serial, strlen((char *)param->serial), tmp_disk_list, 1, 0, 0, NULL, 0);
	return 0;
}

#if 0
static int 
get_disk_param(struct d_list *tmp_disk_list, int fd, int channel)
{
        struct ata_ioc_devices devices;
	int retval;

        devices.channel = channel;
	if (ioctl(fd, IOCATADEVICES, &devices) < 0) {
		return 0;
	}

	if (*devices.name[0]) {
		retval = check_disk_param(tmp_disk_list, devices.name[0], &devices.params[0]);
		if (retval != 0)
			return -1;
	}

	if (*devices.name[1]) {
		retval = check_disk_param(tmp_disk_list, devices.name[1], &devices.params[1]);
		if (retval != 0)
			return -1;
	}
	return 0;
}
#endif

static int 
get_channel_param(struct d_list *tmp_disk_list, int fd, int channel)
{
        struct ata_ioc_devices devices;
	int retval;
	char devname[512];

        devices.channel = channel;
	if (ioctl(fd, IOCATADEVICES, &devices) < 0) {
		return 0;
	}

	if (*devices.name[0] && (strncmp(devices.name[0], "ad", 2) == 0)) {
		sprintf(devname, "/dev/%s", devices.name[0]);
		retval = get_disk_param(tmp_disk_list, devname, &devices.params[0], channel, 0);
		if (retval != 0)
			return -1;
	}

	if (*devices.name[1] && (strncmp(devices.name[1], "ad", 2) == 0)) {
		sprintf(devname, "/dev/%s", devices.name[1]);
		retval = get_disk_param(tmp_disk_list, devname, &devices.params[1], channel, 1);
		if (retval != 0)
			return -1;
	}
	return 0;
}

int
get_ata_list(struct d_list *tmp_disk_list)
{
	int maxchannel;
	int channel;
	int fd;
	int retval;

	if ((fd = open("/dev/ata", O_RDWR)) < 0) {
                fprintf(stderr, "failed to open control device\n");
		return 0;
	}

	if (ioctl(fd, IOCATAGMAXCHANNEL, &maxchannel) < 0) {
		fprintf(stderr, "failed to get channel\n");
		close(fd);
		return 0;
	}

	for (channel = 0; channel < maxchannel; channel++) {
		retval = get_channel_param(tmp_disk_list, fd, channel);
		if (retval != 0) {
			close(fd);
			return -1;
		}
	}
	close(fd);
	return 0;
}
#endif

int
is_vmware_disk(char *vendor, char *product) {
	if (strncmp(vendor, "VMware", strlen("VMware")))
		return 0;
#if 0 
	if (strncmp(product, "VMware Virtual S", strlen("VMware Virtual S")))
		return 0;
#endif
	return 1;
}

struct physdisk *
tl_common_find_vmdisk(char *name)
{
	struct physdisk *tmp;

	TAILQ_FOREACH(tmp, &disk_list, q_entry) {
		struct physdevice *device = (struct physdevice *)(tmp);

		if (!is_vmware_disk(device->vendor, device->product))
			continue;

		if (strcmp(device->devname, name) == 0)
			return tmp;
	}
	return NULL;
}

#if 0
int
get_real_devname(char *devname)
{
	char cmd[512];
	FILE *cmdfp;
	int retval;

	sprintf(cmd, "%s | grep %s", SG_MAP_PROG, devname);

	cmdfp = popen(cmd, "r");
	if (!cmdfp)
		return -1;

	retval = fscanf(cmdfp, "%*s %s\n", devname);
	pclose(cmdfp);
	if (retval != 1)
		return -1;
	return 0;
}
#endif
int
tl_common_scan_physdisk(void)
{
	/* Scan the list of Physical Disks available on this system */
	FILE *fp;
	char buf[512];
	char devname[256];
	int retval, serial_len;
	struct d_list tmp_disk_list;
	struct physdisk *newdisk;
	struct physdisk *olddisk;
	char vendor[32], product[32], revision[32], serialnumber[64];
	struct physdevice device;

	DEBUG_INFO("Entered tl_common_scan_physdisk\n");

	ignore_dev_list_free();
	build_zdev_list();
	build_raid_list();
	build_swap_list();
	build_mount_list();
#ifdef LINUX
	build_pvs();
#endif
	TAILQ_INIT(&tmp_disk_list);
#ifdef FREEBSD
	tl_common_scan_controller("ar", "ATA", "ATA RAID", &tmp_disk_list);
	tl_common_scan_controller("aacd", "AAC", "AAC VOLUME", &tmp_disk_list);
	tl_common_scan_controller("amrd", "AMR", "AMR VOLUME", &tmp_disk_list);
	tl_common_scan_controller("mfid", "MFI", "MFI VOLUME", &tmp_disk_list);
	tl_common_scan_controller("idad", "IDA", "IDA VOLUME", &tmp_disk_list);
	tl_common_scan_controller("ipsd", "IPS", "IPS VOLUME", &tmp_disk_list);
	tl_common_scan_controller("mlxd", "MLX", "MLX VOLUME", &tmp_disk_list);
	tl_common_scan_controller("pst", "PST", "PST VOLUME", &tmp_disk_list);
	tl_common_scan_controller("twed", "TWE", "TWE VOLUME", &tmp_disk_list);
#else
	tl_common_scan_controller("/dev/cciss", "HP", "Smart Array", "c[0-9].*d[0-9].*", &tmp_disk_list);
	tl_common_scan_lvs(&tmp_disk_list);
#endif
	tl_common_scan_zvol(&tmp_disk_list);

	fp = popen(SG_SCAN_PROG, "r");

	if (!fp) {
		DEBUG_ERR_SERVER("Unable to execute program %s\n", SG_SCAN_PROG);
		disk_free_all(&tmp_disk_list);
		return -1;
	}

	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
#ifdef FREEBSD
		int devtype;
#endif
		int has_devid = 1;

		DEBUG_INFO("tl_common_scan_physdisk: Got buf %s\n", buf);
#ifdef FREEBSD
		retval = sscanf(buf, "%s %x %8c %16c %4c", devname, &devtype, vendor, product, revision);
		if (retval != 5) {
			DEBUG_INFO("tl_common_scan_physdisk devname %s devtype 0x%x vendor %.8s product %.16s revision %.4s\n", devname, devtype, vendor, product, revision);
			continue;
		}

		if (devtype != T_DIRECT)
			continue;
#else
		retval = sscanf(buf, "%*s %s %s %s %s", devname, vendor, product, revision);
		if (retval != 4)
			continue;
#endif

		if (is_ignore_dev(devname))
			continue;

		vendor[8] = 0;
		product[16] = 0;
		if (is_vmware_disk(vendor, product)) {
			/* Remove trailing ',' */
			memset(vendor, ' ', sizeof(vendor));
			strcpy(vendor, "VMware");
		}

		memset(&device, 0, sizeof(device));
		retval = do_device_identification(devname, &device);
		if (retval != 0)
			has_devid = 0;

		serial_len = sizeof(serialnumber);
		memset(serialnumber, 0, sizeof(serialnumber));
		retval = do_unit_serial_number(devname, serialnumber, &serial_len);
		if (retval != 0)
			serial_len = 0;

		if (is_ignore_dev(devname))
			continue;

		add_disk(devname, vendor, product, serialnumber, serial_len, &tmp_disk_list, !has_devid, 0, 0, &device, 0); 
	}
	pclose(fp);

#ifdef FREEBSD
	get_ata_list(&tmp_disk_list);
#endif

	tl_common_scan_raiddisk(&tmp_disk_list);

	mark_disks_offline();
	while ((newdisk = TAILQ_FIRST(&tmp_disk_list)))
	{
		struct physdevice *device = (struct physdevice *)(newdisk);
		TAILQ_REMOVE(&tmp_disk_list, newdisk, q_entry);
		olddisk = tl_common_find_physdisk2((struct physdisk *)newdisk);
		if (olddisk) {
			DEBUG_INFO("Ignoring duplicate path to disk at %s original disk at %s\n", newdisk->info.devname, olddisk->info.devname);
			TAILQ_REMOVE(&disk_list, olddisk, q_entry);
			free(olddisk);
			TAILQ_INSERT_TAIL(&disk_list, newdisk, q_entry); 
			continue;
		}

		if (is_vmware_disk(device->vendor, device->product)) {
			olddisk = tl_common_find_vmdisk(newdisk->info.devname);
			if (olddisk) {
				TAILQ_REMOVE(&disk_list, olddisk, q_entry);
				free(olddisk);
			}
			TAILQ_INSERT_TAIL(&disk_list, newdisk, q_entry); 
			continue;
		}

		if (newdisk->raiddisk) {
			olddisk = tl_common_find_raiddisk(newdisk->info.devname);
			if (olddisk) {
				DEBUG_INFO("Ignoring duplicate path to disk at %s original disk at %s\n", newdisk->info.devname, olddisk->info.devname);
				TAILQ_REMOVE(&disk_list, olddisk, q_entry);
				free(olddisk);
			}
			TAILQ_INSERT_TAIL(&disk_list, newdisk, q_entry); 
			continue;
		}
		if (newdisk->controllerdisk) {
			olddisk = tl_common_find_physdisk3((struct physdisk *)newdisk);
			if (olddisk) {
				DEBUG_INFO("tl_common_scan_controller: Ignoring duplicate path to disk at %s original disk at %s\n", newdisk->info.devname, olddisk->info.devname);
				TAILQ_REMOVE(&disk_list, olddisk, q_entry);
				free(olddisk);
			}
			TAILQ_INSERT_TAIL(&disk_list, newdisk, q_entry); 
			continue;
		}
		TAILQ_INSERT_TAIL(&disk_list, newdisk, q_entry); 
		continue;
	}
	prune_offline_disks();

	return 0;
}

struct physdisk *
tl_common_find_raiddisk(char *name)
{
	struct physdisk *tmp;

	TAILQ_FOREACH(tmp, &disk_list, q_entry) {
		struct physdevice *device = (struct physdevice *)(tmp);

		if (!tmp->raiddisk)
			continue;

		if (strcmp(device->devname, name) == 0)
			return tmp;
	}
	return NULL;
}

struct physdisk *
tl_common_find_disk(char *name)
{
	struct physdisk *tmp;

	TAILQ_FOREACH(tmp, &disk_list, q_entry) {
		struct physdevice *device = (struct physdevice *)(tmp);

		if (strcmp(device->devname, name) == 0)
			return tmp;
	}
	return NULL;
}

struct physdisk *
tl_common_find_physdisk2(struct physdisk *disk)
{
	struct physdevice *olddevice = (struct physdevice *)(disk);
	struct physdisk *tmp;

	TAILQ_FOREACH(tmp, &disk_list, q_entry) {
		struct physdevice *device = (struct physdevice *)(tmp);
		if (disk->partid != tmp->partid)
			continue;
#ifdef FREEBSD
		DEBUG_INFO("disk ident %s tmp ident %s\n", disk->ident, tmp->ident);
		DEBUG_INFO("disk serialnumber %.32s\n", olddevice->serialnumber);
		DEBUG_INFO("tmp serialnumber %.32s\n", device->serialnumber);
		if (tmp->controllerdisk && tmp->ident[0] && (strcmp(disk->ident, tmp->ident) == 0))
			return tmp;
#else
		if (tmp->controllerdisk && disk->controllerdisk && strcmp(tmp->info.devname, disk->info.devname) == 0)
			return tmp;
#endif
		if (device_equal(device, olddevice) == 0)
			return tmp;
	}

	return NULL;
}

#ifdef FREEBSD
static struct physdisk *
tl_common_find_physdisk3(struct physdisk *disk)
{
	struct physdisk *tmp;

	TAILQ_FOREACH(tmp, &disk_list, q_entry) {
		if (disk->partid != tmp->partid)
			continue;
		if (tmp->controllerdisk && disk->controllerdisk && tmp->ident[0] && (strcmp(disk->ident, tmp->ident) == 0))
			return tmp;

		if (tmp->controllerdisk && disk->controllerdisk && strcmp(tmp->info.devname, disk->info.devname) == 0)
			return tmp;
	}

	return NULL;
}
#else
static struct physdisk *
tl_common_find_physdisk3(struct physdisk *disk)
{
	struct physdisk *tmp;

	TAILQ_FOREACH(tmp, &disk_list, q_entry) {
		if (disk->partid != tmp->partid)
			continue;
		if (tmp->controllerdisk && disk->controllerdisk && strcmp(tmp->info.devname, disk->info.devname) == 0)
			return tmp;
	}

	return NULL;
}
#endif

struct physdisk *
find_physdisk(struct physdisk *disk)
{
	struct physdisk *cur_disk;

#if 0
	if (disk->raiddisk)
	{
		cur_disk = tl_common_find_raiddisk(disk->info.devname);
	}
	else
	{
		cur_disk = tl_common_find_physdisk2(disk);
	}
#endif
	cur_disk = tl_common_find_physdisk2(disk);
	return cur_disk;
}

int tl_common_sync_physdisk(struct physdisk *disk)
{
	struct physdisk *cur_disk;

	cur_disk = find_physdisk(disk);
	if (!cur_disk)
	{
		DEBUG_ERR_SERVER("Unable to find disk at devname %s rdisk %d\n", disk->info.devname, disk->raiddisk);
		return -1;
	}

	memcpy(&disk->info, &cur_disk->info, sizeof(struct physdevice));
	disk->size = cur_disk->size;
	disk->raiddisk = cur_disk->raiddisk;
	disk->controllerdisk = cur_disk->controllerdisk;
	disk->partid = cur_disk->partid;
	return 0;
}

void
dump_deviceid_diagnostics(FILE *fp, struct device_id *device_id)
{
	fprintf(fp, "<deviceid>\n");
	if (device_id->idflags & ID_FLAGS_T10)
	{
		fprintf(fp, "<t10id>\n");
		fwrite (device_id->t10_id.vendor, 1, 8, fp);
		fwrite (device_id->t10_id.product, 1, 16, fp);
		fwrite (device_id->t10_id.serialnumber, 1, sizeof(device_id->t10_id.serialnumber), fp);
		fprintf(fp, "\n</t10id>\n");
	}

	if (device_id->idflags & ID_FLAGS_NAA)
	{
		fprintf(fp, "<naa>\n");
		fwrite (device_id->naa_id.naa_id, 1, 16, fp);
		fprintf(fp, "\n</naa>\n");
	}

	if (device_id->idflags & ID_FLAGS_EUI)
	{
		fprintf(fp, "<eui>\n");
		fwrite (device_id->eui_id.eui_id, 1, 8, fp);
		fprintf(fp, "\n</eui>\n");
	}

	if (device_id->idflags & ID_FLAGS_VSPECIFIC)
	{
		fprintf(fp, "<vspecific>\n");
		fwrite (device_id->vspecific_id.vspecific_id, 1, 20, fp);
		fprintf(fp, "\n</vspecific>\n");
	}

	if (device_id->idflags & ID_FLAGS_UNKNOWN)
	{
		fprintf(fp, "<unknown>\n");
		fwrite (device_id->unknown_id.unknown_id, 1, 20, fp);
		fprintf(fp, "\n</unknown>\n");
	}
	fprintf(fp, "</deviceid>\n");
}


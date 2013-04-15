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

void
disk_free_all(struct d_list *head)
{
	struct physdisk *disk; 

	while ((disk = TAILQ_FIRST(head)))
	{
		TAILQ_REMOVE(head, disk, q_entry);
		free(disk);
	}
}

int 
parse_device(FILE *fp, struct physdevice *device)
{
	int retval;
	char buf[512];

	DEBUG_INFO("Entered parse_device\n");
	retval = fscanf (fp, "<type>%hhu</type>\n", &device->type);
	if (retval != 1)
	{
		DEBUG_ERR("type tag out of sync\n");
		return -1;
	}

	retval = fscanf (fp, "<online>%d</online>\n", &device->online);
	if (retval != 1)
	{
		DEBUG_ERR("online tag out of sync\n");
		return -1;
	}

	buf[0] = 0;
	fgets(buf, sizeof(buf), fp);
	if (strcmp(buf, "<ptags>\n"))
	{
		DEBUG_ERR("Invalid buf instead of ptags %s\n", buf);
		return -1;
	} 

	retval = fread(device->vendor, 1, 8, fp);
	if (retval != 8)
	{
		DEBUG_ERR("vendor tag out of sync\n");
		return -1;
	}

	retval = fread(device->product, 1, 16, fp);
	if (retval != 16)
	{
		DEBUG_ERR("product tag out of sync\n");
		return -1;
	}

	retval = fread(device->serialnumber, 1, sizeof(device->serialnumber), fp);
	if (retval != sizeof(device->serialnumber))
	{
		DEBUG_ERR("serialnumber tag fread failed retval is %d\n", retval);
		return -1;
	}

	/* Skip trailing tags */
	fgets(buf, sizeof(buf), fp);
	fgets(buf, sizeof(buf), fp);

	retval = fscanf (fp, "<devname>%[^<]</devname>\n", device->devname);
	if (retval != 1)
	{
		DEBUG_ERR("devname tag out of sync retval is %d\n", retval);
		return -1;
	}

	retval = fscanf (fp, "<multipath>%hhu</multipath>\n", &device->multipath);
	if (retval != 1)
	{
		DEBUG_ERR("multipath tag out of sync retval is %d\n", retval);
		return -1;
	}

	if (device->multipath)
	{
		retval = fscanf (fp, "<mdevname>%[^<]</mdevname>\n", device->mdevname);
		if (retval != 1)
		{
			DEBUG_ERR("mdevname tag out of sync retval is %d\n", retval);
			return -1;
		}
	}

	retval = fscanf (fp, "<idflags>%u</idflags>\n", &device->idflags);
	if (retval != 1)
	{
		DEBUG_ERR("idflags tag out of sync\n");
		return -1;
	}

	if (device->idflags & ID_FLAGS_T10)
	{
		if (fgets(buf, sizeof(buf), fp) == NULL)
		{
			DEBUG_ERR("t10id tag not present\n");
			return -1;
		}
		if (strcmp(buf, "<t10id>\n"))
		{
			DEBUG_ERR("t10id tag out of sync %s\n", buf);
			return -1;
		}

		retval = fread(&device->t10_id, 1, sizeof(struct device_t10_id), fp);
		if (retval != sizeof(struct device_t10_id))
		{
			DEBUG_ERR("t10_id fread failed retval is %d\n", retval);
			return -1;
		}

		/* Skip trailing tags */
		fgets(buf, sizeof(buf), fp);
		fgets(buf, sizeof(buf), fp);
		DEBUG_INFO("Got t10 trailing as %s\n", buf);

	}

	if (device->idflags & ID_FLAGS_NAA)
	{
		if (fgets(buf, sizeof(buf), fp) == NULL)
		{
			DEBUG_ERR("naaid tag not present\n");
			return -1;
		}
		if (strcmp(buf, "<naa>\n"))
		{
			DEBUG_ERR("naaid tag out of sync %s\n", buf);
			return -1;
		}

		retval = fread(&device->naa_id, 1, sizeof(struct device_naa_id), fp);
		if (retval != sizeof(struct device_naa_id))
		{
			DEBUG_ERR("naaid tag fread failed retval is %d\n", retval);
			return -1;
		}
		if (fgets(buf, sizeof(buf), fp) == NULL || fgets(buf, sizeof(buf), fp) == NULL)
		{
			DEBUG_ERR("/naaid not present\n");
			return -1;
		}
		if (strcmp(buf, "</naa>\n"))
		{
			DEBUG_ERR("/naaid tag out of sync\n");
			return -1;
		}
	}

	if (device->idflags & ID_FLAGS_EUI)
	{
		DEBUG_INFO("device idflags& ID_FLAGS_EUI");
		if (fgets(buf, sizeof(buf), fp) == NULL)
		{
			DEBUG_ERR("euiid tag not present\n");
			return -1;
		}
		if (strcmp(buf, "<eui>\n"))
		{
			DEBUG_ERR("euiid tag out of sync\n");
			return -1;
		}

		retval = fread(&device->eui_id, 1, sizeof(struct device_eui_id), fp);
		if (retval != sizeof(struct device_eui_id))
		{
			DEBUG_ERR("euiid tag fread failed\n");
			return -1;
		}
		if (fgets(buf, sizeof(buf), fp) == NULL || fgets(buf, sizeof(buf), fp) == NULL)
		{
			DEBUG_ERR("/euiid tag not present\n");
			return -1;
		}
		if (strcmp(buf, "</eui>\n"))
		{
			DEBUG_ERR("/euiid tag out of sync\n");
			return -1;
		}

	}

	if (device->idflags & ID_FLAGS_VSPECIFIC)
	{
		DEBUG_INFO("device idflags& ID_FLAGS_VSPECIFIC");
		if (fgets(buf, sizeof(buf), fp) == NULL)
		{
			DEBUG_ERR("vspecific tag not present\n");
			return -1;
		}
		if (strcmp(buf, "<vspecific>\n"))
		{
			DEBUG_ERR("vspecific tag out of sync %s\n", buf);
			return -1;
		}

		retval = fread(&device->vspecific_id, 1, sizeof(struct device_vspecific_id), fp);
		if (retval != sizeof(struct device_vspecific_id))
		{
			DEBUG_ERR("vspecific tag fread failed retval is %d\n", retval);
			return -1;
		}

		if (fgets(buf, sizeof(buf), fp) == NULL || fgets(buf, sizeof(buf), fp) == NULL)
		{
			DEBUG_ERR("/vspecific not present\n");
			return -1;
		}
		if (strcmp(buf, "</vspecific>\n"))
		{
			DEBUG_ERR("/vspecific tag out of sync\n");
			return -1;
		}
	}

	if (device->idflags & ID_FLAGS_UNKNOWN)
	{
		DEBUG_INFO("device idflags& ID_FLAGS_UNKNOWN");
		if (fgets(buf, sizeof(buf), fp) == NULL)
		{
			DEBUG_ERR("unknownid tag not present\n");
			return -1;
		}
		if (strcmp(buf, "<unknown>\n"))
		{
			DEBUG_ERR("unknownid tag out of sync %s\n", buf);
			return -1;
		}

		retval = fread(&device->unknown_id, 1, sizeof(struct device_unknown_id), fp);
		if (retval != sizeof(struct device_unknown_id))
		{
			DEBUG_ERR("unknownid tag fread failed retval is %d\n", retval);
			return -1;
		}

		if (fgets(buf, sizeof(buf), fp) == NULL || fgets(buf, sizeof(buf), fp) == NULL)
		{
			DEBUG_ERR("/unknownid not present\n");
			return -1;
		}
		if (strcmp(buf, "</unknown>\n"))
		{
			DEBUG_ERR("/unknownid tag out of sync\n");
			return -1;
		}
	}

	return 0;
}

static int
parse_disk(FILE *fp, struct d_list *dlist)
{
	int retval;
	struct physdisk *disk;
	char buf[512];

	DEBUG_INFO("Entered parse_disk\n");
	disk = malloc(sizeof(struct physdisk));

	if (!disk)
	{
		DEBUG_ERR("Unable to allocate for new physdisk\n");
		return -1;
	}

	memset(disk, 0, sizeof(struct physdisk));
	if (fscanf(fp, "bid: %u\n", &disk->bid) != 1)
	{
		DEBUG_ERR("Unable to get bid of disk\n");
		free(disk);
		return -1;
	}

	if (fscanf(fp, "partid: %d\n", &disk->partid) != 1) {
		DEBUG_ERR("Unable to get bid of disk\n");
		free(disk);
		return -1;
	}

	if (fscanf(fp, "size: %"PRIu64"\n", &disk->size) != 1)
	{
		DEBUG_ERR("Unable to get size of disk\n");
		free(disk);
		return -1;
	}

	if (fscanf(fp, "used: %"PRIu64"\n", &disk->used) != 1) {
		DEBUG_ERR("Unable to get used size of disk\n");
		free(disk);
		return -1;
	}

	if (fscanf(fp, "reserved: %"PRIu64"\n", &disk->reserved) != 1) {
		DEBUG_ERR("Unable to get reserved size of disk\n");
		free(disk);
		return -1;
	}

	if (fscanf(fp, "raiddisk: %hhd\n", &disk->raiddisk) != 1) {
		DEBUG_ERR("Unable to get raiddisk flag of disk\n");
		free(disk);
		return -1;
	}

	if (fscanf(fp, "unmap: %hhd\n", &disk->unmap) != 1) {
		DEBUG_ERR("Unable to get unmap status flag of disk\n");
		free(disk);
		return -1;
	}

	if (fscanf(fp, "write_cache: %hhd\n", &disk->write_cache) != 1) {
		DEBUG_ERR("Unable to get write cache status flag of disk\n");
		free(disk);
		return -1;
	}

	if (fscanf(fp, "group_name: %s\n", disk->group_name) != 1) {
		DEBUG_ERR("Unable to get name");
		free(disk);
		return -1;
	}

	retval = parse_device(fp, (struct physdevice *)disk);
	DEBUG_INFO("parse_disk: done with parse_device retval is %d\n", retval);
	if (retval != 0)
	{
		DEBUG_ERR("parse_device failed\n");
		goto err;
	}

	buf[0] = 0;	
	fgets(buf, sizeof(buf), fp);
	DEBUG_INFO("parse_disk: buf is %s\n", buf);
	if (strncmp(buf, "</disk>", strlen("</disk>")) != 0)
	{
		DEBUG_ERR("Invalid buf %s\n", buf);
		goto err;
	}

	TAILQ_INSERT_TAIL(dlist, disk, q_entry); 
	return 0;
err:
	free(disk);
	return -1;
}

int
tl_common_parse_physdisk(FILE *fp, struct d_list *dhead)
{
	char buf[512];
	int retval;

	DEBUG_INFO("Entered tl_common_parse_physdisk\n");

	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
		DEBUG_INFO("tl_common_parse_physdisk: buf is %s\n", buf);
		if (strncmp(buf, "<disk>", strlen("<disk>")))
		{
			continue;
		}
		retval = parse_disk(fp, dhead);
		if (retval != 0)
		{
			DEBUG_ERR("parse_disk returned err\n");
			goto err;
		}
	}

	return 0;
err:
	disk_free_all(dhead);
	return -1;
}

int
device_match_serialnumber(struct physdevice *device, struct device_id *device_id)
{
	if (!device->serial_len)
	{
		return 0;
	}

	if (memcmp(device->t10_id.vendor, device_id->t10_id.vendor, 8))
	{
		return 0;
	}

	if (memcmp(device->t10_id.product, device_id->t10_id.product, 16))
	{
		return 0;
	}

	if (memcmp(device->t10_id.serialnumber, device_id->t10_id.serialnumber, device->serial_len))
	{
		return 0;
	}
	return 1;
}

int
device_ids_match(struct physdevice *device, struct device_id *device_id)
{
	int match = 0;

	DEBUG_INFO("device_ids_match: device's idflags %u passed device_id flags %u\n", device->idflags, device_id->idflags);
	if (device_id->idflags & ID_FLAGS_T10 && device->idflags & ID_FLAGS_T10)
	{
		DEBUG_INFO("device_id->idflags UNIT_IDENTIFIER_T10_VENDOR_ID\n");
		DEBUG_INFO("device_id vendor %.8s device vendor %.8s\n", device_id->t10_id.vendor, device->t10_id.vendor);
		DEBUG_INFO("device_id product %.16s device product %.16s\n", device_id->t10_id.product, device->t10_id.product);
		DEBUG_INFO("device_id serialnumber %.16s device serialnumber %.16s\n", device_id->t10_id.serialnumber, device->t10_id.serialnumber);
		if (memcmp(&device->t10_id, &device_id->t10_id, sizeof(device_id->t10_id)) == 0)
		{
			return 1;
		}
		match = device_match_serialnumber(device, device_id);
		if (match)
		{
			return 1;
		}
	}
	if (device_id->idflags & ID_FLAGS_NAA && device->idflags & ID_FLAGS_NAA)
	{
		DEBUG_INFO("device_id->idflags UNIT_IDENTIFIER_NAA\n");
		DEBUG_INFO("device naa_id %.16s device_id naa_id %.16s\n", device->naa_id.naa_id, device_id->naa_id.naa_id);
		if (memcmp(&device->naa_id.naa_id, &device_id->naa_id.naa_id, sizeof(device_id->naa_id.naa_id)) == 0)
		{
			return 1;
		}
	}
	if (device_id->idflags & ID_FLAGS_EUI && device->idflags & ID_FLAGS_EUI)
	{
		DEBUG_INFO("device_id->idflags UNIT_IDENTIFIER_EUI64\n");
		if (memcmp(&device->eui_id.eui_id, &device_id->eui_id.eui_id, sizeof(device_id->eui_id.eui_id)) == 0)
		{
			return 1;
		}
	}
	if (device_id->idflags & ID_FLAGS_UNKNOWN && device->idflags & ID_FLAGS_UNKNOWN)
	{
		DEBUG_INFO("device_id->idflags UNIT_IDENTIFIER_UNKNOWN\n");
		DEBUG_INFO("device unknown_id %.16s device_id unknown_id %.16s\n", device->unknown_id.unknown_id, device_id->unknown_id.unknown_id);
		if (memcmp(&device->unknown_id.unknown_id, &device_id->unknown_id.unknown_id, sizeof(device_id->unknown_id.unknown_id)) == 0)
		{
			return 1;
		}
	}
	if (device_id->idflags & ID_FLAGS_VSPECIFIC && device->idflags & ID_FLAGS_VSPECIFIC)
	{
		DEBUG_INFO("device_id->idflags UNIT_IDENTIFIER_VENDOR_SPECIFIC\n");
		DEBUG_INFO("device vspecific_id %.16s device_id vspecific_id %.16s\n", device->vspecific_id.vspecific_id, device_id->vspecific_id.vspecific_id);
		if (memcmp(&device->vspecific_id.vspecific_id, &device_id->vspecific_id.vspecific_id, sizeof(device_id->vspecific_id.vspecific_id)) == 0)
		{
			return 1;
		}
	}
	if (device_id->avoltag_valid)
	{
		if (memcmp(device->serialnumber, device_id->serialnumber+4, device->serial_len) == 0)
		{
			return 1;
		}
	}

	return 0;
}

void
device_get_alias(char *devname, char *alias)
{
	if (strncmp(devname, "/dev/mapper/", strlen("/dev/mapper/")) == 0)
	{
		strcpy(alias, devname+strlen("/dev/mapper/"));
		return;
	}
	else if (strncmp(devname, "/dev/", strlen("/dev/")) == 0)
	{
		strcpy(alias, devname+strlen("/dev/"));
		return;
	}
	strcpy(alias, devname);
	return;
}

struct physdisk *
disk_configured(struct physdisk *disk, struct d_list *dlist)
{
	struct physdevice *olddevice = &disk->info;
	struct physdisk *tmp;

	TAILQ_FOREACH(tmp, dlist, q_entry) {
		struct physdevice *device = (struct physdevice *)(tmp);
		if (tmp->partid != disk->partid)
			continue;

		if (device_equal(device, olddevice) == 0) {
			return tmp;
		}

#if 0
		if (disk->raiddisk)
		{
			if (tmp->raiddisk && strcmp(device->devname, olddevice->devname) == 0)
			{
				return tmp;
			}
		}
		else
		{
			if (device_equal(device, olddevice) == 0)
			{
				return tmp;
			}
		}
#endif
	}

	return NULL;
}

int
device_equal(struct physdevice *device, struct physdevice *olddevice)
{
	DEBUG_INFO("Entered device_equal device idflags 0x%x olddevice idflags 0x%x\n", device->idflags, olddevice->idflags);
	if ((device->idflags & ID_FLAGS_T10) && (olddevice->idflags & ID_FLAGS_T10))
	{
		DEBUG_INFO("device vendor %.8s product %.16s serialnumber %.32s\n", device->t10_id.vendor, device->t10_id.product, device->t10_id.serialnumber);
		DEBUG_INFO("olddevice vendor %.8s product %.16s serialnumber %.32s\n", olddevice->t10_id.vendor, olddevice->t10_id.product, olddevice->t10_id.serialnumber);
		if (memcmp(&device->t10_id, &olddevice->t10_id, sizeof(olddevice->t10_id)) == 0)
		{
			return 0;
		}
		if (memcmp(device->t10_id.vendor, olddevice->t10_id.vendor, sizeof(device->t10_id.vendor)))
		{
			DEBUG_INFO("device and old device vendors not equal");
		}
		if (memcmp(device->t10_id.product, olddevice->t10_id.product, sizeof(device->t10_id.product)))
		{
			DEBUG_INFO("device and old device products not equal");
		}
		if (memcmp(device->t10_id.serialnumber, olddevice->t10_id.serialnumber, sizeof(device->t10_id.serialnumber)))
		{
			DEBUG_INFO("device and old device serialnumbers not equal");
		}
		return -1;
	}
	else if ((device->idflags & ID_FLAGS_NAA) && (olddevice->idflags & ID_FLAGS_NAA))
	{
		DEBUG_INFO("device naa_id %.16s olddevice naa_id %.16s\n", device->naa_id.naa_id, olddevice->naa_id.naa_id);
		if (memcmp(&device->naa_id.naa_id, &olddevice->naa_id.naa_id, sizeof(olddevice->naa_id.naa_id)) == 0)
		{
			return 0;
		}
		return -1;
	}
	else if ((device->idflags & ID_FLAGS_EUI) && (olddevice->idflags & ID_FLAGS_EUI))
	{
		if (memcmp(&device->eui_id.eui_id, &olddevice->eui_id.eui_id, sizeof(olddevice->eui_id.eui_id)) == 0)
		{
			DEBUG_INFO("device eui_id %.8s olddevice eui_id %.8s\n", device->eui_id.eui_id, olddevice->eui_id.eui_id);
			return 0;
		}
		return -1;
	}

	else if ((device->idflags & ID_FLAGS_VSPECIFIC) && (olddevice->idflags & ID_FLAGS_VSPECIFIC))
	{
		DEBUG_INFO("device vspecific_id %.16s olddevice vspecific_id %.16s\n", device->vspecific_id.vspecific_id, olddevice->vspecific_id.vspecific_id);
		if (memcmp(&device->vspecific_id.vspecific_id, &olddevice->vspecific_id.vspecific_id, sizeof(olddevice->vspecific_id.vspecific_id)) == 0)
		{
			return 0;
		}
		return -1;
	}
	else if ((device->idflags & ID_FLAGS_UNKNOWN) && (olddevice->idflags & ID_FLAGS_UNKNOWN))
	{
		DEBUG_INFO("device unknown_id %.16s olddevice unknown_id %.16s\n", device->unknown_id.unknown_id, olddevice->unknown_id.unknown_id);
		if (memcmp(&device->unknown_id.unknown_id, &olddevice->unknown_id.unknown_id, sizeof(olddevice->unknown_id.unknown_id)) == 0)
		{
			return 0;
		}
		return -1;
	}
	return -1;
}

void
group_list_free(struct group_list *group_list)
{
	struct group_info *info;

	while ((info = TAILQ_FIRST(group_list)))
	{
		TAILQ_REMOVE(group_list, info, q_entry);
		free(info);
	}
}

int
tl_common_parse_group(FILE *fp, struct group_list *group_list)
{
	struct group_info *info;
	char buf[512];
	int retval;

	while (fgets(buf, sizeof(buf), fp) != NULL)
	{
		if (strncmp(buf, "<group>", strlen("<group>")) != 0)
		{
			continue;
		}

		info = malloc(sizeof(struct group_info));
		if (!info)
		{
			return -1;
		}
		memset(info, 0, sizeof(struct group_info));

		retval = fscanf(fp, "group_id: %u\n", &info->group_id);
		if (retval != 1)
		{
			DEBUG_INFO("Unable to get group id");
			free(info);
			return -1;
		}

		retval = fscanf(fp, "name: %s\n", info->name);
		if (retval != 1)
		{
			DEBUG_INFO("Unable to get name");
			free(info);
			return -1;
		}

		retval = fscanf(fp, "worm: %d\n", &info->worm);
		if (retval != 1) {
			DEBUG_INFO("Unable to get worm property");
			free(info);
			return -1;
		}

		retval = fscanf(fp, "disks: %d\n", &info->disks);
		if (retval != 1) {
			DEBUG_INFO("Unable to get disks property");
			free(info);
			return -1;
		}

		buf[0] = 0;
		fgets(buf, sizeof(buf), fp);
		if (strncmp(buf, "</group>", strlen("</group>")) != 0) {
			free(info);
			return -1;
		}

		TAILQ_INSERT_TAIL(group_list, info, q_entry); 
	}

	return 0;
}
#ifdef FREEBSD
int send_scsi_request(struct scsi_request *request)
{
	struct ccb_scsiio csio;
	uint32_t flags = 0;
	uint8_t *data = NULL;
	uint32_t data_len = 0;
	struct cam_device *device;
	int error;

	device = cam_open_device(request->device, O_RDWR);
	if (!device)
	{
		DEBUG_WARN("Cannot open device %s for flags %d err %d %s\n", request->device, request->fdflags, errno, strerror(errno));
		return -1;
	}

	if (request->datain_len)
	{
		flags = CAM_DIR_IN | CAM_DEV_QFRZDIS;
		data = request->datain;
		data_len = request->datain_len;
	}
	else if (request->dataout_len)
	{
		flags = CAM_DIR_OUT | CAM_DEV_QFRZDIS;
		data = request->dataout;
		data_len = request->dataout_len;
	}
	else
	{
		flags = CAM_DIR_NONE | CAM_DEV_QFRZDIS;
	}
	bzero(&csio, sizeof(csio));
	memcpy(csio.cdb_io.cdb_bytes, request->cdb, request->cdb_len);
	cam_fill_csio(&csio, 1, NULL, flags, MSG_SIMPLE_Q_TAG, data, data_len, SSD_FULL_SIZE, request->cdb_len, request->timeout * 1000);

	error = cam_send_ccb(device, (union ccb *)&csio);
	if (error < 0 || (((csio.ccb_h.status & CAM_STATUS_MASK) != CAM_REQ_CMP) && ((csio.ccb_h.status & CAM_STATUS_MASK) != CAM_SCSI_STATUS_ERROR)))
	{
		DEBUG_INFO("error %d status %x\n", error, csio.ccb_h.status & CAM_STATUS_MASK);
		cam_close_device(device);
		return -1;
	}
 
	if (request->sense_len)
	{
		int min_len = request->sense_len < SSD_FULL_SIZE ? request->sense_len : SSD_FULL_SIZE;
		memcpy(request->sense, &csio.sense_data, min_len);
	}

	request->scsi_status = csio.scsi_status;
	request->resid = csio.resid;

	cam_close_device(device);
	return 0;
}
#else
int send_scsi_request(struct scsi_request *request)
{
	struct sg_io_hdr io_hdr;
	int err;
	int fd;

	fd = open(request->device, request->fdflags);
	if (fd < 0)
	{
		return -1;
	}

	memset(&io_hdr, 0, sizeof(struct sg_io_hdr));

	io_hdr.interface_id = 'S';
	io_hdr.cmdp = request->cdb;
	io_hdr.cmd_len = request->cdb_len;
	if (request->datain_len)
	{
		io_hdr.dxfer_direction = SG_DXFER_FROM_DEV;
		io_hdr.dxferp = request->datain;
		io_hdr.dxfer_len = request->datain_len;
	}
	else if (request->dataout_len)
	{
		io_hdr.dxfer_direction = SG_DXFER_TO_DEV;
		io_hdr.dxferp = request->dataout;
		io_hdr.dxfer_len = request->dataout_len;
	}
	else
	{
		io_hdr.dxfer_direction = SG_DXFER_NONE;
	}
	io_hdr.sbp = request->sense;
	io_hdr.mx_sb_len = request->sense_len;
	io_hdr.timeout = request->timeout * 1000;

	err = ioctl(fd, SG_IO, &io_hdr);
	close(fd);
	if (err < 0) {
		return -1;
	}
	request->scsi_status = io_hdr.status;
	request->resid = io_hdr.resid;
	request->sense_len = io_hdr.sb_len_wr;
	return 0;
}
#endif

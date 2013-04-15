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
#include <vdevice.h>

extern struct vdevice *device_list[];

int
dump_driveconf(FILE *fp, struct tdriveconf *driveconf)
{
	struct vdevice *vdevice = (struct vdevice *)driveconf;

	fprintf(fp, "<drive>\n");
	fprintf(fp, "name: %s\n", vdevice->name);
	fprintf(fp, "serialnumber: %s\n", vdevice->serialnumber);
	fprintf(fp, "type: %d\n", driveconf->type);
	fprintf(fp, "tl_id: %d\n", vdevice->tl_id);
	fprintf(fp, "target_id: %u\n", vdevice->target_id);
	if (strlen(driveconf->tape_label))
	{
		fprintf(fp, "tape_label: %s\n", driveconf->tape_label);
	}
	else
	{
		fprintf(fp, "tape_label: none\n");
	}
	fprintf(fp, "</drive>\n");
	return 0;
}

int
dump_vtlconf(FILE *fp, struct vtlconf *vtlconf, int dumpdrivelist)
{
	struct vdevice *vdevice = (struct vdevice *)vtlconf;
	struct tdriveconf *dconf;

	fprintf(fp, "<vtl>\n");
	fprintf(fp, "name: %s\n", vdevice->name);
	fprintf(fp, "serialnumber: %s\n", vdevice->serialnumber);
	fprintf(fp, "type: %d\n", vtlconf->type);
	fprintf(fp, "slots: %d\n", vtlconf->slots);
	fprintf(fp, "ieports: %d\n", vtlconf->ieports);
	fprintf(fp, "drives: %d\n", vtlconf->drives);
	fprintf(fp, "tl_id: %d\n", vdevice->tl_id);

	if (!dumpdrivelist)
	{
		fprintf(fp, "</vtl>\n");
		return 0;
	}

	TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) {
		dump_driveconf(fp, dconf);
	} 
	fprintf(fp, "</vtl>\n");

	return 0;
}


int
dump_volume(FILE *fp, struct vcartridge *vinfo)
{
	fprintf(fp, "<vcartridge>\n");
	fprintf(fp, "tl_id: %d\n", vinfo->tl_id);
	fprintf(fp, "tape_id: %u\n", vinfo->tape_id);
	fprintf(fp, "worm: %d\n", vinfo->worm);
	fprintf(fp, "type: %d\n", vinfo->type);
	fprintf(fp, "group_name: %s\n", vinfo->group_name);
	fprintf(fp, "label: %s\n", vinfo->label);
	fprintf(fp, "size: %llu\n", (unsigned long long)vinfo->size);
	fprintf(fp, "used: %llu\n", (unsigned long long)vinfo->used);
	fprintf(fp, "vstatus: %u\n", vinfo->vstatus);
	fprintf(fp, "loaderror: %d\n", vinfo->loaderror);
	fprintf(fp, "</vcartridge>\n");
	return 0;
}

int
dump_vdevice(FILE *fp, struct vdevice *vdevice, int dumpdrivelist)
{
	struct vcartridge *volume;

	fprintf(fp, "<vdevice>\n");
	fprintf(fp, "type: %d\n", vdevice->type);

	if (vdevice->type == T_CHANGER)
	{
		struct vtlconf *vtlconf = (struct vtlconf *)(vdevice);
		dump_vtlconf(fp, vtlconf, dumpdrivelist);
	}
	else
	{
		struct tdriveconf *driveconf = (struct tdriveconf *)(vdevice);
		dump_driveconf(fp, driveconf);
	}

	if (!dumpdrivelist)
		goto skip;

	TAILQ_FOREACH(volume, &vdevice->vol_list, q_entry) {
		/* Updates the used percentage of the volume */
		if (!volume->loaderror)
		{
			tl_ioctl(TLTARGIOCGETVCARTRIDGEINFO, volume);
		}
		dump_volume(fp, volume);
	}

skip:
	fprintf(fp, "</vdevice>\n");
	return 0;
}

struct tdriveconf *
tdriveconf_new(int tl_id, int target_id, char *name, char *serialnumber)
{
	struct tdriveconf *tdriveconf;
	struct vdevice *vdevice;

	tdriveconf = alloc_buffer(sizeof(*tdriveconf));
	if (!tdriveconf) {
		return NULL;
	}

	vdevice = &tdriveconf->vdevice;
	vdevice->type = T_SEQUENTIAL;
	vdevice->tl_id = tl_id;
	vdevice->target_id = target_id;
	vdevice->iscsi_tid = -1;
	vdevice->vhba_id = -1;
	strcpy(vdevice->name, name);
	strcpy(vdevice->serialnumber, serialnumber);

	TAILQ_INIT(&vdevice->vol_list);
	return tdriveconf;
}

struct vtlconf *
vtlconf_new(int tl_id, char *name, char *serialnumber)
{
	struct vtlconf *vtlconf;
	struct vdevice *vdevice;

	vtlconf = alloc_buffer(sizeof(*vtlconf));
	if (!vtlconf)
	{
		return NULL;
	}

	vdevice = &vtlconf->vdevice;
	vdevice->tl_id = tl_id;
	vdevice->type = T_CHANGER;
	vdevice->iscsi_tid = -1;
	vdevice->vhba_id = -1;

	strcpy(vdevice->name, name);
	strcpy(vdevice->serialnumber, serialnumber);

	TAILQ_INIT(&vdevice->vol_list);
	TAILQ_INIT(&vtlconf->drive_list);

	return vtlconf;
}	

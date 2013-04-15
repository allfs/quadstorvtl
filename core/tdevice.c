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

#include "tdevice.h"
#include "vdevdefs.h"
#include "mchanger.h"

extern struct tdevice *tdevices[];
int 
tdevice_init(struct tdevice *tdevice, int type, int tl_id, int target_id, char *name, void (*proc_cmd) (void *, void *), char *thr_name)
{
	tdevice->type = type;
	tdevice->tl_id = tl_id;
	tdevice->target_id = target_id;
	strcpy(tdevice->name, name);

	tdevice->devq = devq_init(tl_id, target_id, tdevice, thr_name, proc_cmd);
	if (unlikely(!tdevice->devq))
		return -1;

	SLIST_INIT(&tdevice->istate_list);
	SLIST_INIT(&tdevice->reservation.registration_list);
	tdevice->reservation_lock = sx_alloc("tdevice reservation lock");

	return 0;
}

void
tdevice_exit(struct tdevice *tdevice)
{
	if (tdevice->devq) {
		devq_exit(tdevice->devq);
		tdevice->devq = NULL;
	}
	device_free_all_initiators(&tdevice->istate_list);
	persistent_reservation_clear(&tdevice->reservation.registration_list);
	sx_free(tdevice->reservation_lock);
}

void
tdevice_reset(struct tdevice *tdevice, uint64_t i_prt, uint64_t t_prt, uint8_t init_int)
{
	if (tdevice->type == T_SEQUENTIAL)
		tdrive_reset((struct tdrive *)tdevice, i_prt, t_prt, init_int);
	else
		mchanger_reset((struct mchanger *)tdevice, i_prt, t_prt, init_int);
}

static int
vdevice_new_tdrive(struct vdeviceinfo *deviceinfo)
{
	struct tdevice *tdevice;
	struct tdrive *tdrive;

	if (deviceinfo->tl_id >= TL_MAX_DEVICES)
		return -1;

	if (deviceinfo->target_id > 0) {
		tdevice = tdevices[deviceinfo->tl_id];
		if (!tdevice) {
			debug_warn("Missing mchanger at %d\n", deviceinfo->tl_id);
			return -1;
		}

		tdrive = mchanger_add_tdrive((struct mchanger *)tdevice, deviceinfo);
	}
	else {
		tdevice = tdevices[deviceinfo->tl_id];
		if (tdevice)
			return -1;

		tdrive = tdrive_new(NULL, deviceinfo);
		tdevices[deviceinfo->tl_id] = (struct tdevice *)tdevice;
	}

	if (unlikely(!tdrive))
		return -1;

	tdevice = (struct tdevice *)tdrive;
	cbs_new_device(tdevice, 0);
	deviceinfo->iscsi_tid = tdevice->iscsi_tid;
	deviceinfo->vhba_id = tdevice->vhba_id;
	return 0;
}

static int
vdevice_new_mchanger(struct vdeviceinfo *deviceinfo)
{
	struct mchanger *mchanger;
	struct tdevice *tdevice;

	if (deviceinfo->tl_id >= TL_MAX_DEVICES)
		return -1;

	tdevice = tdevices[deviceinfo->tl_id];
	if (tdevice)
		return -1;

	mchanger = mchanger_new(deviceinfo);
	if (!mchanger)
		return -1;

	tdevice = (struct tdevice *)mchanger;
	tdevices[deviceinfo->tl_id] = (struct tdevice *)mchanger;

	cbs_new_device(tdevice, 0);
	deviceinfo->iscsi_tid = tdevice->iscsi_tid;
	deviceinfo->vhba_id = tdevice->vhba_id;
	return 0;
}

int
vdevice_new(struct vdeviceinfo *deviceinfo)
{
	if (deviceinfo->type == T_SEQUENTIAL)
		return vdevice_new_tdrive(deviceinfo);
	else
		return vdevice_new_mchanger(deviceinfo);

	return 0;
}

int
vcartridge_new(struct vcartridge *vcartridge)
{
	struct tdevice *tdevice;
	int retval;

	if (vcartridge->tl_id >= TL_MAX_DEVICES)
		return -1;

	tdevice = tdevices[vcartridge->tl_id];
	if (!tdevice)
		return -1;

	if (tdevice->type == T_SEQUENTIAL)
		retval = tdrive_new_vcartridge((struct tdrive *)tdevice, vcartridge);
	else
		retval = mchanger_new_vcartridge((struct mchanger *)tdevice, vcartridge);
	return retval;
}

int
vcartridge_load(struct vcartridge *vcartridge)
{
	struct tdevice *tdevice;
	int retval;

	if (vcartridge->tl_id >= TL_MAX_DEVICES)
		return -1;

	tdevice = tdevices[vcartridge->tl_id];
	if (!tdevice)
		return -1;

	if (tdevice->type == T_SEQUENTIAL)
		retval = tdrive_load_vcartridge((struct tdrive *)tdevice, vcartridge);
	else
		retval = mchanger_load_vcartridge((struct mchanger *)tdevice, vcartridge);
	return retval;
}

int
vcartridge_delete(struct vcartridge *vcartridge)
{
	struct tdevice *tdevice;
	int retval;

	if (vcartridge->tl_id >= TL_MAX_DEVICES)
		return -1;

	tdevice = tdevices[vcartridge->tl_id];
	if (!tdevice)
		return -1;

	if (tdevice->type == T_SEQUENTIAL)
		retval = tdrive_delete_vcartridge((struct tdrive *)tdevice, vcartridge);
	else
		retval = mchanger_delete_vcartridge((struct mchanger *)tdevice, vcartridge);
	return retval;
}

int
vcartridge_info(struct vcartridge *vcartridge)
{
	struct tdevice *tdevice;
	int retval;

	if (vcartridge->tl_id >= TL_MAX_DEVICES)
		return -1;

	tdevice = tdevices[vcartridge->tl_id];
	if (!tdevice)
		return -1;

	if (tdevice->type == T_SEQUENTIAL)
		retval = tdrive_vcartridge_info((struct tdrive *)tdevice, vcartridge);
	else
		retval = mchanger_vcartridge_info((struct mchanger *)tdevice, vcartridge);
	return retval;
}

int 
tdevice_delete(uint32_t tl_id, int free_alloc)
{
	struct tdevice *tdevice;

	if (tl_id >= TL_MAX_DEVICES)
		return -1;

	tdevice = tdevices[tl_id];
	if (!tdevice)
		return -1;

	if (tdevice->type == T_SEQUENTIAL)
		tdrive_free((struct tdrive *)tdevice, free_alloc);
	else
		mchanger_free((struct mchanger *)tdevice, free_alloc);
	tdevices[tl_id] = NULL;
	target_clear_fc_rules(tl_id);
	return 0;
}

int
vdevice_delete(struct vdeviceinfo *deviceinfo)
{
	return tdevice_delete(deviceinfo->tl_id, deviceinfo->free_alloc);
}

int
vdevice_modify(struct vdeviceinfo *deviceinfo)
{
	return 0;
}

int
vdevice_info(struct vdeviceinfo *deviceinfo)
{
	return 0;
}

void
tdevice_insert_ccb(struct qsio_hdr *ccb_h)
{
	struct tdevice *tdevice = ccb_h->tdevice;

	devq_insert_ccb(tdevice->devq, ccb_h);
}

int
tdevice_check_cmd(struct tdevice *tdevice, uint8_t op)
{
	if (tdevice->type == T_SEQUENTIAL)
		return tdrive_check_cmd(tdevice, op);
	else
		return mchanger_check_cmd(tdevice, op);
}

void
tdevice_cbs_disable(struct tdevice *tdevice)
{
	if (tdevice->type == T_SEQUENTIAL)
		tdrive_cbs_disable((struct tdrive *)tdevice);
	else
		mchanger_cbs_disable((struct mchanger *)tdevice);
}

void
tdevice_cbs_remove(struct tdevice *tdevice)
{
	if (tdevice->type == T_SEQUENTIAL)
		tdrive_cbs_remove((struct tdrive *)tdevice);
	else
		mchanger_cbs_remove((struct mchanger *)tdevice);
}

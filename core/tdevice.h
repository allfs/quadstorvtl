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

#ifndef QUADSTOR_TDEVICE_H_
#define QUADSTOR_TDEVICE_H_
#include "coredefs.h"
#include "reservation.h"
#include "devq.h"

enum {
	DEVICE_ATTACHED,
};

struct initiator_state {
	mtx_t *istate_lock;
	struct ctio_list queue_list;
	uint32_t ordered;
	uint32_t head;
	uint32_t queued;
	uint32_t pending;
	uint64_t i_prt[2];
	uint64_t t_prt[2];
	uint16_t r_prt;
	uint8_t init_int;
	uint8_t disallowed;
	uint8_t prevent_medium_removal;
	uint8_t pad;
	atomic16_t blocked;
	atomic_t refs;
	uint32_t timestamp;
	SLIST_ENTRY(initiator_state) i_list;
	SLIST_HEAD(, sense_info) sense_list;
	wait_chan_t *istate_wait;
};

SLIST_HEAD(istate_list, initiator_state);

struct tdevice {
	int type;
	int tl_id;
	int target_id;
	int flags;
	int iscsi_tid;
	int vhba_id;
	char name[40];
	void *hpriv;
	sx_t *reservation_lock;
	struct qs_devq *devq;
	struct reservation reservation;
	struct istate_list istate_list;
};

static inline char *
tdevice_name(struct tdevice *tdevice)
{
	return tdevice->name;
}
#define tdevice_reservation_lock(tdev)             (sx_xlock((tdev)->reservation_lock))
#define tdevice_reservation_unlock(tdev)                                   \
do {                                                                    \
        debug_check(!sx_xlocked((tdev)->reservation_lock));              \
        sx_xunlock((tdev)->reservation_lock);                            \
} while (0)

int tdevice_init(struct tdevice *tdevice, int type, int tl_id, int target_id, char *name, void (*proc_cmd) (void *, void *), char *thr_name);
void tdevice_exit(struct tdevice *tdevice);
void tdevice_reset(struct tdevice *tdevice, uint64_t i_prt[], uint64_t t_prt[], uint8_t init_int);

void tdevice_insert_ccb(struct qsio_hdr *ccb_h);
int tdevice_check_cmd(struct tdevice *tdevice, uint8_t op);

int vdevice_new(struct vdeviceinfo *deviceinfo);
int vdevice_delete(struct vdeviceinfo *deviceinfo);
int vdevice_modify(struct vdeviceinfo *deviceinfo);
int vdevice_reset_stats(struct vdeviceinfo *deviceinfo);
int vdevice_info(struct vdeviceinfo *deviceinfo);
int vdevice_load(struct vdeviceinfo *deviceinfo);
void tdevice_cbs_disable(struct tdevice *tdevice);
void tdevice_cbs_remove(struct tdevice *tdevice);
int tdevice_delete(uint32_t tl_id, int free_alloc);
int vcartridge_new(struct vcartridge *vcartridge);
int vcartridge_load(struct vcartridge *vcartridge);
int vcartridge_delete(struct vcartridge *vcartridge);
int vcartridge_info(struct vcartridge *vcartridge);
int vcartridge_reload(struct vcartridge *vcartridge);

#define tdevice_get(tdev)	do {} while (0)
#define tdevice_put(tdev)	do {} while (0)
#endif

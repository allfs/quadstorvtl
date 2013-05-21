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

#ifndef QUADSTOR_FCQ_H_
#define QUADSTOR_FCQ_H_

#include <bsddefs.h>
#include <exportdefs.h>
#include <missingdefs.h>
#include "isp_freebsd.h" 

#define WWN_SIZE 8
enum {
	ENTRY_TYPE_CTIO,
	ENTRY_TYPE_NOTIFY,
};

typedef struct tgtcmd {
	union {
		atio_private_data_t atp;
		inot_private_data_t itp;
	} u;
	uint8_t cdb[16];
	struct qsio_hdr *ccb;
	sglist_t *sglist;
	uint8_t aborted;
	uint8_t local_pool;
	uint8_t tag_action;
	uint16_t nphdl;
	uint16_t target_lun;
	uint16_t chan;
	uint64_t init_id;
	uint64_t i_prt;
	uint64_t t_prt;
	uint16_t r_prt;
	uint16_t in_status;
	uint16_t tag_id;
	uint16_t seq_id;
	uint16_t entry_type;
	uint16_t nt_arg;
	bus_dmamap_t dmap;
	STAILQ_ENTRY(tgtcmd) q_list;
} tgtcmd_t;

struct fcq;
struct fcbridge {
	ispsoftc_t *ha;
	struct fcq *fcq;
	TAILQ_ENTRY(fcbridge) b_list;
	uint32_t id;
};

struct fcq {
	mtx_t fcq_lock;
	int flags;
	int nice;
	wait_chan_t fcq_wait;
	STAILQ_HEAD(, tgtcmd) pending_queue;
	struct fcbridge *fcbridge;
	kproc_t *task;
};

enum {
	FCQ_SHUTDOWN	= 1,
};

struct tgtcmd;
static inline void
fcq_insert_cmd(struct fcbridge *fcbridge, struct tgtcmd *cmd)
{
	struct fcq *fcq = fcbridge->fcq;

	mtx_lock(&fcq->fcq_lock);
	STAILQ_INSERT_TAIL(&fcq->pending_queue, cmd, q_list);
	mtx_unlock(&fcq->fcq_lock);
	chan_wakeup_one(&fcq->fcq_wait);
}

struct fcq * fcq_init(struct fcbridge *fcbridge);
void fcq_exit(struct fcq *fcq);
int fcbridge_proc_cmd(void *bridge, void *iop);
int fcbridge_task_mgmt(struct fcbridge *fcbridge, struct qsio_immed_notify *notify);
void fcbridge_disable_device(struct tdevice *device);
struct fcbridge * fcbridge_new(void *ha, uint32_t id);
void fcbridge_exit(struct fcbridge *fcbridge);

static inline struct tgtcmd *
ctio_cmd(struct qsio_scsiio *ctio)
{
	DEBUG_BUG_ON(!ctio->ccb_h.priv.qpriv.qcmd);
	return ctio->ccb_h.priv.qpriv.qcmd;
}

static inline struct fcbridge *
ctio_fcbridge(struct qsio_scsiio *ctio)
{
	DEBUG_BUG_ON(!ctio->ccb_h.priv.qpriv.fcbridge);
	return ctio->ccb_h.priv.qpriv.fcbridge;
}

static inline struct fcbridge *
notify_fcbridge(struct qsio_immed_notify *notify)
{
	DEBUG_BUG_ON(!notify->ccb_h.priv.qpriv.fcbridge);
	return notify->ccb_h.priv.qpriv.fcbridge;
}

static inline ispsoftc_t *
notify_host(struct qsio_immed_notify *notify)
{
	DEBUG_BUG_ON(!notify->ccb_h.priv.qpriv.fcbridge);
	return notify->ccb_h.priv.qpriv.fcbridge->ha;
}

static inline struct tgtcmd *
notify_cmd(struct qsio_immed_notify *notify)
{
	DEBUG_BUG_ON(!notify->ccb_h.priv.qpriv.qcmd);
	return notify->ccb_h.priv.qpriv.qcmd;
}

static inline ispsoftc_t *
ctio_host(struct qsio_scsiio *ctio)
{
	DEBUG_BUG_ON(!ctio->ccb_h.priv.qpriv.fcbridge);
	return ctio->ccb_h.priv.qpriv.fcbridge->ha;
}

void fcbridge_route_cmd_post(struct qsio_scsiio *ctio);
int fcbridge_i_prt_valid(struct fcbridge *fcbridge, uint64_t i_prt[]);
/* TM failed response code, see FCP */
#define FC_TM_FAILED                0x5

#endif

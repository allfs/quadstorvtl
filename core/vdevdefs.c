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

#include "coredefs.h"
#include "reservation.h"
#include "vdevdefs.h"
#include "scsidefs.h"
#include "sense.h"
#include "tdevice.h"
#include <exportdefs.h>
#include "mchanger.h"

extern struct tdevice *tdevices[];
extern mtx_t *tdevice_lookup_lock;

void
device_add_sense(struct initiator_state *istate, uint8_t error_code, uint8_t sense_key, uint32_t info, uint8_t asc, uint8_t ascq)
{
	struct sense_info *sinfo, *iter, *prev = NULL;
	struct qs_sense_data *sense;
	int count = 1;

	sinfo = zalloc(sizeof(struct sense_info), M_SENSEINFO, Q_WAITOK);
	sense = &sinfo->sense_data;
	fill_sense_info(sense, error_code, sense_key, info, asc, ascq);
	mtx_lock(istate->istate_lock);
	SLIST_FOREACH(iter, &istate->sense_list, s_list) {
		prev = iter;
		count++;
	}

	if (prev) {
		SLIST_INSERT_AFTER(prev, sinfo, s_list);
	}
	else
		SLIST_INSERT_HEAD(&istate->sense_list, sinfo, s_list);

	if (count > MAX_UNIT_ATTENTIONS) {
		iter = SLIST_FIRST(&istate->sense_list);
		SLIST_REMOVE_HEAD(&istate->sense_list, s_list);
		free(iter, M_SENSEINFO);
	}

	mtx_unlock(istate->istate_lock);
	return;
}

int
device_request_sense(struct qsio_scsiio *ctio, struct initiator_state *istate, int add_sense_len)
{
	struct qs_sense_data tmp;
	struct qs_sense_data *sense;
	uint8_t *cdb = ctio->cdb;
	int allocation_length;
	struct sense_info *sense_info = NULL;
	int sense_len;

	allocation_length = cdb[4];

	ctio->scsi_status = SCSI_STATUS_OK;
	if (!device_has_sense(istate)) {
		sense = &tmp;
		bzero(sense, sizeof(struct qs_sense_data));
		sense->error_code = SSD_CURRENT_ERROR;
		sense->flags = SSD_KEY_NO_SENSE;
		sense->add_sense_code = NO_ADDITIONAL_SENSE_INFORMATION_ASC;
		sense->add_sense_code_qual = NO_ADDITIONAL_SENSE_INFORMATION_ASCQ;
		sense->extra_len =
			offsetof(struct qs_sense_data, extra_bytes) -
			offsetof(struct qs_sense_data, cmd_spec_info);
	}
	else
	{
		sense_info = device_get_sense(istate);
		sense = &sense_info->sense_data;
	}
	sense_len = SENSE_LEN(sense);

	ctio->dxfer_len = min_t(int, sense_len + add_sense_len, allocation_length);
	ctio_allocate_buffer(ctio, ctio->dxfer_len, Q_WAITOK);
	memcpy(ctio->data_ptr, sense, ctio->dxfer_len);

	if (sense_info)
		free(sense_info, M_SENSEINFO);
	/* send ccb */
	return 0;
}

int
device_find_sense(struct initiator_state *istate, uint8_t sense_key, uint8_t asc, uint8_t ascq)
{
	struct sense_info *sinfo;
	struct qs_sense_data *sense;

	mtx_lock(istate->istate_lock);
	SLIST_FOREACH(sinfo, &istate->sense_list, s_list) {
		sense = &sinfo->sense_data;
		if (sense->flags == sense_key &&
		    sense->add_sense_code == asc &&
		    sense->add_sense_code_qual == ascq)
		{
			mtx_unlock(istate->istate_lock);
			return 0;
		}
	}
	mtx_unlock(istate->istate_lock);
	return -1;
}

/* called under lock */
void
device_unit_attention(struct tdevice *tdevice, int all, uint64_t i_prt[], uint64_t t_prt[], uint8_t init_int, uint8_t asc, uint8_t ascq, int ignore_dup)
{
	struct initiator_state *istate;
	int retval;

	SLIST_FOREACH(istate, &tdevice->istate_list, i_list) {
		if (!all && iid_equal(istate->i_prt, istate->t_prt, istate->init_int, i_prt, t_prt, init_int))
		{
			continue;
		}

		if (ignore_dup)
		{
			retval = device_find_sense(istate, SSD_KEY_UNIT_ATTENTION, asc, ascq);
			if (retval == 0)
				continue;
		}
		device_add_sense(istate, SSD_CURRENT_ERROR, SSD_KEY_UNIT_ATTENTION, 0, asc, ascq);
	}
}

void
device_wait_all_initiators(struct istate_list *lhead)
{
	struct initiator_state *iter;

	SLIST_FOREACH(iter, lhead, i_list) {
		wait_on_chan(iter->istate_wait, TAILQ_EMPTY(&iter->queue_list));
	}
}

void
device_free_all_initiators(struct istate_list *lhead)
{
	struct initiator_state *iter;

	while ((iter = SLIST_FIRST(lhead)) != NULL) {
		SLIST_REMOVE_HEAD(lhead, i_list);
		free_initiator_state(iter);
	}
}

void
device_free_stale_initiators(struct istate_list *lhead)
{
	struct initiator_state *iter, *prev = NULL;
	unsigned long elapsed;

	SLIST_FOREACH(iter, lhead, i_list) {
		elapsed = get_elapsed(iter->timestamp);
		if (ticks_to_msecs(elapsed) < stale_initiator_timeout) {
			prev = iter;
			continue;
		}

		if (prev)
			SLIST_REMOVE_AFTER(prev, i_list);
		else
			SLIST_REMOVE_HEAD(lhead, i_list);
		free_initiator_state(iter);
	}
}

void
device_init_naa_identifier(struct logical_unit_naa_identifier *naa_identifier, char *serial_number)
{
	naa_identifier->code_set = 0x01;
	naa_identifier->identifier_type = UNIT_IDENTIFIER_NAA;
	naa_identifier->identifier_length = sizeof(struct logical_unit_naa_identifier) - offsetof(struct logical_unit_naa_identifier, naa_id);
	memcpy(naa_identifier->naa_id, serial_number, 16);
	naa_identifier->naa_id[0] = 0x6e;
}

void
device_init_unit_identifier(struct logical_unit_identifier *unit_identifier, char *vendor_id, char *product_id, int serial_len)
{
	unit_identifier->code_set = 0x02; /*logical unit idenifier */
	unit_identifier->identifier_type = UNIT_IDENTIFIER_T10_VENDOR_ID;
	sys_memset(unit_identifier->vendor_id, ' ', 8);
	strncpy(unit_identifier->vendor_id, vendor_id, strlen(vendor_id));
	sys_memset(unit_identifier->product_id, ' ', 16);
	strncpy(unit_identifier->product_id, product_id, strlen(product_id));
	unit_identifier->identifier_length = offsetof(struct logical_unit_identifier, serial_number) - offsetof(struct logical_unit_identifier, vendor_id);
	unit_identifier->identifier_length += serial_len;
}

extern sx_t *cbs_lock;
extern struct interface_list cbs_list;

void
cbs_disable_device(struct tdevice *tdevice)
{
	struct qs_interface_cbs *cbs;

	if (!atomic_test_bit(DEVICE_ATTACHED, &tdevice->flags))
		return;

	sx_xlock(cbs_lock);
	LIST_FOREACH(cbs, &cbs_list, i_list) {
		if (cbs->interface == TARGET_INT_ISCSI)
			(*cbs->disable_device)(tdevice, tdevice->iscsi_tid, tdevice->hpriv);
		else
			(*cbs->disable_device)(tdevice, tdevice->vhba_id, tdevice->hpriv);
	}
	sx_xunlock(cbs_lock);
}

void
cbs_remove_device(struct tdevice *tdevice)
{
	struct qs_interface_cbs *cbs;

	sx_xlock(cbs_lock);
	if (!atomic_test_bit(DEVICE_ATTACHED, &tdevice->flags)) {
		sx_xunlock(cbs_lock);
		return;
	}

	atomic_clear_bit(DEVICE_ATTACHED, &tdevice->flags);
	LIST_FOREACH(cbs, &cbs_list, i_list) {
		if (cbs->interface == TARGET_INT_ISCSI)
			(*cbs->remove_device)(tdevice, tdevice->iscsi_tid, tdevice->hpriv);
		else
			(*cbs->remove_device)(tdevice, tdevice->vhba_id, tdevice->hpriv);
	}
	sx_xunlock(cbs_lock);
}

void
cbs_update_device(struct tdevice *tdevice)
{
	struct qs_interface_cbs *cbs;

	if (!atomic_test_bit(DEVICE_ATTACHED, &tdevice->flags))
		return;

	sx_xlock(cbs_lock);
	LIST_FOREACH(cbs, &cbs_list, i_list) {
		if (!cbs->update_device)
			continue;
		if (cbs->interface == TARGET_INT_ISCSI)
			(*cbs->update_device)(tdevice, tdevice->iscsi_tid, tdevice->hpriv);
		else
			(*cbs->update_device)(tdevice, tdevice->vhba_id, tdevice->hpriv);
	}
	sx_xunlock(cbs_lock);

}

void
cbs_new_device(struct tdevice *tdevice, int notify_usr)
{
	struct qs_interface_cbs *cbs;

	sx_xlock(cbs_lock);
	if (atomic_test_bit(DEVICE_ATTACHED, &tdevice->flags)) {
		sx_xunlock(cbs_lock);
		return;
	}
	LIST_FOREACH(cbs, &cbs_list, i_list) {
		(*cbs->new_device)(tdevice);
	}
	atomic_set_bit(DEVICE_ATTACHED, &tdevice->flags);
	sx_xunlock(cbs_lock);
}

struct tdevice *
get_device(uint32_t fc_bus)
{
	struct tdevice *tdevice;
	struct tdrive *tdrive;
	int bus, target_id;

	bus = (fc_bus - 1) / TL_DEVICES_PER_BUS;
	target_id = (fc_bus - 1) % TL_DEVICES_PER_BUS;

	if (bus >= TL_MAX_DEVICES)
		return NULL;

	mtx_lock(tdevice_lookup_lock);
	tdevice = tdevices[bus];
	if (!tdevice) {
		mtx_unlock(tdevice_lookup_lock);
		return NULL;
	}

	if (target_id) {
		tdrive = mchanger_locate_tdrive((struct mchanger *)tdevice, target_id);
		tdevice = (struct tdevice *)tdrive;
	}

	if (tdevice && !atomic_test_bit(DEVICE_ATTACHED, &tdevice->flags))
		tdevice = NULL;
	else if (tdevice)
		tdevice_get(tdevice);
	mtx_unlock(tdevice_lookup_lock);
	return tdevice;
}

int
get_next_device_id(void)
{
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++)
	{
		if (!tdevices[i])
			return i;
	}
	return -1;	
}

static int
__device_istate_abort_task(struct tdevice *tdevice, uint64_t i_prt[], uint64_t t_prt[], int init_int, uint32_t task_tag)
{
	struct initiator_state *istate;
	int task_found, task_exists;

	tdevice_reservation_lock(tdevice);
	istate = device_get_initiator_state(tdevice, i_prt, t_prt, 0, init_int, 0, 1);
	tdevice_reservation_unlock(tdevice);
	if (!istate)
		return 0;

	task_exists = 0;
	tdevice_reservation_lock(tdevice);
	task_found = istate_abort_task(istate, i_prt, t_prt, init_int, task_tag, &task_exists);
	if (task_found) {
		tdevice_reservation_unlock(tdevice);
		atomic16_set(&istate->blocked, 0);
		chan_wakeup(istate->istate_wait);
		istate_put(istate);
		return task_found;
	}

	tdevice_reservation_unlock(tdevice);
	while (!task_found && task_exists) {
		debug_info("task not found, but exists\n");
		pause("psg", 1000);
		task_exists = istate_task_exists(istate, task_tag);
	}
	atomic16_set(&istate->blocked, 0);
	chan_wakeup(istate->istate_wait);
	istate_put(istate);
	return task_found;
}

int
device_istate_abort_task(struct tdevice *tdevice, uint64_t i_prt[], uint64_t t_prt[], int init_int, uint32_t task_tag)
{
	int task_found, i;

	if (tdevice) {
		return __device_istate_abort_task(tdevice, i_prt, t_prt, init_int, task_tag);
	}

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		mtx_lock(tdevice_lookup_lock);
		tdevice = tdevices[i]; 
		if (tdevice)
			tdevice_get(tdevice);
		mtx_unlock(tdevice_lookup_lock);
		if (!tdevice)
			continue;
		task_found = __device_istate_abort_task(tdevice, i_prt, t_prt, init_int, task_tag);
		tdevice_put(tdevice);
		if (task_found)
			return task_found;
	}
	return 0;
}

static void
__device_istate_abort_task_set(struct tdevice *tdevice, uint64_t i_prt[], uint64_t t_prt[], int init_int)
{
	struct initiator_state *istate;

	tdevice_reservation_lock(tdevice);
	istate = device_get_initiator_state(tdevice, i_prt, t_prt, 0, init_int, 0, 1);
	tdevice_reservation_unlock(tdevice);
	if (!istate)
		return;
	tdevice_reservation_lock(tdevice);
	istate_abort_task_set(istate);
	tdevice_reservation_unlock(tdevice);
	wait_on_chan(istate->istate_wait, TAILQ_EMPTY(&istate->queue_list));
	atomic16_set(&istate->blocked, 0);
	chan_wakeup(istate->istate_wait);
	istate_put(istate);
}

void
device_istate_abort_task_set(struct tdevice *tdevice, uint64_t i_prt[], uint64_t t_prt[], int init_int)
{
	int i;

	if (tdevice) {
		__device_istate_abort_task_set(tdevice, i_prt, t_prt, init_int);
		return;
	}

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		mtx_lock(tdevice_lookup_lock);
		tdevice = tdevices[i]; 
		if (tdevice)
			tdevice_get(tdevice);
		mtx_unlock(tdevice_lookup_lock);
		if (!tdevice)
			continue;
		__device_istate_abort_task_set(tdevice, i_prt, t_prt, init_int);
		tdevice_put(tdevice);
	}
}

void
device_free_initiator(uint64_t i_prt[], uint64_t t_prt[], int init_int, struct tdevice *tdevice)
{
	int i;

	debug_info("i_prt %llx %llx t_prt %llx %llx init int %d tdevice %p\n", (unsigned long long)i_prt[0], (unsigned long long)i_prt[1], (unsigned long long)t_prt[0], (unsigned long long)t_prt[1], init_int, tdevice);
	if (tdevice) {
		device_free_initiator_state2(tdevice, i_prt, t_prt, init_int);
		return;
	}

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		mtx_lock(tdevice_lookup_lock);
		tdevice = tdevices[i]; 
		if (tdevice)
			tdevice_get(tdevice);
		mtx_unlock(tdevice_lookup_lock);
		if (!tdevice)
			continue;
		device_free_initiator_state2(tdevice, i_prt, t_prt, init_int);
		tdevice_put(tdevice);
	}
}

int
pgdata_allocate_data(struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, allocflags_t flags)
{
	struct pgdata **pglist;
	int pglist_cnt;

	pglist = pgdata_allocate(block_size, num_blocks, &pglist_cnt, flags, 1);
	if (unlikely(!pglist))
	{
		debug_warn("Allocation for pglist of num_blocks %u failed\n", num_blocks);
		return -1;
	}
	ctio->data_ptr = (void *)pglist;
	ctio->pglist_cnt = pglist_cnt;
	ctio->dxfer_len = block_size * num_blocks;
	return 0;
}

int
device_allocate_buffers(struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, allocflags_t flags)
{
	/* Allocate for the data transfer */
	if (!num_blocks)
		return 0;

	pgdata_allocate_data(ctio, block_size, num_blocks, flags);
	if (!ctio->data_ptr) {
		debug_warn("Allocating ctio data_ptr failed num_blocks %u\n", num_blocks);
		return -1;
	}
	ctio->ccb_h.flags |= QSIO_DATA_DIR_OUT;
	return 0;
}

int
device_allocate_cmd_buffers(struct qsio_scsiio *ctio, allocflags_t flags)
{
	uint8_t *cdb = ctio->cdb;
	uint32_t parameter_list_length;

	switch (cdb[0])
	{
		case MODE_SELECT_6:
			parameter_list_length = cdb[4];
			break;
		case SEND_DIAGNOSTIC:
			parameter_list_length = be16toh(*(uint16_t *)(&cdb[3]));
			break;
		case MODE_SELECT_10:
		case PERSISTENT_RESERVE_OUT:
		case UNMAP:
			parameter_list_length = be16toh(*(uint16_t *)(&cdb[7]));
			break;
		case WRITE_ATTRIBUTE:
			parameter_list_length = be32toh(*(uint32_t *)(&cdb[10]));
			if (parameter_list_length > 65536) /* Limit memory here */
				return -1;
			break;
		default:
			parameter_list_length = 0;
			debug_check(1);
	}

	debug_info("cdb %x parameter_list_length %d\n", cdb[0], parameter_list_length);
	ctio_allocate_buffer(ctio, parameter_list_length, flags);
	if (!ctio->data_ptr)
	{
		return -1;
	}
	ctio->ccb_h.flags |= QSIO_DATA_DIR_OUT;
	return 0;
}

void
ctio_free_all(struct qsio_scsiio *ctio)
{
	ctio_free_data(ctio);
	if (ctio->istate)
		istate_put(ctio->istate);
	ctio_free_sense(ctio);
	uma_zfree(ctio_cache, ctio);
}

static struct fc_rule_list fc_rule_list = TAILQ_HEAD_INITIALIZER(fc_rule_list);

static void
tdevice_revalidate_istates(struct tdevice *tdevice)
{
	struct istate_list *istate_list = &tdevice->istate_list;
	struct initiator_state *iter;

	tdevice_reservation_lock(tdevice);
	SLIST_FOREACH(iter, istate_list, i_list) {
		if (iter->init_int == TARGET_INT_FC)
			iter->disallowed = fc_initiator_check(iter->i_prt, tdevice);
	}
	tdevice_reservation_unlock(tdevice);
}

static void
update_istates(void)
{
	struct tdevice *tdevice;
	int i;

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		mtx_lock(tdevice_lookup_lock);
		tdevice = tdevices[i]; 
		if (tdevice)
			tdevice_get(tdevice);
		mtx_unlock(tdevice_lookup_lock);
		if (!tdevice)
			continue;
		tdevice_revalidate_istates(tdevice);
		tdevice_put(tdevice);
	}
}

int
target_add_fc_rule(struct fc_rule_config *fc_rule_config)
{
	struct fc_rule *fc_rule, *iter;

	fc_rule = zalloc(sizeof(*fc_rule), M_QUADSTOR, Q_WAITOK);
	if (unlikely(!fc_rule)) {
		debug_warn("Memory allocation failure\n");
		return -1;
	}

	port_fill(fc_rule->wwpn, fc_rule_config->wwpn);
	fc_rule->target_id = fc_rule_config->target_id;
	fc_rule->rule = fc_rule_config->rule;
	debug_info("wwpn %llx %llx target id %u rule %d\n", (unsigned long long)fc_rule->wwpn[0], (unsigned long long)fc_rule->wwpn[1], fc_rule->target_id, fc_rule->rule);

	mtx_lock(glbl_lock);
	TAILQ_FOREACH(iter, &fc_rule_list, r_list) {
		if (port_equal(iter->wwpn, fc_rule->wwpn) && iter->target_id == fc_rule->target_id) {
			iter->rule = fc_rule->rule;
			mtx_unlock(glbl_lock);
			free(fc_rule, M_QUADSTOR);
			update_istates();
			return 0;
		}
	}

	if (!fc_rule->wwpn[0] && !fc_rule->wwpn[1])
		TAILQ_INSERT_HEAD(&fc_rule_list, fc_rule, r_list);
	else
		TAILQ_INSERT_TAIL(&fc_rule_list, fc_rule, r_list);
	mtx_unlock(glbl_lock);
	update_istates();
	return 0;
}

int
target_remove_fc_rule(struct fc_rule_config *fc_rule_config)
{
	struct fc_rule *iter;

	mtx_lock(glbl_lock);
	TAILQ_FOREACH(iter, &fc_rule_list, r_list) {
		if (port_equal(iter->wwpn, fc_rule_config->wwpn) && iter->target_id == fc_rule_config->target_id) {
			TAILQ_REMOVE(&fc_rule_list, iter, r_list);
			mtx_unlock(glbl_lock);
			free(iter, M_QUADSTOR);
			update_istates();
			return 0;
		}
	}
	mtx_unlock(glbl_lock);
	return 0;
}

void
target_clear_fc_rules(int target_id)
{
	struct fc_rule *iter, *next;

	mtx_lock(glbl_lock);
	TAILQ_FOREACH_SAFE(iter, &fc_rule_list, r_list, next) {
		if (target_id >= 0 && iter->target_id != target_id)
			continue;

		TAILQ_REMOVE(&fc_rule_list, iter, r_list);
		free(iter, M_QUADSTOR);
	}
	mtx_unlock(glbl_lock);
}

int
fc_initiator_check(uint64_t wwpn[], void *device)
{
	struct fc_rule *iter;
	int rule_wwpn = -1;
	int rule_target = -1;
	int rule_all_wwpn = -1;
	int rule_all_target = -1;
	struct tdevice *tdevice = device;
	uint32_t target_id = tdevice->tl_id;

	debug_info("wwpn %llx %llx target id %u\n", (unsigned long long)wwpn[0], (unsigned long long)wwpn[1], tdevice->target_id);
	mtx_lock(glbl_lock);
	if (TAILQ_EMPTY(&fc_rule_list)) {
		mtx_unlock(glbl_lock);
		return FC_RULE_ALLOW;
	}

	TAILQ_FOREACH(iter, &fc_rule_list, r_list) {
		debug_info("iter wwpn %llx %llx target id %u\n", (unsigned long long)iter->wwpn[0], (unsigned long long)iter->wwpn[1], iter->target_id);
		if (port_equal(iter->wwpn, wwpn) && iter->target_id == target_id) {
			debug_info("found match %llx %llx tdevice %s rule %d\n", (unsigned long long)wwpn[0], (unsigned long long)wwpn[1], tdevice_name(tdevice), iter->rule);
			mtx_unlock(glbl_lock);
			return iter->rule;
		}

		if (port_equal(iter->wwpn, wwpn) && !iter->target_id)
			rule_wwpn = iter->rule;
		else if (iter->target_id == target_id)
			rule_target = iter->rule;
		else if (!iter->wwpn[0])
			rule_all_wwpn = iter->rule;
		else if (!iter->target_id)
			rule_all_target = iter->rule;
	}
	mtx_unlock(glbl_lock);
	debug_info("found match %llx %llx tdevice %s rule_wwpn %d rule_target %d rule all wwpn %d rule all target %d\n", (unsigned long long)wwpn[0], (unsigned long long)wwpn[1], tdevice_name(tdevice), rule_wwpn, rule_target, rule_all_wwpn, rule_all_target);
	if (rule_wwpn > 0)
		return rule_wwpn;
	else if (rule_target > 0)
		return rule_target;
	else if (rule_all_wwpn > 0)
		return rule_all_wwpn;
	else if (rule_all_target > 0)
		return rule_all_target;
	else
		return FC_RULE_ALLOW;
}

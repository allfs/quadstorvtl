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
#include "sense.h"
#include "vdevdefs.h"
#include <exportdefs.h>

static int
initiator_has_registered_ar(struct reservation *reservation, uint64_t i_prt, uint64_t t_prt, uint8_t init_int)
{
	struct registration *tmp;

	SLIST_FOREACH(tmp, &reservation->registration_list, r_list) {
		if (iid_equal(tmp->i_prt, tmp->t_prt, tmp->init_int, i_prt, t_prt, init_int))
			return 1;
	}
	return 0;
}

static int
in_persistent_ro_type(struct reservation *reservation)
{
	if (!reservation->is_reserved || reservation->type != RESERVATION_TYPE_PERSISTENT)
		return 0;

	if (reservation->persistent_type == RESERVATION_TYPE_WRITE_EXCLUSIVE_RO || reservation->persistent_type == RESERVATION_TYPE_EXCLUSIVE_ACCESS_RO)
		return 1;
	else
		return 0; 

}

static int
in_persistent_ar_type(struct reservation *reservation)
{
	if (!reservation->is_reserved || reservation->type != RESERVATION_TYPE_PERSISTENT)
		return 0;

	if (reservation->persistent_type == RESERVATION_TYPE_WRITE_EXCLUSIVE_AR || reservation->persistent_type == RESERVATION_TYPE_EXCLUSIVE_ACCESS_AR)
		return 1;
	else
		return 0; 
}
 
static int
is_persistent_reservation_holder(struct reservation *reservation, uint64_t i_prt, uint64_t t_prt, uint8_t init_int)
{
	if (reservation->type != RESERVATION_TYPE_PERSISTENT)
		return 0;

	switch (reservation->persistent_type) {
		case RESERVATION_TYPE_WRITE_EXCLUSIVE:
		case RESERVATION_TYPE_EXCLUSIVE_ACCESS:
		case RESERVATION_TYPE_WRITE_EXCLUSIVE_RO:
		case RESERVATION_TYPE_EXCLUSIVE_ACCESS_RO:
			if (iid_equal(reservation->i_prt, reservation->t_prt, reservation->init_int, i_prt, t_prt, init_int))
				return 1;
			else
				return 0;
		case RESERVATION_TYPE_WRITE_EXCLUSIVE_AR:
		case RESERVATION_TYPE_EXCLUSIVE_ACCESS_AR:
			if (initiator_has_registered_ar(reservation, i_prt, t_prt, init_int))
				return 1;
			else
				return 0;
	}
	debug_check(1);
	return 0;
}

static int
multiple_registrants_reservation_type(struct reservation *reservation)
{
	switch (reservation->persistent_type) {
		case RESERVATION_TYPE_WRITE_EXCLUSIVE:
		case RESERVATION_TYPE_EXCLUSIVE_ACCESS:
			return 0;
		case RESERVATION_TYPE_WRITE_EXCLUSIVE_RO:
		case RESERVATION_TYPE_EXCLUSIVE_ACCESS_RO:
		case RESERVATION_TYPE_WRITE_EXCLUSIVE_AR:
		case RESERVATION_TYPE_EXCLUSIVE_ACCESS_AR:
			return 1;
	}
	debug_check(1);
	return 0;
}

int
persistent_reservation_read_capabilities(struct qsio_scsiio *ctio, uint16_t allocation_length)
{
	struct reservation_capabilities cap;
	uint16_t min_len;

	bzero(&cap, sizeof(cap));
	cap.length = htobe16(8);
#if 0
	cap.ptpl_c |= 0x01; /* PTPL_C */
	cap.ptpl_c |= 0x10;  /* CRH */
	cap.ptpl_c |= 0x04; /* ATP_C All Target Ports capable*/
#endif
	cap.tmv |= 0x80; /* TMV */
	cap.tmask1 |= 0x2; /* WR_EX/Write Exclusive access */
	cap.tmask1 |= 0x8; /* EX_AC/Exclusive access */
	cap.tmask1 |= 0x20; /* WR_EX_RO/Write Exclusive Registrants Only */
	cap.tmask1 |= 0x40; /* EX_AC_RO/Exclusive Access Registrants Only */
	cap.tmask1 |= 0x80; /* WR_EX_AR/Write Exclusive All Registrants */
	cap.tmask2 = 0x1; /* EX_AC_AR/Exclusive Access All Registrants */
	min_len = min_t(uint16_t, allocation_length, sizeof(struct reservation_capabilities));
	ctio_allocate_buffer(ctio, min_len, Q_NOWAIT);
	if (!ctio->data_ptr)
	{
		return -1;
	}
	memcpy(ctio->data_ptr, &cap, min_len);
	return 0;
}

extern struct mdaemon_info mdaemon_info;

int
persistent_reservation_read_reservations(struct qsio_scsiio *ctio, uint16_t allocation_length, struct reservation *reservation)
{
	uint16_t done = 0;
	uint8_t *buffer;
	struct pin_data *pin_data;

	ctio_allocate_buffer(ctio, allocation_length, Q_NOWAIT);
	if (!ctio->data_ptr)
	{
		return -1;
	}

	buffer = ctio->data_ptr;

	*((uint32_t *)buffer) = htobe32(reservation->generation);
	done = 8;
	debug_info("reservation is_reserved %d i_prt %llx t_prt %llx key %llx\n", reservation->is_reserved, (unsigned long long)reservation->i_prt, (unsigned long long)reservation->t_prt, (unsigned long long)reservation->persistent_key);
	if (reservation->is_reserved)
	{
		if (allocation_length >= (8 + sizeof(pin_data)))
		{
			pin_data = (struct pin_data *)(buffer+8);
			bzero(pin_data, sizeof(struct pin_data));
			pin_data->key = htobe64(reservation->persistent_key);
			pin_data->type = reservation->persistent_type;
		}
		done += sizeof(struct pin_data);
	}

	*((uint32_t *)(buffer+4)) = htobe32(done - 8);
	if (done < allocation_length)
	{
		ctio->dxfer_len = done;
	}
	return 0;
}

static void
registrant_unit_attention(struct tdevice *tdisk, struct registration *registration, uint8_t asc, uint8_t ascq)
{
	struct initiator_state *istate;

	istate = device_get_initiator_state(tdisk, registration->i_prt, registration->t_prt, registration->r_prt, registration->init_int, 0, 0);
	if (!istate)
		return;

	device_add_sense(istate, SSD_CURRENT_ERROR, SSD_KEY_UNIT_ATTENTION, 0, asc, ascq);
	istate_put(istate);
}

static void
registrants_unit_attention(struct tdevice *tdisk, uint8_t asc, uint8_t ascq, uint64_t i_prt, uint64_t t_prt, uint8_t init_int, int skip)
{
	struct reservation *reservation = &tdisk->reservation;
	struct registration *registration;
	int send_sync = 0;

	SLIST_FOREACH(registration, &reservation->registration_list, r_list) {
		if (skip && iid_equal(registration->i_prt, registration->t_prt, registration->init_int, i_prt, t_prt, init_int))
			continue;
		registrant_unit_attention(tdisk, registration, asc, ascq);
		send_sync = 1;
	}
}

void
persistent_reservation_clear(struct registration_list *lhead)
{
	struct registration *tmp;

	while ((tmp = SLIST_FIRST(lhead)) != NULL) {
		SLIST_REMOVE_HEAD(lhead, r_list);
		free(tmp, M_RESERVATION);
	}
}

int
persistent_reservation_read_full(struct qsio_scsiio *ctio, uint16_t allocation_length, struct reservation *reservation)
{
	struct registration *registration;
	uint16_t done;
	uint8_t *buffer;
	uint16_t desc_size;
	uint16_t transport_size;
	struct pfull_data *pfull;

	ctio_allocate_buffer(ctio, allocation_length, Q_NOWAIT);
	if (!ctio->data_ptr)
	{
		return -1;
	}

	buffer = ctio->data_ptr;

	*((uint32_t *)buffer) = htobe32(reservation->generation);
	done = 8;
	SLIST_FOREACH(registration, &reservation->registration_list, r_list) {
		desc_size = sizeof(struct pfull_data);
		transport_size = 0;
		if (registration->init_int == TARGET_INT_ISCSI)
		{
			transport_size += sizeof(struct transport_id_common);
			transport_size += strlen(registration->init_name);
		}
		else if (registration->init_int == TARGET_INT_FC)
		{
			transport_size += sizeof(struct transport_id_fc);
		}
		else if (registration->init_int == TARGET_INT_LOCAL)
		{
			transport_size += sizeof(struct transport_id_scsi);
		}

		desc_size += transport_size;
		if ((done + desc_size) > allocation_length)
		{
			done += desc_size;
			continue;
		}

		pfull = (struct pfull_data *)(buffer+done);
		bzero(pfull, sizeof(struct pfull_data));
		pfull->key = htobe64(registration->key);
		pfull->addl_len = htobe16(transport_size);
		debug_info("reservation is_reserved %d reservation->i_prt %llx reservation->t_prt %llx reservation->init_int %d\n", reservation->is_reserved, (unsigned long long)reservation->i_prt, (unsigned long long)reservation->t_prt, reservation->init_int);
		debug_info("registration init_name %s\n", registration->init_name);
		if (reservation->is_reserved && is_persistent_reservation_holder(reservation, registration->i_prt, registration->t_prt, registration->init_int)) {
			pfull->r_holder |= 0x01;
#if 0
			pfull->r_holder |= 0x02; /* All Target ports */
#endif
			pfull->type = reservation->persistent_type;
		}
		pfull->rel_tgt_port = htobe16(reservation->r_prt);

		done += sizeof(struct pfull_data);
		bzero(buffer+done, transport_size);
		if (registration->init_int == TARGET_INT_ISCSI)
		{
			struct transport_id_common *tid = (struct transport_id_common *)(buffer+done);
			tid->protocol_id = 0x05;
			tid->addl_len = htobe16(transport_size - sizeof(struct transport_id_common));
			done += sizeof(struct transport_id_common);
			strcpy(buffer+done, registration->init_name);
			done += strlen(registration->init_name);
		}
		else if (registration->init_int == TARGET_INT_FC)
		{
			struct transport_id_fc *tid = (struct transport_id_fc *)(buffer+done);
			memcpy(tid->n_port_name, &registration->i_prt, 8);
			done += sizeof(struct transport_id_fc);
		}
		else if (registration->init_int == TARGET_INT_LOCAL)
		{
			struct transport_id_scsi *tid = (struct transport_id_scsi *)(buffer+done);
			tid->protocol_id = 0x01;
			tid->scsi_address = htobe16(0x07);
			done += sizeof(struct transport_id_scsi);
		}
	}

	*((uint32_t *)(buffer+4)) = htobe32(done - 8);
	if (done < allocation_length)
	{
		ctio->dxfer_len = done;
	}
	return 0;
}

int
persistent_reservation_read_keys(struct qsio_scsiio *ctio, uint16_t allocation_length, struct reservation *reservation)
{
	struct registration *registration;
	uint16_t done;
	uint8_t *buffer;
	uint8_t key_size = 8;

	ctio_allocate_buffer(ctio, allocation_length, Q_NOWAIT);
	if (!ctio->data_ptr)
	{
		return -1;
	}

	buffer = ctio->data_ptr;
	*((uint32_t *)buffer) = htobe32(reservation->generation);

	done = 8;
	SLIST_FOREACH(registration, &reservation->registration_list, r_list) {
		if ((done + key_size) > allocation_length)
		{
			done += key_size;
			continue;
		}
		*((uint64_t *)(buffer+done)) = htobe64(registration->key);
		done += key_size;
	}

	*((uint32_t *)(buffer+4)) = htobe32(done - 8);
	if (done < allocation_length)
	{
		ctio->dxfer_len = done;
	}
	return 0;
}

static int
initiator_has_registered(uint64_t key, uint64_t i_prt, uint64_t t_prt, uint8_t init_int, struct registration_list *lhead, struct registration **registration)
{
	struct registration *tmp;

	SLIST_FOREACH(tmp, lhead, r_list) {
		if ((iid_equal(tmp->i_prt, tmp->t_prt, tmp->init_int, i_prt, t_prt, init_int)) && tmp->key == key)
		{
			if (registration)
				*registration = tmp;
			return 1;
		}
	}
	return 0;
}

static void
persistent_reservation_reset(struct reservation *reservation)
{
	reservation->is_reserved = 0;
	reservation->type = 0;
	reservation->persistent_type = 0;
	reservation->persistent_key = 0;
}

static void
persistent_reservation_remove_registration(struct tdevice *tdisk, struct registration *registration)
{
	struct reservation *reservation = &tdisk->reservation;

	if (!reservation->is_reserved)
		return;

	if (in_persistent_ar_type(reservation)) {
		if (SLIST_EMPTY(&reservation->registration_list))
			persistent_reservation_reset(reservation);
	}
	else {
		if (is_persistent_reservation_holder(reservation, registration->i_prt, registration->t_prt, registration->init_int)) {
			if (in_persistent_ro_type(reservation)) {
				registrants_unit_attention(tdisk, RESERVATIONS_RELEASED_ASC, RESERVATIONS_RELEASED_ASCQ, registration->i_prt, registration->t_prt, registration->init_int, 1);
			}
			persistent_reservation_reset(reservation);
		}
	}
}
 
int
persistent_reservation_handle_register(struct tdevice *tdisk, struct qsio_scsiio *ctio)
{
	struct reservation *reservation = &tdisk->reservation;
	struct registration *tmp, *registration, *prev = NULL;
	struct reservation_parameter *param;
	uint64_t service_action_key;
	uint64_t prev_key;

	param = (struct reservation_parameter *)(ctio->data_ptr);
	prev_key = be64toh(param->key);
	service_action_key = be64toh(param->service_action_key);
	registration = zalloc(sizeof(struct registration), M_RESERVATION, Q_WAITOK);

	SLIST_FOREACH(tmp, &reservation->registration_list, r_list) {

		if (!iid_equal(tmp->i_prt, tmp->t_prt, tmp->init_int, ctio->i_prt, ctio->t_prt, ctio->init_int))
		{
			prev = tmp;
			continue;
		}

		if (prev_key != tmp->key) {
			free(registration, M_RESERVATION);
			ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
			return 0;
		}

		if (!service_action_key)
		{
			if (prev)
				SLIST_REMOVE_AFTER(prev, r_list);
			else
				SLIST_REMOVE_HEAD(&reservation->registration_list, r_list);
			persistent_reservation_remove_registration(tdisk, tmp);
			free(tmp, M_RESERVATION);
		}
		else
		{
			tmp->key = service_action_key;
		}
		reservation->generation++;
		free(registration, M_RESERVATION);
		return 0;
	}

	if (!prev_key && !service_action_key)
	{
		reservation->generation++;
		free(registration, M_RESERVATION);
		return 0;
	}

	if (prev_key)
	{
		free(registration, M_RESERVATION);
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		return 0;
	}

	registration->i_prt = ctio->i_prt;
	registration->t_prt = ctio->t_prt;
	registration->r_prt = ctio->r_prt;
	registration->init_int = ctio->init_int;
	registration->key = service_action_key;
	if (ctio->init_int == TARGET_INT_ISCSI)
	{
		struct iscsi_priv *priv = &ctio->ccb_h.priv.ipriv;

		strcpy(registration->init_name, priv->init_name);
		debug_info("init name %s\n", registration->init_name);
	}
	SLIST_INSERT_HEAD(&reservation->registration_list, registration, r_list);
	reservation->generation++;
	return 0;
}

int
persistent_reservation_handle_register_and_ignore(struct tdevice *tdisk, struct qsio_scsiio *ctio)
{
	struct reservation *reservation = &tdisk->reservation;
	struct registration *tmp, *registration, *prev = NULL;
	struct reservation_parameter *param;
	uint64_t service_action_key;

	param = (struct reservation_parameter *)(ctio->data_ptr);
	service_action_key = be64toh(param->service_action_key);
	registration = zalloc(sizeof(struct registration), M_RESERVATION, Q_WAITOK);

	SLIST_FOREACH(tmp, &reservation->registration_list, r_list) {
		if (!iid_equal(tmp->i_prt, tmp->t_prt, tmp->init_int, ctio->i_prt, ctio->t_prt, ctio->init_int))
		{
			prev = tmp;
			continue;
		}

		if (!service_action_key)
		{
			if (prev)
				SLIST_REMOVE_AFTER(prev, r_list);
			else
				SLIST_REMOVE_HEAD(&reservation->registration_list, r_list);

			persistent_reservation_remove_registration(tdisk, tmp);
			free(tmp, M_RESERVATION);
		}
		else
		{
			tmp->key = service_action_key;
		}
		reservation->generation++;
		free(registration, M_RESERVATION);
		return 0;
	}

	if (!service_action_key)
	{
		reservation->generation++;
		free(registration, M_RESERVATION);
		return 0;
	}

	registration->key = service_action_key;
	registration->i_prt = ctio->i_prt;
	registration->t_prt = ctio->t_prt;
	registration->r_prt = ctio->r_prt;
	registration->init_int = ctio->init_int;
	if (ctio->init_int == TARGET_INT_ISCSI)
	{
		struct iscsi_priv *priv = &ctio->ccb_h.priv.ipriv;

		strcpy(registration->init_name, priv->init_name);
		debug_info("init name %s\n", registration->init_name);
	}
	SLIST_INSERT_HEAD(&reservation->registration_list, registration, r_list);
	reservation->generation++;
	return 0;
}

int
persistent_reservation_handle_reserve(struct tdevice *tdisk, struct qsio_scsiio *ctio)
{
	struct reservation *reservation = &tdisk->reservation;
	uint8_t *cdb = ctio->cdb;
	struct reservation_parameter *param;
	uint64_t key;
	int retval;
	uint8_t type;
	struct registration *registration;

	param = (struct reservation_parameter *)(ctio->data_ptr);
	key = be64toh(param->key);
	type = (cdb[2] & 0xF);

	debug_info("type %x key %llx\n", type, (unsigned long long)(key));

	retval = initiator_has_registered(key, ctio->i_prt, ctio->t_prt, ctio->init_int, &reservation->registration_list, &registration);
	if (!retval) {
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		return 0;
	}

	if (reservation->is_reserved && !is_persistent_reservation_holder(reservation, ctio->i_prt, ctio->t_prt, ctio->init_int)) {
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT; /* only one LU reservation */
		return 0;
	}

	if (reservation->is_reserved && (reservation->persistent_key != key || reservation->persistent_type != type))
	{
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT; /* only one LU reservation */
		return 0;
	}

	if (reservation->is_reserved) {
		return 0;
	}

	reservation->is_reserved = 1;
	reservation->type = RESERVATION_TYPE_PERSISTENT;
	reservation->persistent_type = type;
	reservation->persistent_key = key;
	reservation->i_prt = ctio->i_prt;
	reservation->t_prt = ctio->t_prt;
	reservation->r_prt = ctio->r_prt;
	reservation->init_int = ctio->init_int;
	return 0;
}

int
persistent_reservation_handle_release(struct tdevice *tdisk, struct qsio_scsiio *ctio)
{
	struct reservation *reservation = &tdisk->reservation;
	uint8_t *cdb = ctio->cdb;
	struct reservation_parameter *param;
	uint64_t key;
	uint8_t type;
	uint8_t scope;
	int retval;
	struct registration *registration;

	param = (struct reservation_parameter *)(ctio->data_ptr);
	key = be64toh(param->key);
	type = (cdb[2] & 0xF);
	scope = (cdb[2] >> 4);

	if (!reservation->is_reserved)
	{
		return 0;
	}

	retval = initiator_has_registered(key, ctio->i_prt, ctio->t_prt, ctio->init_int, &reservation->registration_list, &registration);
	if (!retval) {
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		return 0;
	}

	if (!iid_equal(reservation->i_prt, reservation->t_prt, reservation->init_int, ctio->i_prt, ctio->t_prt, ctio->init_int)) {
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		return 0;
	}

	if (reservation->persistent_key != key) {
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		return 0;
	}

	if (scope || (reservation->persistent_type != type))
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_RELEASE_OF_PERSISTENT_RESERVATION_ASC, INVALID_RELEASE_OF_PERSISTENT_RESERVATION_ASCQ);
		return 0;
	}

	if (multiple_registrants_reservation_type(reservation)) {
		registrants_unit_attention(tdisk, RESERVATIONS_RELEASED_ASC, RESERVATIONS_RELEASED_ASCQ, ctio->i_prt, ctio->t_prt, ctio->init_int, 1);
	}
	persistent_reservation_reset(reservation);
	return 0;
}

int
persistent_reservation_handle_clear(struct tdevice *tdisk, struct qsio_scsiio *ctio)
{
	struct reservation *reservation = &tdisk->reservation;
	struct reservation_parameter *param;
	uint64_t key;
	int retval;
	struct registration *registration;

	param = (struct reservation_parameter *)(ctio->data_ptr);
	key = be64toh(param->key);

	retval = initiator_has_registered(key, ctio->i_prt, ctio->t_prt, ctio->init_int, &reservation->registration_list, &registration);
	if (!retval) {
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		return 0;
	}

	registrants_unit_attention(tdisk, RESERVATIONS_PREEMPTED_ASC, RESERVATIONS_PREEMPTED_ASCQ, ctio->i_prt, ctio->t_prt, ctio->init_int, 1);
	persistent_reservation_reset(reservation);
	reservation->generation++;
	return 0;
}

static void
istate_abort_tasks_other_initiators(struct initiator_state *istate, uint64_t i_prt, uint64_t t_prt, uint8_t init_int)
{
	struct qsio_scsiio *iter;

	mtx_lock(istate->istate_lock);
	if (TAILQ_EMPTY(&istate->queue_list)) {
		mtx_unlock(istate->istate_lock);
		return;
	}

	TAILQ_FOREACH(iter, &istate->queue_list, ta_list) {
		if (iter->ccb_h.flags & QSIO_IN_DEVQ)
			continue;
		if (iter->i_prt == i_prt && iter->t_prt == t_prt && iter->init_int == init_int) {
			iter->ccb_h.flags |= QSIO_CTIO_ABORTED;
			iter->ccb_h.flags |= QSIO_SEND_ABORT_STATUS;
		}
	}
	mtx_unlock(istate->istate_lock);
}
 
static void
persistent_reservation_abort_tasks(struct tdevice *tdisk, uint64_t i_prt, uint64_t t_prt, uint16_t r_prt, int init_int)
{
	struct initiator_state *istate;

	istate = device_get_initiator_state(tdisk, i_prt, t_prt, r_prt, init_int, 0, 0);
	tdevice_reservation_lock(tdisk);
	if (istate)
		istate_abort_tasks_other_initiators(istate, i_prt, t_prt, init_int);
	tdevice_reservation_unlock(tdisk);
}

int
persistent_reservation_handle_preempt(struct tdevice *tdisk, struct qsio_scsiio *ctio, int abort)
{
	struct reservation *reservation = &tdisk->reservation;
	uint8_t *cdb = ctio->cdb;
	struct registration *tmp, *tvar, *prev = NULL;
	struct reservation_parameter *param;
	uint64_t key;
	uint64_t service_action_key;
	uint8_t type;
	int retval;
	int is_ar = 0;
	int send_sync = 0;

	param = (struct reservation_parameter *)(ctio->data_ptr);
	key = be64toh(param->key);
	service_action_key = be64toh(param->service_action_key);
	type = (cdb[2] & 0xF);

	retval = initiator_has_registered(key, ctio->i_prt, ctio->t_prt, ctio->init_int, &reservation->registration_list, NULL);
	if (!retval) {
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		return 0;
	}

	if (reservation->is_reserved && reservation->persistent_key != key)
	{
		ctio->scsi_status = SCSI_STATUS_RESERV_CONFLICT;
		return 0;
	}

	is_ar = in_persistent_ar_type(reservation);
	if (!is_ar && reservation->is_reserved && !service_action_key)
	{
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ILLEGAL_REQUEST, 0, INVALID_FIELD_IN_PARAMETER_LIST_ASC, INVALID_FIELD_IN_PARAMETER_LIST_ASCQ);
		return 0;
	}

	SLIST_FOREACH_SAFE(tmp, &reservation->registration_list, r_list, tvar) {
		if (iid_equal(tmp->i_prt, tmp->t_prt, tmp->init_int, ctio->i_prt, ctio->t_prt, ctio->init_int)) {
			prev = tmp;
			continue;
		}

		if (tmp->key != service_action_key && (!is_ar || service_action_key)) {
			prev = tmp;
			continue;
		}

		if (prev)
			SLIST_REMOVE_AFTER(prev, r_list);
		else
			SLIST_REMOVE_HEAD(&reservation->registration_list, r_list);

		if (abort)
			persistent_reservation_abort_tasks(tdisk, tmp->i_prt, tmp->t_prt, tmp->r_prt, tmp->init_int);

		registrant_unit_attention(tdisk, tmp, REGISTRATIONS_PREEMPTED_ASC, REGISTRATIONS_PREEMPTED_ASCQ);
		send_sync = 1;
		free(tmp, M_RESERVATION);
	}

	if ((reservation->is_reserved && !is_ar && (reservation->persistent_key == service_action_key)) || (is_ar && !service_action_key)) {
		reservation->type = type;
		reservation->i_prt = ctio->i_prt;
		reservation->t_prt = ctio->t_prt;
		reservation->r_prt = ctio->r_prt;
		reservation->init_int = ctio->init_int;
		reservation->persistent_key = key;
		reservation->persistent_type = type;
	}
	reservation->generation++;
	return 0;
}

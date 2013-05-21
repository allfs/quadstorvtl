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

#ifndef QS_RESERVATION_H_
#define QS_RESERVATION_H_

struct reservation_parameter {
	uint64_t key;
	uint64_t service_action_key;
	uint32_t scope;
	uint8_t  aptpl;
	uint8_t  rsvd;
	uint16_t extent;
} __attribute__ ((__packed__));

struct transport_id_common {
	uint8_t protocol_id;
	uint8_t rsvd;
	uint16_t addl_len;
} __attribute__ ((__packed__));

struct transport_id_fc {
	uint8_t protocol_id;
	uint8_t rsvd[7];
	uint8_t n_port_name[16];
} __attribute__ ((__packed__));

struct transport_id_scsi {
	uint8_t protocol_id;
	uint8_t rsvd;
	uint16_t scsi_address;
	uint16_t obs;
	uint16_t relative_tgt_port;
	uint8_t  rsvd1[16];
} __attribute__ ((__packed__));

struct registration {
	uint64_t key;
	uint64_t i_prt[2];
	uint64_t t_prt[2];
	uint16_t r_prt;
	uint8_t init_int;
	uint8_t pad[1];
	SLIST_ENTRY(registration) r_list;
	char init_name[256];
};

struct reservation_capabilities {
	uint16_t length;
	uint8_t  ptpl_c;
	uint8_t  tmv;
	uint8_t  tmask1;
	uint8_t  tmask2;
	uint8_t  rsvd1;
	uint8_t  rsvd2;
} __attribute__ ((__packed__));

struct reservation {
	uint32_t  generation;
	uint8_t  is_reserved : 1;
	uint8_t  type : 3;
	uint8_t  persistent_type : 4;
	uint8_t  init_int;
	uint16_t  r_prt;
	uint64_t  i_prt[2];
	uint64_t  t_prt[2];
	uint64_t  persistent_key;
	SLIST_HEAD(registration_list, registration) registration_list;
};

struct pfull_data {
	uint64_t key;
	uint32_t rsvd;
	uint8_t  r_holder;
	uint8_t  type;
	uint32_t rsvd2;
	uint16_t rel_tgt_port;
	uint32_t addl_len;
	uint8_t  transport_id[0];
} __attribute__ ((__packed__));

struct pin_data {
	uint64_t key;
	uint32_t scope;
	uint8_t  rsvd;
	uint8_t  type;
	uint16_t obsolete;
} __attribute__ ((__packed__));

struct qsio_scsiio;

static inline void
port_fill(uint64_t dest[], uint64_t src[])
{
	dest[0] = src[0];
	dest[1] = src[1];
}

static inline int
port_equal(uint64_t port1[], uint64_t port2[])
{
	return (port1[0] == port2[0] && port1[1] == port2[1]);
}

static inline int
device_reserved(struct qsio_scsiio *ctio, struct reservation *reservation)
{
	if (!reservation->is_reserved || (port_equal(reservation->i_prt, ctio->i_prt) && port_equal(reservation->t_prt, ctio->t_prt)))
		return 0;
	else {
		debug_info("reservation i_prt %llu:%llu ctio i_prt %llu:%llu reservation t_prt %llu ctio t_prt %llu reservation init_int %d ctio init_int %d\n", reservation->i_prt[0], reservation->i_prt[1], ctio->i_prt[0], ctio->i_prt[1], reservation->t_prt, ctio->t_prt, reservation->init_int, ctio->init_int);
		return 1;
	}
}

int persistent_reservation_read_capabilities(struct qsio_scsiio *ctio, uint16_t allocation_length);
int persistent_reservation_read_keys(struct qsio_scsiio *ctio, uint16_t allocation_length, struct reservation *reservation);
int persistent_reservation_read_reservations(struct qsio_scsiio *ctio, uint16_t allocation_length, struct reservation *reservation);
int persistent_reservation_read_full(struct qsio_scsiio *ctio, uint16_t allocation_length, struct reservation *reservation);
void persistent_reservation_clear(struct registration_list *lhead);
int persistent_reservation_handle_register(struct tdevice *tdisk, struct qsio_scsiio *ctio);
int persistent_reservation_handle_register_and_ignore(struct tdevice *tdisk, struct qsio_scsiio *ctio);
int persistent_reservation_handle_reserve(struct tdevice *tdisk, struct qsio_scsiio *ctio);
int persistent_reservation_handle_release(struct tdevice *tdisk, struct qsio_scsiio *ctio);
int persistent_reservation_handle_clear(struct tdevice *tdisk, struct qsio_scsiio *ctio);
int persistent_reservation_handle_preempt(struct tdevice *tdisk, struct qsio_scsiio *ctio, int abort);
#endif

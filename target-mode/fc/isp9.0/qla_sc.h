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

#ifndef QUADSTOR_QLA_SC_H_
#define QUADSTOR_QLA_SC_H_

#include <bsddefs.h>
#include <exportdefs.h>
#include <fcq.h>
#include "isp_freebsd.h" 

void qla_sc_detach_fcbridge(void);
void qla_end_ccb(void *ccb_void);
void qla_end_ctio_ccb(struct qsio_scsiio *ctio);
void fcbridge_intr_insert(struct fcbridge *fcbridge, struct qsio_hdr *ccb_h);
void fcbridge_intr_remove(struct fcbridge *fcbridge, struct qsio_hdr *ccb_h);
void __local_ctio_free_all(struct qsio_scsiio *ctio);
void __ctio_free_data(struct qsio_scsiio *ctio);
void __ctio_free_all(struct qsio_scsiio *ctio, int local_pool);
struct qsio_scsiio * __local_ctio_new(allocflags_t flags);
int __ctio_queue_cmd(struct qsio_scsiio *ctio);
void fcbridge_free_initiator(uint64_t i_prt, uint64_t t_prt);

MALLOC_DECLARE(M_QISP);
#endif

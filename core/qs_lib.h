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

#ifndef QUADSTOR_LIB_H_
#define QUADSTOR_LIB_H_

#include "coredefs.h"

enum {
	BIO_META_ERROR,
};

struct bio_meta {
	unsigned long flags;
	wait_compl_t *completion;
};

static inline void
bio_meta_destroy(struct bio_meta *bio_meta)
{
	wait_completion_free(bio_meta->completion);
}

static inline void
bio_meta_init(struct bio_meta *bio_meta)
{
	bzero(bio_meta, sizeof(*bio_meta));
	bio_meta->completion = wait_completion_alloc("bio meta compl");
}

static inline void
wait_for_bio_meta(struct bio_meta *bio_meta)
{
	wait_for_done(bio_meta->completion);
}

struct bdevint;
int qs_lib_bio_page(struct bdevint *bint, uint64_t b_start, uint32_t size, pagestruct_t *page, void *end_io, void *priv, int rw, int type);
int qs_lib_bio_lba(struct bdevint *bint, uint64_t b_start, pagestruct_t *page, int rw, int type);

#endif

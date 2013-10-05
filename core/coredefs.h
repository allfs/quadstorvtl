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

#ifndef IO_H_
#define IO_H_

#ifdef FREEBSD 
#include "corebsd.h"
#else
#include "coreext.h"
#endif
#include "rawdefs.h"
#include <exportdefs.h>

enum {
	META_IO_PENDING,
	META_DATA_DIRTY,
	META_DATA_ERROR,
	META_IO_READ_PENDING,
	META_DATA_READ_DIRTY,
	META_DATA_CLONED,
	META_LOAD_DONE,
	META_CSUM_CHECK_DONE,
	META_DATA_NEW,
	META_DATA_LOADED,
	DATA_WRITE_PENDING,
	BINT_ALLOC_INSERTED,
	CACHE_DATA_ERROR,
};

enum {
	TCACHE_IO_FLUSH,
	TCACHE_LOG_WRITE,
	TCACHE_IO_ERROR,
	TCACHE_IO_SUBMITTED,
};

static inline uint64_t
align_size(uint64_t size, int boundary)
{
	uint64_t adjust;

	adjust = (size + (boundary - 1)) & -(boundary);
	return (adjust);
}

static inline uint16_t
net_calc_csum16(uint8_t *buf, int len)
{
        int i;
        uint16_t csum = 0;

        for (i = 0; i < len ; i+=sizeof(uint16_t))
        {
                uint16_t val = *((uint16_t *)(buf+i));
                csum += val;
        }
        return ~(csum);
}

static inline uint16_t
calc_csum16(uint8_t *buf, int len)
{
        int i;
        uint64_t csum = 0;

        for (i = 0; i < len ; i+=8)
        {
                uint64_t val = *((uint64_t *)(buf+i));
                csum += val;
        }
        return ~(csum);
}

static inline uint64_t
calc_csum(uint8_t *buf, int len)
{
        int i;
        uint64_t csum = 0;

        for (i = 0; i < len ; i+=8)
        {
                uint64_t val = *((uint64_t *)(buf+i));
                csum ^= val;
        }
        return (csum);
}

extern uma_t *ctio_cache;
extern uma_t *pgdata_cache;
extern uma_t *tcache_cache;
#ifdef FREEBSD
extern uma_t *biot_cache;
extern uma_t *biot_page_cache;
#endif
extern mtx_t *glbl_lock;
extern sx_t *gchain_lock;

struct index_info;
struct ddblock_info;
struct log_page;
struct amap_table;
struct amap;

STAILQ_HEAD(pgdata_wlist, pgdata);
struct pgdata {
	pagestruct_t *page;
	uint16_t pg_len;
	uint16_t pg_offset;
	int flags;
	pagestruct_t *verify_page;
	STAILQ_ENTRY(pgdata) w_list;
	wait_compl_t *completion;
};

static inline void
pgdata_free_page(struct pgdata *pgdata)
{
	if (pgdata->page) {
		vm_pg_free(pgdata->page);
		pgdata->page = NULL;
	}
}

static inline int
pgdata_alloc_page(struct pgdata *pgdata, allocflags_t flags)
{
	if (pgdata->page)
		return 0;

	pgdata->page = vm_pg_alloc(flags);
	if (unlikely(!pgdata->page))
		return -1;
	return 0;
}

static inline void
pgdata_add_page_ref(struct pgdata *dest, pagestruct_t *page)
{
	vm_pg_ref(page);
	dest->page = page;
}

static inline void
pgdata_add_ref(struct pgdata *dest, struct pgdata *src)
{
	pgdata_add_page_ref(dest, src->page);
	dest->pg_len = src->pg_len;
}

static inline void
pgdata_free(struct pgdata *pgdata)
{
	pgdata_free_page(pgdata);
	debug_check(!pgdata->completion);
	wait_completion_free(pgdata->completion);
	uma_zfree(pgdata_cache, pgdata);
}

static inline void
pglist_free(struct pgdata **pglist, int pglist_cnt)
{
	int i;

	for (i = 0; i < pglist_cnt; i++)
		pgdata_free(pglist[i]);
	free(pglist, M_PGLIST);
}

#include "../export/qsio_ccb.h"

int pgdata_allocate_data(struct qsio_scsiio *ctio, uint32_t block_size, uint32_t num_blocks, allocflags_t flags);

static inline void
map_pglist_pages(struct pgdata **pglist, int pglist_cnt, pagestruct_t **pages)
{
	struct pgdata *pgdata;
	int i;

	for (i = 0; i < pglist_cnt; i++) {
		pgdata = pglist[i];
		pages[i] = pgdata->page;
	}
}

static inline int
pgdata_get_count(uint32_t block_size, uint32_t num_blocks)
{
	int pglist_cnt;

	if (!(block_size & LBA_MASK))
		pglist_cnt = ((block_size >> LBA_SHIFT)) * num_blocks;
	else
		pglist_cnt = ((block_size >> LBA_SHIFT) + 1) * num_blocks;

	return pglist_cnt;
}

static inline struct pgdata **
pgdata_allocate_pglist(int nsegs, allocflags_t flags)
{
	struct pgdata **pglist;

	pglist = zalloc(sizeof(struct pgdata *) * nsegs, M_PGLIST, flags);
	if (unlikely(!pglist)) {
		debug_warn("Allocation failure nsegs %d, flags %u\n", nsegs, flags);
		return NULL;
	}

	return pglist;
}

static inline struct pgdata **
pgdata_allocate(uint32_t block_size, uint32_t num_blocks, int *ret_pglist_cnt, allocflags_t flags, int alloc_page)
{
	struct pgdata **pglist, *pgtmp;
	int remaining;
	int pglist_cnt;
	int i, retval, idx;
	
	*ret_pglist_cnt = pglist_cnt = pgdata_get_count(block_size, num_blocks);
	debug_check(!pglist_cnt);

	pglist = pgdata_allocate_pglist(pglist_cnt, flags);
	if (unlikely(!pglist)) {
		debug_warn("allocating pglist for count %d failed\n", pglist_cnt);
		return NULL;
	}

	idx = 0;
	for (i = 0; i < num_blocks; i++) {
		for (remaining = block_size; remaining > 0; remaining -= LBA_SIZE) {
			pgtmp = __uma_zalloc(pgdata_cache, flags | Q_ZERO, sizeof(*pgtmp));
			if (unlikely(!pgtmp)) {
				pglist_free(pglist, idx);
				return NULL;
			}
			pgtmp->completion = wait_completion_alloc("pgdata compl"); 

			pglist[idx] = pgtmp;
			idx++;
			if (!alloc_page)
				continue;

			pgtmp->pg_len = (remaining > LBA_SIZE) ? LBA_SIZE : remaining;
			retval = pgdata_alloc_page(pgtmp, Q_SFBUF);
			if (unlikely(retval != 0)) {
				debug_warn("allocating for pgdata page failed\n");
				pglist_free(pglist, idx);
				return NULL;
			}
		}
	}
	return pglist;
}

void ctio_free_all(struct qsio_scsiio *ctio);

static inline void
ctio_free_data(struct qsio_scsiio *ctio)
{
	if (!ctio->dxfer_len)
		return;

	if (ctio->pglist_cnt)
	{
		pglist_free((void *)ctio->data_ptr, ctio->pglist_cnt);
	}
	else
	{
		free(ctio->data_ptr, M_CTIODATA);
	}
	ctio->data_ptr = NULL;
	ctio->dxfer_len = 0;
	ctio->pglist_cnt = 0;
}

static inline struct qsio_scsiio *
ctio_new(allocflags_t flags)
{
	return __uma_zalloc(ctio_cache, flags | Q_ZERO, sizeof(struct qsio_scsiio)); 
}

enum {
	COMP_ALG_LZF	= 0x00,
	COMP_ALG_LZ4	= 0x01,
};

#define COMP_ALG_SHIFT	28	
#define COMP_ALG_MASK	((1 << COMP_ALG_SHIFT) - 1)

#define SET_COMP_SIZE(ptr, csize, alg)	(*((uint32_t *)(ptr)) = (csize | (alg << COMP_ALG_SHIFT)))

int qs_deflate_block(uint8_t *uncomp_addr, int uncomp_len, uint8_t *comp_addr, int *comp_size, void *wrkmem, int algo);
int qs_inflate_block(uint8_t *comp_addr, int comp_len, uint8_t *uncomp_addr, int uncomp_len);

/* timeouts */
extern int stale_initiator_timeout;
extern atomic_t kern_inited;
extern atomic_t mdaemon_load_done;

static inline uint32_t
get_elapsed(uint32_t timestamp)
{
	uint32_t cur_ticks = ticks;

	if (cur_ticks >= timestamp)
		return (cur_ticks - timestamp);
	else
		return (timestamp - cur_ticks);
}

#define wait_on_chan_check(chn, condition)			\
do {								\
	if (!(condition))					\
		wait_on_chan(chn, condition);			\
} while (0)

#define wait_on_chan_interruptible_check(chn, condition)	\
do {								\
	if (!(condition))					\
		wait_on_chan_interruptible(chn, condition);	\
} while (0)

#define debug_warn_notify(fmt,args...)								\
do {													\
	char notifybuf[64];										\
	snprintf(notifybuf, sizeof(notifybuf) -1 , "%s:%d " fmt, __FUNCTION__, __LINE__, ##args);	\
	debug_warn(fmt,##args);										\
	node_usr_notify_msg(USR_NOTIFY_WARN, 0, notifybuf);						\
} while (0)

#define debug_error_notify(fmt,args...)								\
do {													\
	char notifybuf[64];										\
	snprintf(notifybuf, sizeof(notifybuf) -1 , "%s:%d " fmt, __FUNCTION__, __LINE__, ##args);	\
	debug_warn(fmt,##args);										\
	node_usr_notify_msg(USR_NOTIFY_ERR, 0, notifybuf);						\
} while (0)

#define debug_info_notify(fmt,args...)								\
do {													\
	char notifybuf[64];										\
	snprintf(notifybuf, sizeof(notifybuf) -1 , "%s:%d " fmt, __FUNCTION__, __LINE__, ##args);	\
	node_usr_notify_msg(USR_NOTIFY_INFO, 0, notifybuf);						\
} while (0)

#ifdef FREEBSD 
#define CREATE_CACHE(vr,nm,s)                           \
do {                                                    \
	vr = uma_zcreate(nm,s, NULL, NULL, NULL, NULL, 0, 0);   \
} while(0);
#else
#define CREATE_CACHE(vr,nm,s)                           \
do {                                                    \
	vr = uma_zcreate(nm,s);                 \
} while(0);
#endif

static inline void print_buffer(char *buf, int len)
{
	int i;
	unsigned char c;
	for (i = 0; i < len; i++) {
		if (i && (i % 16) == 0)
			printf("\n");
		if (!i || (i % 16) == 0)
			printf("%02d: ", i);
		printf("%02x ", buf[i] & 0xFF);
	}
	printf("\n");

	for (i = 0; i < len; i++) {
		if (i && (i % 16) == 0)
			printf("\n");
		if (!i || (i % 16) == 0)
			printf("%02d: ", i);
		c = buf[i];
		if (c > 0x20 && c < 0x7B)
			printf("%c ", c);
		else
			printf(". ");
	}
	printf("\n");
}
#endif /* IO_H_ */

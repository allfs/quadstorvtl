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
#include "tcache.h"
#include "bdev.h"

MALLOC_DEFINE(M_QUADSTOR, "quads", "QUADStor allocations");
MALLOC_DEFINE(M_UNMAP, "quad unmap", "QUADStor allocations");
MALLOC_DEFINE(M_TSEGMENT, "quad tsegment", "QUADStor allocations");
MALLOC_DEFINE(M_BLKENTRY, "quad blkentry", "QUADStor allocations");
MALLOC_DEFINE(M_QCACHE, "quad qcache", "QUADStor allocations");
MALLOC_DEFINE(M_DRIVE, "quad drive", "QUADStor allocations");
MALLOC_DEFINE(M_TMAPS, "quad tmaps", "QUADStor allocations");
MALLOC_DEFINE(M_MCHANGERELEMENT, "quad mchanger element", "QUADStor allocations");
MALLOC_DEFINE(M_MCHANGER, "quad mchanger", "QUADStor allocations");
MALLOC_DEFINE(M_SUPERBLK, "quad superblk", "QUADStor allocations");
MALLOC_DEFINE(M_TCACHE, "quad tcache", "QUADStor allocations");
MALLOC_DEFINE(M_CBS, "quad cbs", "QUADStor allocations");
MALLOC_DEFINE(M_PGLIST, "quad pg list", "QUADStor allocations");
MALLOC_DEFINE(M_SENSEINFO, "quad sense info", "QUADStor allocations");
MALLOC_DEFINE(M_CTIODATA, "quad ctio data", "QUADStor allocations");
MALLOC_DEFINE(M_BINDEX, "quad bindex", "QUADStor bindex allocations");
MALLOC_DEFINE(M_BINT, "quad bint", "QUADStor bint allocations");
MALLOC_DEFINE(M_BDEVGROUP, "quad bdevgroup", "QUADStor bdevgroup allocations");
MALLOC_DEFINE(M_BIOMETA, "quad biometa", "QUADStor biometa allocations");
MALLOC_DEFINE(M_RESERVATION, "quad reservation", "QUADStor reservation allocations");
MALLOC_DEFINE(M_DEVQ, "quad devq", "QUADStor devq allocations");
MALLOC_DEFINE(M_GDEVQ, "quad gdevq", "QUADStor gdevq allocations");
MALLOC_DEFINE(M_WRKMEM, "quad wrkmem", "QUADStor wrkmem allocations");
int
tcache_need_new_bio(struct tcache *tcache, struct biot *biot, uint64_t b_start, struct bdevint *bint, int stat)
{
	if (biot->bint != bint) {
		return 1;
	}

	if ((biot->b_start + (biot->dxfer_len >> bint->sector_shift)) != b_start) {
		debug_info("biot b_start %llu biot dxfer len %d b_start %llu\n", (unsigned long long)biot->b_start, biot->dxfer_len, (unsigned long long)b_start);
		return 1;
	}
	else {
		if (biot->dxfer_len & LBA_MASK)
			return 1;
		return 0;
	}
}

struct biot *
biot_alloc(struct bdevint *bint, uint64_t b_start, void *cache)
{
	struct biot *biot;

	biot = __uma_zalloc(biot_cache, Q_NOWAIT | Q_ZERO, sizeof(*biot));
	if (unlikely(!biot)) {
		debug_warn("Slab allocation failure\n");
		return NULL;
	}

	biot->pages = __uma_zalloc(biot_page_cache, Q_NOWAIT, ((MAXPHYS >> LBA_SHIFT) * sizeof(pagestruct_t *)));
	if (unlikely(!biot->pages)) {
		debug_warn("Slab allocation failure\n");
		uma_zfree(biot_cache, biot);
		return NULL;
	}

	biot->bint = bint;
	biot->b_start = b_start;
	biot->cache = cache;
	biot->max_pages = (MAXPHYS >> LBA_SHIFT);

	return biot;
}

struct bio *
bio_get_new(struct bdevint *bint, void *end_bio_func, void *consumer, uint64_t b_start, int bio_vec_count, int rw)
{
	struct bio *bio;

	bio = g_alloc_bio();
	bio->bio_offset = b_start << bint->sector_shift;
	bio->bio_done = end_bio_func;
	bio->bio_caller1 = consumer;
	bio_set_command(bio, rw);
	return bio;
}

int
biot_add_page(struct biot *biot, pagestruct_t *page, int pg_length)
{
	if ((biot->dxfer_len + pg_length) > MAXPHYS || (biot->page_count == biot->max_pages))
		return 0;

	biot->pages[biot->page_count] = page;
	biot->page_count++;
	biot->dxfer_len += pg_length;
	return pg_length;
}

int
bdev_unmap_support(iodev_t *iodev)
{
	return 1;
}

int
bio_unmap(iodev_t *iodev, void *cp, uint64_t block, uint32_t blocks, uint32_t shift, void *callback, void *priv)
{
	struct bio *bio;

	bio = g_alloc_bio();
	bio->bio_cmd = BIO_DELETE;
	bio->bio_offset = block << shift;
	bio->bio_done = callback;
	bio->bio_caller1 = priv;
	bio->bio_length = blocks << shift;
	bio->bio_bcount = bio->bio_length;
	g_io_request(bio, cp);
	return 0;
}
  
void
vm_pg_unmap(void *maddr, int page_count)
{
	vm_offset_t pbase = (vm_offset_t)maddr;

	pmap_qremove(pbase, page_count);
	kmem_free(kernel_map, pbase, page_count * PAGE_SIZE);
}

void*
vm_pg_map(pagestruct_t **pages, int page_count)
{
	vm_offset_t pbase;

	while (!(pbase = kmem_alloc_nofault(kernel_map, PAGE_SIZE * page_count)))
		pause("psg", 10);

	pmap_qenter(pbase, pages, page_count);
	return (void *)pbase;
}

void
send_biot(struct biot *biot, int rw, void *endfn)
{
	struct bio *bio;

	biot->pbase = (vm_offset_t)vm_pg_map(biot->pages, biot->page_count);
	bio = bio_get_new(biot->bint, endfn, biot, biot->b_start, 1, rw);
	bio->bio_data = (caddr_t)(biot->pbase);
	bio->bio_length = biot->dxfer_len;
	bio->bio_bcount = bio->bio_length;
	biot->bio = bio;
	g_io_request(bio, biot->bint->cp);
}

void
g_destroy_biot(struct biot *biot)
{
	if (biot->pbase)
		vm_pg_unmap((void *)biot->pbase, biot->page_count);

	if (biot->pages)
		uma_zfree(biot_page_cache, biot->pages);

	if (biot->bio)
		g_destroy_bio(biot->bio);

	uma_zfree(biot_cache, biot);
}

int
bio_get_command(struct bio *bio)
{
	if (bio->bio_cmd == BIO_READ)
		return QS_IO_READ;
	else
		return QS_IO_WRITE;
}

void
bio_set_command(struct bio *bio, int cmd)
{
	switch (cmd) {
	case QS_IO_READ:
		bio->bio_cmd = BIO_READ;
		break;
	case QS_IO_WRITE:
		bio->bio_cmd = BIO_WRITE;
		break;
	default:
		debug_check(1);
	}
}

void
__sched_prio(struct thread *thr, int prio)
{
	int set_prio = 0;

	switch (prio) {
	case QS_PRIO_SWP:
		set_prio = PSWP;
		break;
	case QS_PRIO_INOD:
		set_prio = PINOD;
		break;
	default:
		debug_check(1);
	}
	thread_lock(thr);
	sched_prio(thr, set_prio);
	thread_unlock(thr);
}

void kern_panic(char *msg)
{
	debug_check(1);
	panic(msg);
}
#define kern_panic	panic

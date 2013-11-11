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
#include "tape.h"
#include "bdev.h"
#include "mchanger.h"
#include "bdevgroup.h"
#include "qs_lib.h"
#include "tcache.h"

#ifdef FREEBSD
struct g_class bdev_vdev_class = {
	.name = "QSTOR::VDEV",
	.version = G_VERSION,
};
#endif

struct bdevint *bint_list[TL_MAX_DISKS];

static void
bdev_alloc_list_insert(struct bdevint *bint)
{
	struct bdevint *iter, *prev = NULL;
	struct bdevgroup *group = bint->group;

	sx_xlock(group->alloc_lock);
	bint_lock(bint);
	if (atomic_test_bit(BINT_ALLOC_INSERTED, &bint->flags)) {
		bint_unlock(bint);
		sx_xunlock(group->alloc_lock);
		return;
	}

	SLIST_FOREACH(iter, &group->alloc_list, a_list) {
		if (iter->free < bint->free)
			break;
		prev = iter;
	}
	if (prev)
		SLIST_INSERT_AFTER(prev, bint, a_list);
	else
		SLIST_INSERT_HEAD(&group->alloc_list, bint, a_list);
	atomic_set_bit(BINT_ALLOC_INSERTED, &bint->flags);
	bint_unlock(bint);
	sx_xunlock(group->alloc_lock);
}

static void
bdev_remove_from_alloc_list(struct bdevint *bint)
{
	struct bdevgroup *group = bint->group;

	bint_lock(bint);
	if (bint == group->eligible)
		group->eligible = NULL;

	if (!atomic_test_bit(BINT_ALLOC_INSERTED, &bint->flags)) {
		bint_unlock(bint);
		return;
	}
	SLIST_REMOVE(&group->alloc_list, bint, bdevint, a_list);
	atomic_clear_bit(BINT_ALLOC_INSERTED, &bint->flags);
	bint_unlock(bint);
}

void 
bint_decr_free(struct bdevint *bint, uint64_t used)
{
	bint->free -= used;
}

void 
bint_incr_free(struct bdevint *bint, uint64_t freed)
{
	bint->free += freed;
}

static uint64_t
bint_index_meta_offset(struct bdevint *bint)
{
	if (bint_is_group_master(bint))
		return INDEX_META_OFFSET_MASTER;
	else
		return INDEX_META_OFFSET_NORMAL;
}

uint64_t
bint_index_bstart(struct bdevint *bint, int index)
{
	uint64_t b_start;
	uint32_t blocks;
	uint64_t meta_offset = bint_index_meta_offset(bint);

	b_start = meta_offset >> bint->sector_shift;
	blocks = LBA_SIZE >> bint->sector_shift;
	b_start += (index * blocks);
	return b_start;
}

static void 
bint_index_write_csum(struct bintindex *index)
{
	struct raw_bintindex *raw_index;
	uint16_t csum;

	raw_index = (struct raw_bintindex *)(vm_pg_address(index->metadata) + (LBA_SIZE - sizeof(*raw_index)));
	csum = net_calc_csum16(vm_pg_address(index->metadata), LBA_SIZE - sizeof(*raw_index));
	raw_index->csum = csum;
}

static int
bint_index_validate(struct bintindex *index)
{
	struct raw_bintindex *raw_index;
	uint16_t csum;

	raw_index = (struct raw_bintindex *)(vm_pg_address(index->metadata) + (LBA_SIZE - sizeof(*raw_index)));
	csum = net_calc_csum16(vm_pg_address(index->metadata), LBA_SIZE - sizeof(*raw_index));
	if (raw_index->csum != csum) {
		debug_warn("Mismatch in csum got %x stored %x\n", csum, raw_index->csum);
		return -1;
	}
	return 0;
}

int
bint_index_io(struct bdevint *bint, struct bintindex *index, int rw)
{
	int retval;

	if (rw != QS_IO_READ)
		bint_index_write_csum(index);

	retval = qs_lib_bio_lba(bint, index->b_start, index->metadata, rw, 0);
	return retval;
}

void
bint_index_free(struct bintindex *index)
{
	wait_on_chan(index->index_wait, TAILQ_EMPTY(&index->unmap_list));
	if (index->metadata)
		vm_pg_free(index->metadata);
	wait_chan_free(index->index_wait);
	free(index, M_BINDEX);
}

static struct bintindex *
bint_index_new(struct bdevint *bint, int index_id)
{
	int retval;
	struct bintindex *index;

	index = zalloc(sizeof(struct bintindex), M_BINDEX, Q_WAITOK);
	index->metadata = vm_pg_alloc(VM_ALLOC_ZERO);
	if (unlikely(!index->metadata)) {
		debug_warn("Unable to allocate index's metadata\n");
		free(index, M_BINDEX);
		return NULL;
	}

	TAILQ_INIT(&index->unmap_list);
	index->index_wait = wait_chan_alloc("bint index wait");
	index->b_start = bint_index_bstart(bint, index_id);
	index->index_id = index_id;
	index->bint = bint;

	retval = bint_index_io(bint, index, QS_IO_SYNC);
	if (unlikely(retval != 0)) {
		debug_warn("Failed to read index data\n");
		bint_index_free(index);
		return NULL;
	}

	return index;
}

static struct bintindex *
bint_index_load(struct bdevint *bint, int index_id)
{
	int retval;
	struct bintindex *index;

	index = zalloc(sizeof(struct bintindex), M_BINDEX, Q_WAITOK);
	index->metadata = vm_pg_alloc(0);
	if (unlikely(!index->metadata)) {
		debug_warn("Unable to allocate index's metadata\n");
		free(index, M_BINDEX);
		return NULL;
	}

	TAILQ_INIT(&index->unmap_list);
	index->index_wait = wait_chan_alloc("bint index wait");
	index->b_start = bint_index_bstart(bint, index_id);
	index->index_id = index_id;
	index->bint = bint;

	retval = bint_index_io(bint, index, QS_IO_READ);
	if (unlikely(retval != 0)) {
		debug_warn("Failed to read index data\n");
		bint_index_free(index);
		return NULL;
	}

	retval = bint_index_validate(index);
	if (unlikely(retval != 0)) {
		bint_index_free(index);
		return NULL;
	}

	return index;
}

static void
bint_index_insert(struct bdevint *bint, struct bintindex *index)
{
	struct bintindex *tmp;
	unsigned long flags;
	int unmap_done;

	if (bint->index_count < BINT_MAX_INDEX_COUNT || STAILQ_EMPTY(&bint->index_list)) {
		bint->index_count++;
		STAILQ_INSERT_TAIL(&bint->index_list, index, i_list);
		return;
	}

	tmp = STAILQ_FIRST(&bint->index_list);
	chan_lock_intr(tmp->index_wait, &flags);
	unmap_done = TAILQ_EMPTY(&tmp->unmap_list);
	chan_unlock_intr(tmp->index_wait, &flags);
	if (unmap_done) {
		STAILQ_REMOVE_HEAD(&bint->index_list, i_list);
		bint_index_free(tmp);
	}
	STAILQ_INSERT_TAIL(&bint->index_list, index, i_list);
}

static void
bint_index_free_all(struct bdevint *bint)
{
	struct bintindex *index;

	while ((index = STAILQ_FIRST(&bint->index_list)) != NULL) {
		STAILQ_REMOVE_HEAD(&bint->index_list, i_list);
		bint_index_free(index);
	}
	bint->index_count = 0;
}

#ifdef FREEBSD
static void
bint_dev_close(struct bdevint *bint)
{
	int flags = FREAD | FWRITE;
	
	if (bint->cp) {
		struct g_geom *gp;

		g_topology_lock();
 		gp = bint->cp->geom;
		g_access(bint->cp, -1, -1, 0);
		g_detach(bint->cp);
		g_destroy_consumer(bint->cp);
		g_destroy_geom(gp);
		g_topology_unlock();
	}

	if (bint->b_dev) {
		int vfslocked;

		vfslocked = VFS_LOCK_GIANT(bint->b_dev->v_mount);
		(void)vn_close(bint->b_dev, flags, NOCRED, curthread);
		VFS_UNLOCK_GIANT(vfslocked);
	}
}
#else
static void
bint_dev_close(struct bdevint *bint)
{
	(*kcbs.close_block_device)(bint->b_dev);
}
#endif

static int 
bint_free(struct bdevint *bint, int free_alloc)
{
	pagestruct_t *page;
	int retval;

	if (!free_alloc)	
		goto skip;

	page = vm_pg_alloc(VM_ALLOC_ZERO);
	if (unlikely(!page)) {
		debug_warn("Page allocation failure\n");
		return -1;
	}

	retval = qs_lib_bio_lba(bint, bint->b_start, page, QS_IO_SYNC, 0);
	vm_pg_free(page);
	if (unlikely(retval != 0))
		return -1;

skip:
	if (bint->b_dev)
		bint_dev_close(bint);

	bint_index_free_all(bint);
	sx_free(bint->bint_lock);
	free(bint, M_BINT);
	return 0;
}

struct bintindex *
bint_get_index(struct bdevint *bint, int index_id)
{
	struct bintindex *index;

	STAILQ_FOREACH(index, &bint->index_list, i_list) {
		if (index->index_id == index_id)
			return index;
	}

	index = bint_index_load(bint, index_id);
	if (unlikely(!index))
		return NULL;

	bint_index_insert(bint, index);
	return index;
}

static uint64_t
bint_index_free_blocks(struct bdevint *bint, struct bintindex *index)
{
	int i, j, bmap_entries, nindexes;
	uint8_t *bmap;
	uint64_t free = 0, block, end;
	int index_id = index->index_id;

	nindexes = bint_nindexes(bint->usize);

	bmap = (uint8_t *)vm_pg_address(index->metadata);
	bmap_entries = calc_bmap_entries(bint, index_id);
	for (i = 0; i < bmap_entries; i++) {
		uint8_t val = bmap[i];

		if (val == 0xFF)
			continue;

		j = get_iter_start(index_id, i);
		for (; j < 8; j++) {
			if (((index_id + 1) == nindexes) && ((i + 1) == bmap_entries)) {
				block = calc_alloc_block(bint, index_id, i, j);
				end = block + (BINT_UNIT_SIZE >> bint->sector_shift);
				if (end > bint->b_end)
					break;
			}

			if (!(val & (1 << j)))
				free++;
		}
	}
	return free;
}

static int
bint_index_unmap_done(struct bintindex *index, int entry, int pos, int wait)
{
	unsigned long flags;
	struct bintunmap *unmap;

again:
	chan_lock_intr(index->index_wait, &flags);
	TAILQ_FOREACH(unmap, &index->unmap_list, u_list) {
		if (unmap->entry == entry && unmap->pos == pos) {
			chan_unlock_intr(index->index_wait, &flags);
			if (!wait)
				return 0;
			pause("unmap wt", 100);
			goto again;
		}
	}
	chan_unlock_intr(index->index_wait, &flags);
	return 1;
}

static uint64_t
bint_get_block(struct bdevint *bint, struct bintindex *index, uint64_t *b_end)
{
	int i, j;
	uint64_t block;
	uint64_t end;
	int index_id;
	uint8_t *bmap;
	int retval, bmap_entries;
	int wait = 0;
	int retry;

	debug_check(!index);
	index_id = index->index_id;
	bmap = (uint8_t *)(vm_pg_address(index->metadata));

	bmap_entries = calc_bmap_entries(bint, index_id);
again:
	retry = 0;
	for (i = 0; i < bmap_entries; i++) {
		uint8_t val = bmap[i];

		if (val == 0xFF)
			continue;

		j = get_iter_start(index_id, i);
		for (; j < 8; j++) {
			if (!(val & (1 << j))) {
				retval = bint_index_unmap_done(index, i, j, wait);
				if (retval) {
					bmap[i] |= (1 << j);
					goto found;
				}
				debug_check(wait);
				retry = 1;
			}
		}
	}

	if (retry && !wait) {
		wait = 1;
		goto again;
	}

	return 0;
found:
	block = calc_alloc_block(bint, index_id, i, j);
	end = block + (BINT_UNIT_SIZE >> bint->sector_shift);
	if (end > bint->b_end) {
		bmap[i] &= ~(1 << j);
		return 0ULL;
	}

	retval = bint_index_io(bint, index, QS_IO_SYNC);
	if (unlikely(retval != 0)) {
		bmap[i] &= ~(1 << j);
		debug_warn("index sync failed for index_id %d bid %u\n", index->index_id, bint->bid);
		return 0ULL;
	}

	*b_end = end;
	debug_check(bint->free < BINT_UNIT_SIZE);
	bint_decr_free(bint, BINT_UNIT_SIZE);
	return block;
}

static uint64_t
__bint_get_block(struct bdevint *bint, uint64_t *b_end)
{
	struct bintindex *index;
	uint64_t block;
	int index_id = 0, nindexes;

	nindexes = bint_nindexes(bint->usize);
	bint_lock(bint);
again:
	if (index_id == nindexes) {
		bint_unlock(bint);
		return 0;
	}

	index = bint_get_index(bint, index_id);
	if (unlikely(!index)) {
		bint_unlock(bint);
		return 0;
	}

	block = bint_get_block(bint, index, b_end);
	if (unlikely(!block)) {
		index_id++;
		goto again;
	}

	bint_unlock(bint);
	return block;
}

#ifdef FREEBSD 
static void bio_unmap_end_bio(bio_t *bio)
#else
static void bio_unmap_end_bio(bio_t *bio, int err)
#endif
{
	struct bintunmap *unmap = (struct bintunmap *)bio_get_caller(bio);
	struct bintindex *index = unmap->index;
#ifdef FREEBSD
	int err = bio->bio_error;

	if (err == EOPNOTSUPP)
		atomic_clear_bit(GROUP_FLAGS_UNMAP, &index->bint->group_flags);
#endif

	chan_lock(index->index_wait);
	TAILQ_REMOVE(&index->unmap_list, unmap, u_list);
	chan_wakeup_unlocked(index->index_wait);
	chan_unlock(index->index_wait);
	g_destroy_bio(bio);
	free(unmap, M_UNMAP);
}

static int
__bint_release_block(struct bdevint *bint, uint64_t block)
{
	struct bintindex *index;
	struct bintunmap *unmap;
	unsigned long intr_flags;
	int index_id;
	int entry, pos;
	uint8_t *bmap;
	int retval;

	index_id = calc_index_id(bint, block, &entry, &pos);
	debug_info("block %llu index id %llu entry %llu pos %llu\n", (unsigned long long)block, (unsigned long long)index_id, (unsigned long long)entry, (unsigned long long)pos);
	index = bint_get_index(bint, (int)(index_id));
	if (unlikely(!index)) {
		debug_warn("Cannot get a index at index_id %llu\n", (unsigned long long)index_id);
		return -1;
	}

	bmap = (uint8_t *)(vm_pg_address(index->metadata));
	if (!(bmap[entry] & (1 << pos))) {
		debug_warn("block %llu was not alloced\n", (unsigned long long)block);
		return -1;
	}

	bmap[entry] &= ~(1 << pos);

	retval = bint_index_io(bint, index, QS_IO_SYNC);
	if (unlikely(retval != 0)) {
		debug_warn("index write failed\n");
		bmap[entry] |= (1 << pos);
	}
	bint_incr_free(bint, BINT_UNIT_SIZE);
	if (bint_unmap_supported(bint)) {
		unmap = zalloc(sizeof(*unmap), M_UNMAP, Q_WAITOK);
		unmap->index = index;
		unmap->entry = entry;
		unmap->pos = pos;
		chan_lock_intr(index->index_wait, &intr_flags);
		TAILQ_INSERT_TAIL(&index->unmap_list, unmap, u_list);
		chan_unlock_intr(index->index_wait, &intr_flags);
		retval = bio_unmap(bint->b_dev, bint->cp, block, (BINT_UNIT_SIZE) >> bint->sector_shift, bint->sector_shift, bio_unmap_end_bio, unmap);
		if (unlikely(retval != 0)) {
			chan_lock_intr(index->index_wait, &intr_flags);
			TAILQ_REMOVE(&index->unmap_list, unmap, u_list);
			chan_wakeup_unlocked(index->index_wait);
			chan_unlock_intr(index->index_wait, &intr_flags);
			free(unmap, M_UNMAP);
		}
	}
	return 0;
}

int
bint_sync(struct bdevint *bint)
{
	struct raw_bdevint *raw_bint;
	int retval, serial_max;
	pagestruct_t *page;

	page = vm_pg_alloc(VM_ALLOC_ZERO);
	if (unlikely(!page)) {
		debug_warn("Page allocation failure\n");
		return -1;
	}

	raw_bint = (struct raw_bdevint *)(vm_pg_address(page));
	raw_bint->bid = bint->bid;
	raw_bint->usize = bint->usize;
	raw_bint->b_start = bint->b_start;
	raw_bint->b_end = bint->b_end;
	raw_bint->group_id = bint->group->group_id;
	raw_bint->group_flags = bint->group_flags;
	strcpy(raw_bint->group_name, bint->group->name);
	raw_bint->flags |= RID_SET;
	memcpy(raw_bint->mrid, bint->mrid, TL_RID_MAX);
	memcpy(raw_bint->magic, "QUADSTOR", strlen("QUADSTOR"));
	memcpy(raw_bint->quad_prod, "VTL", strlen("VTL"));
	memcpy(raw_bint->vendor, bint->vendor, sizeof(bint->vendor));
	memcpy(raw_bint->product, bint->product, sizeof(bint->product));
	serial_max = sizeof(raw_bint->serialnumber);
	memcpy(raw_bint->serialnumber, bint->serialnumber, serial_max);
	memcpy(raw_bint->ext_serialnumber, bint->serialnumber + serial_max, sizeof(raw_bint->ext_serialnumber));

	retval = qs_lib_bio_lba(bint, bint->b_start, page, QS_IO_SYNC, 0);
	if (unlikely(retval != 0)) {
		debug_warn("Sync failed for bdev meta at b_start %llu\n", (unsigned long long)bint->b_start);
	}
	vm_pg_free(page);
	return retval;
}

void
bdev_finalize(void)
{
	struct bdevgroup *group;
	struct bdevint *bint;
	int i;

	sx_xlock(gchain_lock);
	for (i = 0; i < TL_MAX_DISKS; i++) {
		bint = bint_list[i];
		if (!bint)
			continue;
		bint_list[i] = NULL;
		bdev_remove_from_alloc_list(bint);
		group = bint->group;
		atomic_dec(&group->bdevs);
		bint_clear_group_master(group, bint);
		bint_free(bint, 0);
	}
	sx_xunlock(gchain_lock);
}

int
bdev_remove(struct bdev_info *binfo)
{
	struct bdevgroup *group;
	struct bdevint *bint, *master_bint;
	int retval;

	if (binfo->bid >= TL_MAX_DISKS)
		return -1;

	sx_xlock(gchain_lock);
	bint = bint_list[binfo->bid];
	if (!bint) {
		sx_xunlock(gchain_lock);
		return -1;
	}
	group = bint->group;
	master_bint = bint_get_group_master(bint);
	if (atomic_read(&group->bdevs) > 1 && (bint == master_bint)) {
		sx_xunlock(gchain_lock);
		sprintf(binfo->errmsg, "Cannot delete pool's %s master disk, when pool contains other disks", group->name);
		return -1;
	}

	bdev_remove_from_alloc_list(bint);
	retval = bint_free(bint, 1);
	if (retval == 0) {
		bint_clear_group_master(group, bint);
		bint_list[binfo->bid] = NULL;
		atomic_dec(&group->bdevs);
	}
	sx_xunlock(gchain_lock);
	return retval;
}

int
bdev_unmap_config(struct bdev_info *binfo)
{
	struct bdevint *bint;
	int retval, unmap;

	bint = bdev_find(binfo->bid);
	if (!bint) {
		debug_warn("Cannot find bdev at id %u\n", binfo->bid);
		return -1;
	}

	if (binfo->unmap && atomic_test_bit(GROUP_FLAGS_UNMAP_ENABLED, &bint->group_flags))
		return 0;
	else if (!binfo->unmap && !atomic_test_bit(GROUP_FLAGS_UNMAP_ENABLED, &bint->group_flags))
		return 0;

	if (binfo->unmap) {
		atomic_set_bit(GROUP_FLAGS_UNMAP_ENABLED, &bint->group_flags);
		unmap = bdev_unmap_support(bint->b_dev);
		if (unmap)
			atomic_set_bit(GROUP_FLAGS_UNMAP, &bint->group_flags);
	}
	else {
		atomic_clear_bit(GROUP_FLAGS_UNMAP_ENABLED, &bint->group_flags);
		atomic_clear_bit(GROUP_FLAGS_UNMAP, &bint->group_flags);
	}

	retval = bint_sync(bint);
	return retval;
}

int
bdev_get_info(struct bdev_info *binfo)
{
	struct bdevint *bint;

	bint = bdev_find(binfo->bid);
	if (!bint)
	{
		debug_warn("Cannot find bdev at id %u\n", binfo->bid);
		return -1;
	}

	binfo->size = bint->size;
	binfo->usize = bint->usize;
	binfo->free = bint->free;
	binfo->ismaster = bint_is_group_master(bint);
	binfo->unmap = atomic_test_bit(GROUP_FLAGS_UNMAP_ENABLED, &bint->group_flags) ? 1 : 0;
	return 0;
}

static int
bint_zero_vtape_blocks(struct bdevint *bint)
{
	uint64_t b_start;
	int retval;

	if (!atomic_test_bit(GROUP_FLAGS_MASTER, &bint->group_flags))
		return 0;

	b_start = VTAPES_OFFSET >> bint->sector_shift;
	retval = tcache_zero_range(bint, b_start, MAX_VTAPES);
	return retval;
}

static int
bint_zero_indexes(struct bdevint *bint, int start_idx)
{
	int i;
	int nindexes;
	static struct bintindex *index;
	int error = 0;

	nindexes = bint_nindexes(bint->usize);
	for (i = start_idx; i < nindexes; i++)
	{
		index = bint_index_new(bint, i);
		if (!index) {
			error = -1;
			break;
		}
		bint_index_insert(bint, index);
	}

	bint_index_free_all(bint);
	return error;
}

static int
bint_load(struct bdevint *bint)
{
	struct raw_bdevint *raw_bint;
	int retval;
	int i;
	int nindexes;
	struct bintindex *index;
	uint64_t free = 0;
	pagestruct_t *page;

	page = vm_pg_alloc(0);
	if (unlikely(!page)) {
		debug_warn("Page allocation failure\n");
		return -1;
	}

	retval = qs_lib_bio_lba(bint, bint->b_start, page, QS_IO_READ, 0);
	if (unlikely(retval != 0))
		goto err;

	raw_bint = (struct raw_bdevint *)(vm_pg_address(page));

	if (memcmp(raw_bint->magic, "QUADSTOR", strlen("QUADSTOR"))) {
		debug_warn("raw bint magic mismatch\n");
		goto err;
	}

	if (memcmp(raw_bint->vendor, bint->vendor, sizeof(bint->vendor))) {
		debug_warn("raw bint vendor mismatch\n");
		goto err;
	}

	if (memcmp(raw_bint->product, bint->product, sizeof(bint->product))) {
		debug_warn("raw bint product mismatch\n");
		goto err;
	}

	if (!raw_bint_serial_match(raw_bint, bint->serialnumber, bint->serial_len)) {
		debug_warn("raw bint serialnumber mismatch\n");
		goto err;
	}

	if (unlikely(raw_bint->bid != bint->bid)) {
		debug_warn("raw bid %u mismatch with bid %u\n", raw_bint->bid, bint->bid);
		goto err;
	}

	if (unlikely(raw_bint->usize != bint->usize)) {
		debug_warn("raw size %llu mismatch with size %llu\n", (unsigned long long)raw_bint->usize, (unsigned long long)bint->usize);
		goto err;
	}

	if (unlikely(raw_bint->b_start != bint->b_start)) {
		debug_warn("raw b_start %llu mismatch with b_start %llu\n", (unsigned long long)raw_bint->b_start, (unsigned long long)bint->b_start);
		goto err;
	}

	if (unlikely(raw_bint->b_end != bint->b_end)) {
		debug_warn("raw b_end %llu mismatch with b_end %llu\n", (unsigned long long)raw_bint->b_end, (unsigned long long)bint->b_end);
		goto err;
	}

	bint->group = bdev_group_locate(raw_bint->group_id);
	if (unlikely(!bint->group)){
		debug_warn("Cannot locate pool at %u\n", raw_bint->group_id);
		goto err;
	}
	bint->group_flags = raw_bint->group_flags;
	memcpy(bint->mrid, raw_bint->mrid, TL_RID_MAX);
	nindexes = bint_nindexes(raw_bint->usize);

	for (i = 0; i < nindexes; i++) {
		index = bint_index_load(bint, i);
		if (unlikely(!index)) {
			debug_warn("Cannot load index map at id %d\n", i);
			goto err;
		}

		free += bint_index_free_blocks(bint, index);
		bint_index_insert(bint, index);
	}

	debug_info("usize %llu free %llu\n", (unsigned long long)bint->usize, (unsigned long long)(free << BINT_UNIT_SHIFT));
	bint->free = (free << BINT_UNIT_SHIFT);

	retval = 0;
	if (memcmp(raw_bint->quad_prod, "VTL", strlen("VTL"))) {
		memcpy(raw_bint->quad_prod, "VTL", strlen("VTL"));
		retval = qs_lib_bio_lba(bint, bint->b_start, page, QS_IO_SYNC, 0);
		if (unlikely(retval != 0))
			debug_warn("Fixing quad prod failed\n");
	}

	vm_pg_free(page);
	return retval;
err:
	vm_pg_free(page);
	return -1;
}

static void
calc_sector_bits(uint32_t sector_size, uint32_t *sector_shift)
{
	uint32_t shift;

	shift = 0;
	while ((sector_size >>= 1))
	{
		shift++;
	}
	*sector_shift = shift;
	return;
}

#ifdef FREEBSD
static void
bdev_orphan(struct g_consumer *cp)
{
}

static int
bint_dev_open(struct bdevint *bint, struct bdev_info *binfo)
{
	struct nameidata nd;
	int flags = FREAD | FWRITE; /* snm need to check on O_EXLOCK */
	int error;
	int vfslocked;
	struct g_provider *pp;
	struct g_geom *gp;
	struct g_consumer *cp;
	uint32_t sector_shift = 0;

	NDINIT(&nd, LOOKUP, NOFOLLOW, UIO_SYSSPACE, binfo->devpath, curthread);
	error = vn_open(&nd, &flags, 0, NULL);
	if (error)
	{
		debug_warn("failed to open disk %s error %d\n", binfo->devpath, error);
		return -1;
	}
	vfslocked = NDHASGIANT(&nd);
	NDFREE(&nd, NDF_ONLY_PNBUF);

	bint->b_dev = nd.ni_vp;
	if (!vn_isdisk(bint->b_dev, &error))
	{
		debug_warn("path %s doesnt correspond to a disk error %d\n", binfo->devpath, error);
		goto err;
	}

	g_topology_lock();

	pp = g_dev_getprovider(bint->b_dev->v_rdev);
	gp = g_new_geomf(&bdev_vdev_class, "qstor::vdev");
	gp->orphan = bdev_orphan;
	cp = g_new_consumer(gp);

	error = g_attach(cp, pp);
	if (error != 0) {
		debug_warn("Failed to attached GEOM consumer error %d\n", error);
		goto gcleanup;
	}

	error = g_access(cp, 1, 1, 0);
	if (error != 0) {
		debug_warn("Failed to set access for GEOM consumer error %d\n", error);
		g_detach(cp);
		goto gcleanup;
	}

	calc_sector_bits(pp->sectorsize, &sector_shift);
	bint->sector_shift = sector_shift;
	bint->size = pp->mediasize;

	bint->cp = cp;
	g_topology_unlock();

	VOP_UNLOCK(bint->b_dev, 0);
	VFS_UNLOCK_GIANT(vfslocked);
	return 0;
gcleanup:
	g_destroy_consumer(cp);
	g_destroy_geom(gp);
	g_topology_unlock();
err:
	VOP_UNLOCK(bint->b_dev, 0);
	bint_dev_close(bint);
	VFS_UNLOCK_GIANT(vfslocked);
	return -1;
}
#else
int
bint_dev_open(struct bdevint *bint, struct bdev_info *binfo)
{
	int error = 0;
	uint32_t sector_size = 0;
	uint32_t sector_shift = 0;

	bint->b_dev = (*kcbs.open_block_device)(binfo->devpath, &bint->size, &sector_size, &error);
	if (unlikely(!bint->b_dev)) {
		debug_warn("Unable to open dev %s err is %d\n", binfo->devpath, error);
		return -1;
	}

	calc_sector_bits(sector_size, &sector_shift);
	bint->sector_shift = sector_shift;
	return 0;
}
#endif

static struct bdevint *
bint_alloc(struct bdev_info *binfo)
{
	struct bdevint *bint;

	bint = zalloc(sizeof(struct bdevint), M_BINT, Q_WAITOK);
	bint->bint_lock = sx_alloc("bint lock");
	STAILQ_INIT(&bint->index_list);
	STAILQ_INIT(&bint->check_list);
	bint->bid = binfo->bid;

	memcpy(bint->vendor, binfo->vendor, sizeof(bint->vendor));
	memcpy(bint->product, binfo->product, sizeof(bint->product));
	memcpy(bint->serialnumber, binfo->serialnumber, sizeof(bint->serialnumber));
	bint->serial_len = binfo->serial_len;
	return bint;
}

int
bdev_add_new(struct bdev_info *binfo)
{
	struct bdevint *bint, *master_bint;
	int retval;

	bint = bdev_find(binfo->bid);
	if (bint)
		return 0;

	bint = bint_alloc(binfo);

	retval = bint_dev_open(bint, binfo);
	if (unlikely(retval != 0)) {
		debug_warn("Cannot open device at %s\n", binfo->devpath);
		free(bint, M_BINT);
		return -1;
	}

	if (bint->size < (1ULL << 32)) {
		debug_warn("Invalid bint size %llu, too less\n", (unsigned long long)bint->size);
		goto err;
	}

	bint->usize = (bint->size - (bint->size & ~BINT_UNIT_MASK));
	bint->b_start = (BDEV_META_OFFSET >> bint->sector_shift);
	bint->b_end = (bint->usize >> bint->sector_shift);

	if (binfo->isnew) {
		bint->group = bdev_group_locate(binfo->group_id);
		if (unlikely(!bint->group)) {
			debug_warn("Cannot locate pool at %u\n", binfo->group_id);
			goto err;
		}

		bint->free = bint->usize - BINT_RESERVED_SIZE;

		if (binfo->unmap) {
			int unmap;

			atomic_set_bit(GROUP_FLAGS_UNMAP_ENABLED, &bint->group_flags);
			unmap = bdev_unmap_support(bint->b_dev);
			if (unmap)
				atomic_set_bit(GROUP_FLAGS_UNMAP, &bint->group_flags);
		}

		if (!atomic_read(&bint->group->bdevs)) {
			atomic_set_bit(GROUP_FLAGS_MASTER, &bint->group_flags);
			if (bint->group->worm)
				atomic_set_bit(GROUP_FLAGS_WORM, &bint->group_flags);
			memcpy(bint->mrid, binfo->rid, TL_RID_MAX);
		} else {
			master_bint = bint_get_group_master(bint);
			if (!master_bint) {
				debug_warn("Cannot locate group master for %s\n", bint->group->name);
				goto err;
			}
			memcpy(bint->mrid, master_bint->mrid, TL_RID_MAX);
		}

		retval = bint_zero_vtape_blocks(bint);
		if (unlikely(retval != 0)) {
			debug_warn("Cannot zero vtape blocks\n");
			goto err;
		}

		retval = bint_zero_indexes(bint, 0);
		if (unlikely(retval != 0)) {
			debug_warn("Cannot zero indexes\n");
			goto err;
		}

		retval = bint_sync(bint);
		if (unlikely(retval != 0)) {
			debug_warn("bint sync failed\n");
			goto err;
		}
	}
	else
	{
		retval = bint_load(bint);
		if (unlikely(retval != 0))
		{
			goto err;
		}
		binfo->group_id = bint->group->group_id;
	}

	bint_list[binfo->bid] = bint;
	bdev_alloc_list_insert(bint);
	atomic_inc(&bint->group->bdevs);
	if (bint_is_group_master(bint)) { 
		bint_set_group_master(bint);
		binfo->ismaster = 1;
	}
	return 0;

err:
	bint_free(bint, 0);
	return -1;
}

static inline uint64_t
bint_used(struct bdevint *bint)
{
	return (bint->usize - bint->free);
}

#define BINT_ALLOC_RESERVED		(BINT_UNIT_SIZE << 2) /* 4 segments */

static struct bdevint *
bdev_alloc_list_next_rotate(struct bdevint *bint)
{
	struct bdevint *ret;
	struct bdevgroup *group = bint->group;

	ret = SLIST_NEXT(bint, a_list);
	if (!ret)
		ret = SLIST_FIRST(&group->alloc_list);
	return ret;
}

static struct bdevint *
bint_get_eligible(struct bdevgroup *group, uint32_t size)
{
	struct bdevint *found = NULL, *next, *eligible;

	sx_xlock(group->alloc_lock);
	eligible = group->eligible;
	if (!eligible) {
		eligible = SLIST_FIRST(&group->alloc_list);
	}

	while (eligible) {
		if (eligible->free) {
			found = eligible;
			eligible = bdev_alloc_list_next_rotate(eligible);
			break;
		}
		next = bdev_alloc_list_next_rotate(eligible);
		bdev_remove_from_alloc_list(eligible);
		if (next == eligible) {
			eligible = NULL;
			break;
		}
		eligible = next;
	}
	group->eligible = eligible;
	sx_xunlock(group->alloc_lock);
	return found;
}

uint64_t
bdev_get_block(struct bdevint *bint, struct bdevint **ret_bint, uint64_t *b_end)
{
	uint64_t ret;
	struct bdevgroup *group;
	struct bdevint *a_bint;

	group = bint->group;
	debug_check(!group);

	a_bint = bint_get_eligible(group, BINT_UNIT_SIZE);
	if (!a_bint)
		return 0;

	ret = __bint_get_block(a_bint, b_end);
	if (ret)
		*ret_bint = a_bint;
	else
		debug_warn("a_bint usize %llu free %llu\n", (unsigned long long)a_bint->usize, (unsigned long long)a_bint->free);

	return ret;
}

int
bdev_release_block(struct bdevint *bint, uint64_t block)
{
	debug_check(!block);
	bint_lock(bint);
	__bint_release_block(bint, block);
	bint_unlock(bint);
	bdev_alloc_list_insert(bint);
	return 0;
}

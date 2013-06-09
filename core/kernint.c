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
#include "../common/commondefs.h"
#include "bdev.h"
#include "vdevdefs.h"
#include <exportdefs.h>
#include "tdevice.h"
#include "tcache.h"
#include "blk_map.h"
#include "bdevgroup.h"
#include "gdevq.h"
#include "lz4.h"
#include "lzfP.h"

int stale_initiator_timeout = STALE_INITIATOR_TIMEOUT;
struct qs_kern_cbs kcbs;

int
qs_deflate_block(uint8_t *in_buf, int uncomp_len, uint8_t *out_buf, int *comp_size, void *wrkmem, int algo)
{
	int retval;

	/* 4 bytes for comp size information */
	switch (algo) {
	case COMP_ALG_LZF:
		retval = lzf_compress(in_buf, uncomp_len, out_buf+sizeof(uint32_t), uncomp_len - 508, wrkmem);
		break;
	case COMP_ALG_LZ4:
		retval = LZ4_compress_limitedOutput(wrkmem, in_buf, out_buf+sizeof(uint32_t), uncomp_len, uncomp_len - 508);
		break;
	default:
		debug_check(1);
		retval = 0;
	}

	if (!retval) {
		return -1;
	}

	SET_COMP_SIZE(out_buf, retval, algo);
	*comp_size = (retval + sizeof(uint32_t));
	return 0;
}

int
qs_inflate_block(uint8_t *in_buf, int comp_len, uint8_t *out_buf, int uncomp_len)
{
	int retval;
	uint32_t stored_comp_size, comp_size, algo;

	stored_comp_size = *((uint32_t *)(in_buf));
	comp_size = stored_comp_size & COMP_ALG_MASK;
	algo = stored_comp_size >> COMP_ALG_SHIFT;

	debug_check(comp_size > comp_len);
	if (comp_size > comp_len)
		debug_warn("comp size %u comp len %d\n", comp_size, comp_len);
	switch (algo) {
	case COMP_ALG_LZF:
		retval = lzf_decompress(in_buf+sizeof(uint32_t), comp_size, out_buf, uncomp_len);
		break;
	case COMP_ALG_LZ4:
		retval = LZ4_uncompress_unknownOutputSize(in_buf+sizeof(uint32_t), out_buf, comp_size, uncomp_len);
		break;
	default:
		debug_check(1);
		retval = 0;
	}

	if (retval != uncomp_len)
		return -1;
	return 0;
}

uma_t *bentry_cache;
uma_t *bmap_cache;
uma_t *map_lookup_cache;
uma_t *tcache_cache;
uma_t *chan_cache;
uma_t *compl_cache;
uma_t *ctio_cache;
uma_t *istate_cache;
uma_t *pgdata_cache;
#ifdef FREEBSD
uma_t *biot_cache;
uma_t *biot_page_cache;
uma_t *mtx_cache;
uma_t *sx_cache;
#endif

struct mdaemon_info mdaemon_info;
atomic_t kern_inited;
atomic_t mdaemon_load_done;
atomic_t itf_enabled;

struct interface_list cbs_list;
sx_t *cbs_lock;
sx_t *gchain_lock;
mtx_t *tdevice_lookup_lock;
mtx_t *glbl_lock;

struct tdevice *tdevices[TL_MAX_DEVICES];

static void
mdaemon_set_info(struct mdaemon_info *info)
{
	memcpy(&mdaemon_info, info, sizeof(*info));
}

static void
exit_caches(void)
{
	debug_print("free bentry_cache\n");
	if (bentry_cache)
		__uma_zdestroy("bentry cache", bentry_cache);

	debug_print("free bmap_cache\n");
	if (bmap_cache)
		__uma_zdestroy("bmap cache", bmap_cache);

	debug_print("free map_lookup_cache\n");
	if (map_lookup_cache)
		__uma_zdestroy("map_lookup cache", map_lookup_cache);

	debug_print("free tcache_cache\n");
	if (tcache_cache)
		__uma_zdestroy("qs_tcache", tcache_cache);

	debug_print("free chan_cache\n");
	if (chan_cache)
		__uma_zdestroy("qs_wait_chan", chan_cache);

	debug_print("free compl_cache\n");
	if (compl_cache)
		__uma_zdestroy("qs_wait_compl", compl_cache);

	debug_print("free ctio_cache\n");
	if (ctio_cache)
		__uma_zdestroy("qs_ctio", ctio_cache);

	debug_print("free istate_cache\n");
	if (istate_cache)
		__uma_zdestroy("qs_istate", istate_cache);

	debug_print("free pgdata_cache\n");
	if (pgdata_cache)
		__uma_zdestroy("qs_pgdata", pgdata_cache);
#ifdef FREEBSD
	debug_print("biot_cache_free\n");
	if (biot_cache)
		uma_zdestroy(biot_cache);

	debug_print("biot_page_cache_free\n");
	if (biot_page_cache)
		uma_zdestroy(biot_page_cache);

	debug_print("mtx_cache_free\n");
	if (mtx_cache)
		uma_zdestroy(mtx_cache);

	debug_print("sx_cache_free\n");
	if (sx_cache)
		uma_zdestroy(sx_cache);
#endif
}

#ifdef FREEBSD 
#define CREATE_CACHE(vr,nm,s)				\
do {							\
	vr = uma_zcreate(nm,s, NULL, NULL, NULL, NULL, 0, 0);	\
} while(0);
#else
#define CREATE_CACHE(vr,nm,s)				\
do {							\
	vr = uma_zcreate(nm,s);			\
} while(0);
#endif

static int
init_caches(void)
{
	CREATE_CACHE(bentry_cache, "bentry cache", sizeof(struct blk_entry));
	if (!bentry_cache) {
		debug_warn("Cannot create bentry cache\n");
		return -1;
	}

	CREATE_CACHE(bmap_cache, "bmap cache", sizeof(struct blk_map));
	if (!bmap_cache) {
		debug_warn("Cannot create bmap cache\n");
		return -1;
	}

	CREATE_CACHE(map_lookup_cache, "map lookup cache", sizeof(struct map_lookup));
	if (!map_lookup_cache) {
		debug_warn("Cannot create map_lookup cache\n");
		return -1;
	}

	CREATE_CACHE(tcache_cache, "qs_tcache", sizeof(struct tcache));
	if (!tcache_cache) {
		debug_warn("Cannot create tcache cache\n");
		return -1;
	}

	CREATE_CACHE(chan_cache, "qs_wait_chan", sizeof(wait_chan_t));
	if (unlikely(!chan_cache)) {
		return -1;
	}

	CREATE_CACHE(compl_cache, "qs_wait_compl", sizeof(wait_compl_t));
	if (unlikely(!compl_cache)) {
		return -1;
	}

	CREATE_CACHE(ctio_cache, "ctio_cache", sizeof(struct qsio_scsiio));
	if (!ctio_cache) {
		debug_warn("Cannot create ctio cache\n");
		return -1;
	}

	CREATE_CACHE(istate_cache, "qs_istate", sizeof(struct initiator_state));
	if (unlikely(!istate_cache)) {
		return -1;
	}

	CREATE_CACHE(pgdata_cache, "pgdata_cache", sizeof(struct pgdata));
	if (!pgdata_cache) {
		debug_warn("Cannot create pgdata cache\n");
		return -1;
	}

#ifdef FREEBSD
	CREATE_CACHE(biot_cache, "qs_biot", sizeof(struct biot));
	if (unlikely(!biot_cache)) {
		return -1;
	}

	CREATE_CACHE(biot_page_cache, "qs_biot_page", ((MAXPHYS >> LBA_SHIFT) * sizeof(pagestruct_t *)));
	if (unlikely(!biot_page_cache)) {
		return -1;
	}

	CREATE_CACHE(sx_cache, "qs_sx", sizeof(sx_t));
	if (unlikely(!sx_cache)) {
		return -1;
	}

	CREATE_CACHE(mtx_cache, "qs_mtx", sizeof(mtx_t));
	if (unlikely(!mtx_cache)) {
		return -1;
	}
#endif

	return 0;
}

static void
exit_globals(void)
{
	if (gchain_lock)
		sx_free(gchain_lock);

	if (cbs_lock)
		sx_free(cbs_lock);

	if (tdevice_lookup_lock)
		mtx_free(tdevice_lookup_lock);

	if (glbl_lock)
		mtx_free(glbl_lock);
}

static void
device_detach_interfaces(void)
{
	struct qs_interface_cbs *iter;
	struct qs_interface_cbs *fc;

again:
	fc = NULL;
	sx_xlock(cbs_lock);
	LIST_FOREACH(iter, &cbs_list, i_list) {
		if (!iter->detach_interface)
			continue;
		fc = iter;
		break;
	}
	sx_xunlock(cbs_lock);
	if (!fc)
		return;

	(*fc->detach_interface)();
	goto again;
}

static int 
__kern_exit(void)
{
	int i;

	debug_print("kern_inited; %d\n", atomic_read(&kern_inited));
	if (!atomic_read(&kern_inited))
		return 0;

	atomic_set(&itf_enabled, 0);
	atomic_set(&kern_inited, 0);

	debug_print("disable devices\n");
	for (i = 0; i < TL_MAX_DEVICES; i++) {
		struct tdevice *tdevice = tdevices[i];

		if (!tdevice)
			continue;
		tdevice_cbs_disable(tdevice);
	}

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		struct tdevice *tdevice = tdevices[i];

		if (!tdevice)
			continue;

		tdevice_cbs_remove(tdevice);
	}

	debug_print("detach interfaces\n");
	device_detach_interfaces();

	debug_print("exit gdevq threads\n");
	exit_gdevq_threads();

	for (i = 0; i < TL_MAX_DEVICES; i++) {
		tdevice_delete(i, 0);
	}

	debug_print("bdev finalize\n");
	bdev_finalize();

	debug_print("groups free\n");
	bdev_groups_free();

	debug_print("clear fc rules\n");
	target_clear_fc_rules(-1);

	debug_print("end\n");
	return 0;
}

static void
init_globals(void)
{
	gchain_lock = sx_alloc("gchain lock");
	cbs_lock = sx_alloc("cbs lock");
	tdevice_lookup_lock = mtx_alloc("tdevice lookup lock");
	glbl_lock = mtx_alloc("glbl lock");
}

static int
coremod_load_done(void)
{
	atomic_set(&mdaemon_load_done, 1);
	return 0;
}

int
kern_interface_init(struct qs_kern_cbs *kern_cbs)
{
	int retval;

	kern_cbs->coremod_load_done = coremod_load_done;
	kern_cbs->coremod_check_disks = bdev_check_disks;
	kern_cbs->coremod_exit = __kern_exit;
	kern_cbs->mdaemon_set_info = mdaemon_set_info;
	kern_cbs->bdev_add_new = bdev_add_new;
	kern_cbs->bdev_remove = bdev_remove;
	kern_cbs->bdev_get_info = bdev_get_info;
	kern_cbs->bdev_add_group = bdev_group_add; 
	kern_cbs->bdev_delete_group = bdev_group_remove; 
	kern_cbs->bdev_rename_group = bdev_group_rename; 
	kern_cbs->vdevice_new = vdevice_new;
	kern_cbs->vdevice_delete = vdevice_delete;
	kern_cbs->vdevice_modify = vdevice_modify;
	kern_cbs->vdevice_info = vdevice_info;
	kern_cbs->vdevice_load = vdevice_load;
	kern_cbs->vcartridge_new = vcartridge_new;
	kern_cbs->vcartridge_load = vcartridge_load;
	kern_cbs->vcartridge_delete = vcartridge_delete;
	kern_cbs->vcartridge_info = vcartridge_info;
	kern_cbs->vcartridge_reload = vcartridge_reload;
	kern_cbs->target_add_fc_rule = target_add_fc_rule;
	kern_cbs->target_remove_fc_rule = target_remove_fc_rule;

	memcpy(&kcbs, kern_cbs, sizeof(kcbs));

	retval = init_caches();
	if (unlikely(retval != 0)) {
		exit_caches();
		return -1;
	}

	init_globals();

	retval = init_gdevq_threads();
	if (unlikely(retval != 0)) {
		exit_globals();
		exit_caches();
		return -1;
	}

	atomic_set(&kern_inited, 1);
	atomic_set(&itf_enabled, 1);
	return 0;
}

void
kern_interface_exit(void)
{
	__kern_exit();
	exit_globals();
	exit_caches();
}

struct qs_interface_cbs *
device_interface_locate(int interface)
{
	struct qs_interface_cbs *iter;

	sx_xlock(cbs_lock);
	LIST_FOREACH(iter, &cbs_list, i_list) {
		if (iter->interface == interface) {
			sx_xunlock(cbs_lock);
			return iter;
		}
	}
	sx_xunlock(cbs_lock);
	return NULL;
}

int
__device_unregister_interface(struct qs_interface_cbs *cbs)
{
	struct qs_interface_cbs *iter;
	int found = -1;

	sx_xlock(cbs_lock);
	LIST_FOREACH(iter, &cbs_list, i_list) {
		if (iter->interface != cbs->interface)
			continue;
		LIST_REMOVE(iter, i_list);
		free(iter, M_CBS);
		found = 0;
		break;
	}
	sx_xunlock(cbs_lock);
	return found;
}

static uint64_t
node_get_tprt(void)
{
	return 1;
}

int
__device_register_interface(struct qs_interface_cbs *cbs)
{
	struct qs_interface_cbs *new;

	sx_xlock(cbs_lock);
	if (!atomic_read(&itf_enabled)) {
		sx_xunlock(cbs_lock);
		return -1;
	}

	new = zalloc(sizeof(*new), M_CBS, Q_WAITOK);
	cbs->ctio_new = ctio_new;
	cbs->ctio_allocate_buffer = ctio_allocate_buffer;
	cbs->ctio_free_data = ctio_free_data;
	cbs->ctio_free_all = ctio_free_all;
	cbs->ctio_write_length = ctio_write_length;
	cbs->get_device = get_device;
	cbs->device_tid = device_tid;
	cbs->bus_from_lun = bus_from_lun;
	cbs->write_lun = write_lun;
	cbs->device_set_vhba_id = device_set_vhba_id;
	cbs->device_set_hpriv = device_set_hpriv;
	cbs->device_send_ccb = device_send_ccb;
	cbs->device_send_notify = device_send_notify;
	cbs->device_istate_abort_task = device_istate_abort_task;
	cbs->device_istate_abort_task_set = device_istate_abort_task_set;
	cbs->device_istate_queue_ctio = device_istate_queue_ctio;
	cbs->device_queue_ctio = device_queue_ctio;
	cbs->device_queue_ctio_list = device_queue_ctio_list;
	cbs->device_queue_ctio_direct = tdevice_insert_ccb;
	cbs->device_remove_ctio = device_remove_ctio;
	cbs->device_check_cmd = tdevice_check_cmd;
	cbs->device_allocate_buffers = device_allocate_buffers;
	cbs->device_allocate_cmd_buffers = device_allocate_cmd_buffers;
	cbs->device_target_reset = tdevice_reset;
	cbs->device_free_initiator = device_free_initiator;
	cbs->fc_initiator_check = fc_initiator_check;
	cbs->get_tprt = node_get_tprt;

	memcpy(new, cbs, sizeof(*new));
	LIST_INSERT_HEAD(&cbs_list, new, i_list);
	sx_xunlock(cbs_lock);
	return 0;
}

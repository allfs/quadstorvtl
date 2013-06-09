#ifndef QS_EXPORTDEFS_H_
#define QS_EXPORTDEFS_H_	1

#include "qsio_ccb.h"

enum {
	TARGET_INT_LOCAL  = 0x01,
	TARGET_INT_ISCSI  = 0x02,
	TARGET_INT_FC     = 0x03,
};

#define LDEV_RPORT_START	3
#define FC_RPORT_START		4
#define SRPT_RPORT_START	32000
#define ISCSI_RPORT_START	32768
#define RPORT_MAX		65535

enum {
	QS_PRIO_SWP,
	QS_PRIO_INOD,
};

struct tdevice;
struct qsio_scsiio;
struct logical_unit_identifier;
struct fc_rule_config;

struct qs_interface_cbs {
	/* set by the interface */
	int (*new_device) (struct tdevice *);
	int (*update_device) (struct tdevice *, int tidk, void *hpriv);
	int (*remove_device) (struct tdevice *, int tid, void *hpriv);
	void (*disable_device) (struct tdevice *, int tid, void *hpriv);
	void (*detach_interface) (void);/* Mainly for FC */
	void (*ctio_exec) (struct qsio_scsiio *);
	int interface; /* Type of interface */
	atomic_t itf_enabled;
	
	/* set by core */
	struct qsio_scsiio* (*ctio_new) (allocflags_t flags);
	void (*ctio_allocate_buffer) (struct qsio_scsiio *, int, allocflags_t);
	void (*ctio_free_data) (struct qsio_scsiio *);
	void (*ctio_free_all) (struct qsio_scsiio *);
	int (*ctio_write_length) (struct qsio_scsiio *, struct tdevice *, uint32_t *block_size, uint32_t *, int *);

	void (*write_lun)(struct tdevice *, uint8_t *);
	uint32_t (*bus_from_lun)(uint16_t ilun);
	struct tdevice* (*get_device)(uint32_t);
	uint32_t (*device_tid) (struct tdevice *);
	void (*device_set_vhba_id)(struct tdevice *, int, int);
	void (*device_set_hpriv)(struct tdevice *, void *);
	void (*device_send_ccb) (struct qsio_scsiio *);
	void (*device_send_notify) (struct qsio_immed_notify *);
	int (*device_istate_queue_ctio) (struct tdevice *, struct qsio_scsiio *);
	int (*device_istate_abort_task) (struct tdevice *, uint64_t[], uint64_t[], int, uint32_t);
	void (*device_istate_abort_task_set) (struct tdevice *, uint64_t[], uint64_t[], int);
	int (*device_queue_ctio) (struct tdevice *, struct qsio_scsiio *);
	void (*device_queue_ctio_direct) (struct qsio_hdr *);
	void (*device_queue_ctio_list) (struct ccb_list *);
	void (*device_remove_ctio) (struct qsio_scsiio *, struct ccb_list *);
	int (*device_check_cmd) (struct tdevice *, uint8_t);
	int (*device_allocate_buffers) (struct qsio_scsiio *, uint32_t, uint32_t, allocflags_t);
	int (*device_allocate_cmd_buffers) (struct qsio_scsiio *, allocflags_t);
	void (*device_target_reset) (struct tdevice *vdevice, uint64_t i_prt[], uint64_t t_prt[], uint8_t init_int);
	void (*device_free_initiator) (uint64_t[], uint64_t[], int, struct tdevice *);

	int (*fc_initiator_check)(uint64_t wwpn[], void *device);
	uint64_t (*get_tprt) (void);

	LIST_ENTRY(qs_interface_cbs) i_list;
};

#ifdef FREEBSD
#define BSD_LIST_HEAD	LIST_HEAD
#endif

BSD_LIST_HEAD(interface_list, qs_interface_cbs);

struct bdev_info;
struct vdeviceinfo;
struct vcartridge;
struct group_conf;
struct mdaemon_info;

struct tpriv {
	void *data;
};

struct qs_kern_cbs {
	/* set by module layer */
	void (*debug_warn)(char *fmt, ...);
	void (*debug_info)(char *fmt, ...);
	void (*debug_print)(char *fmt, ...);
	void (*debug_check)(void);
	unsigned long (*msecs_to_ticks) (unsigned long);
	unsigned long (*ticks_to_msecs) (unsigned long);
	uint32_t (*get_ticks)(void);
	iodev_t* (*open_block_device)(const char *devpath, uint64_t *size, uint32_t *sector_size, int *error);
	void (*close_block_device)(iodev_t *);
	pagestruct_t* (*vm_pg_alloc)(allocflags_t flags);
	void (*vm_pg_free)(pagestruct_t *pp);
	void* (*vm_pg_address)(pagestruct_t *pp);
	void (*vm_pg_ref)(pagestruct_t *pp);
	void (*vm_pg_unref)(pagestruct_t *pp);
	void* (*vm_pg_map)(pagestruct_t **pp, int pg_count);
	void (*vm_pg_unmap)(void *maddr, int pg_count);
	void* (*uma_zcreate)(const char *name, size_t size);
	void (*uma_zdestroy)(const char *name, void *cachep);
	void* (*uma_zalloc)(uma_t *cachep, allocflags_t flags, size_t len);
	void (*uma_zfree)(uma_t *cachep, void *ptr);
	void* (*zalloc)(size_t size, int type, allocflags_t flags);
	void* (*malloc)(size_t size, int type, allocflags_t flags); 
	void (*free)(void *ptr);
	uint64_t (*get_availmem)(void);
	mtx_t* (*mtx_alloc)(const char *name);
	void (*mtx_free)(mtx_t *mtx);
	void (*mtx_lock)(mtx_t *mtx);
	void (*mtx_lock_intr)(mtx_t *mtx, void *);
	void (*mtx_unlock)(mtx_t *mtx);
	void (*mtx_unlock_intr)(mtx_t *mtx, void *);
	sx_t* (*shx_alloc)(const char *name);
	void (*shx_free)(sx_t *sx);
	void (*shx_xlock)(sx_t *sx);
	void (*shx_xunlock)(sx_t *sx);
	void (*shx_slock)(sx_t *sx);
	void (*shx_sunlock)(sx_t *sx);
	int (*shx_xlocked)(sx_t *sx);
	void (*bdev_start)(iodev_t *b_dev, struct tpriv *tpriv);
	void (*bdev_marker)(iodev_t *b_dev, struct tpriv *tpriv);
	cv_t* (*cv_alloc)(const char *name);
	void (*cv_free)(cv_t *);
	void (*cv_wait)(cv_t *cv, mtx_t *lock, void *, int intr);
	long (*cv_timedwait)(cv_t *cv, mtx_t *lock, void *, int timo);
	void (*cv_wait_sig)(cv_t *cv, mtx_t *lock, int intr);
	void (*wakeup_one_compl)(cv_t *cv, mtx_t *mtx, int *done);
	void (*wakeup_compl)(cv_t *cv, mtx_t *mtx, int *done);
	void (*wakeup_one)(cv_t *cv, mtx_t *mtx);
	void (*wakeup_one_nointr)(cv_t *cv, mtx_t *mtx);
	void (*wakeup_one_unlocked)(cv_t *cv);
	void (*wakeup_unlocked)(cv_t *cv);
	void (*wakeup)(cv_t *cv, mtx_t *mtx);
	void (*wakeup_nointr)(cv_t *cv, mtx_t *mtx);
	void (*pause)(const char *, int timo);
	void (*printf)(const char *fmt, ...);
	int (*sprintf)(char *buf, const char *, ...);
	int (*snprintf)(char *, size_t, const char *, ...);
	int (*kernel_thread_check)(int *flags, int bit);
	int (*kernel_thread_stop)(kproc_t *task, int *flags, void *chan, int bit);
	void (*sched_prio)(int prio);
	int (*get_cpu_count)(void);
	bio_t* (*g_new_bio)(iodev_t *iodev, void (*end_bio_func)(bio_t *, int), void *consumer, uint64_t bi_sector, int bio_vec_count, int rw);
	void (*bio_free_pages)(bio_t *bio);
	int (*bio_add_page)(bio_t *bio, pagestruct_t *pp, unsigned int len, unsigned int offset);
	void (*bio_free_page)(bio_t * bio);
	void (*bio_set_command)(bio_t *, int);
	int (*bio_get_command)(bio_t *);
	void* (*bio_get_caller)(bio_t *);
	int (*bio_get_length)(bio_t *);
	int (*bio_unmap)(iodev_t *, void *cp, uint64_t start_sector, uint32_t blocks, uint32_t shift, void (*end_bio_func)(bio_t *, int), void *priv);
	int (*bdev_unmap_support)(iodev_t *);
	iodev_t* (*send_bio)(bio_t *);
	iodev_t* (*bio_get_iodev)(bio_t *);
	uint64_t (*bio_get_start_sector)(bio_t *);
	uint32_t (*bio_get_max_pages)(iodev_t *);
	uint32_t (*bio_get_nr_sectors)(bio_t *);
	void (*g_destroy_bio)(bio_t *);
	void (*processor_yield)(void);
	int (*kproc_create)(void *fn, void *data, kproc_t **task, const char namefmt[], ...);
	void (*thread_start)(struct tpriv *);
	void (*thread_end)(struct tpriv *);
	int (*copyout)(void *, void *, size_t len);
	int (*copyin)(void *, void *, size_t len);
	void (*kern_panic) (char *);

	/* set by core lib */
	void (*mdaemon_set_info)(struct mdaemon_info *);
	int (*bdev_add_new)(struct bdev_info *);
	int (*bdev_remove)(struct bdev_info *);
	int (*bdev_add_stub)(struct bdev_info *);
	int (*bdev_remove_stub)(struct bdev_info *);
	int (*bdev_get_info)(struct bdev_info *);
	int (*bdev_ha_config)(struct bdev_info *);
	int (*bdev_unmap_config)(struct bdev_info *);
	int (*bdev_wc_config)(struct bdev_info *);
	int (*bdev_add_group)(struct group_conf *);
	int (*bdev_delete_group)(struct group_conf *);
	int (*bdev_rename_group)(struct group_conf *);
	int (*vdevice_new)(struct vdeviceinfo *);
	int (*vdevice_delete)(struct vdeviceinfo *);
	int (*vdevice_modify)(struct vdeviceinfo *);
	int (*vdevice_info)(struct vdeviceinfo *);
	int (*vdevice_load)(struct vdeviceinfo *);
	int (*vcartridge_new)(struct vcartridge *);
	int (*vcartridge_load)(struct vcartridge *);
	int (*vcartridge_delete)(struct vcartridge *);
	int (*vcartridge_info)(struct vcartridge *);
	int (*vcartridge_reload)(struct vcartridge *);
	int (*coremod_load_done)(void);
	int (*coremod_check_disks)(void);
	int (*coremod_exit)(void);
	int (*target_add_fc_rule)(struct fc_rule_config *);
	int (*target_remove_fc_rule)(struct fc_rule_config *);

	/* Clustering sock defs */
	sock_t* (*sock_create) (void *priv);
	int (*sock_connect) (sock_t *sock, uint32_t addr, uint32_t local_addr, uint16_t port);
	int (*sock_read) (sock_t *sock, void *buf, int len);
	int (*sock_write) (sock_t *sock, void *buf, int len);
	int (*sock_write_page) (sock_t *sock, pagestruct_t *page, int off, int pg_len);
	void (*sock_close) (sock_t *sock, int linger);
	void (*sock_free) (sock_t *sock);
	int (*sock_bind) (sock_t *sock, uint32_t addr, uint16_t port);
	sock_t* (*sock_accept) (sock_t *sock, void *priv, int *error, uint32_t *ipaddr);
	int (*sock_has_write_space) (sock_t *sock);
	int (*sock_has_read_data) (sock_t *sock);
	void (*sock_nopush) (sock_t *sock, int set);

	/* Clustering sock callbacks */
	void (*sock_read_avail) (void *priv);
	void (*sock_write_avail) (void *priv);
	void (*sock_state_change) (void *priv, int newstate);
};

enum {
	SOCK_STATE_CONNECTED = 1,
	SOCK_STATE_CLOSED,
};

/* libcore interfaces */
int kern_interface_init(struct qs_kern_cbs *kcbs);
void kern_interface_exit(void);
int __device_register_interface(struct qs_interface_cbs *cbs);
int __device_unregister_interface(struct qs_interface_cbs *cbs);

/* Used by interface modules */
int device_register_interface(struct qs_interface_cbs *cbs);
void device_unregister_interface(struct qs_interface_cbs *cbs);

#define LBA_SHIFT		12
#define LBA_SIZE		(1U << LBA_SHIFT)
#define LBA_MASK		(LBA_SIZE - 1)

static inline void 
ctio_idx_offset(int ctio_done, int *idx, int *offset)
{
	*idx = (ctio_done >> LBA_SHIFT);
	*offset = (ctio_done & LBA_MASK);
}

static inline int
transfer_length_to_pglist_cnt(int lba_shift, uint32_t transfer_length)
{
	if (lba_shift == LBA_SHIFT)
		return transfer_length;
	else {
		uint32_t size = (transfer_length << lba_shift);
		int pglist_cnt;

		pglist_cnt = size >> LBA_SHIFT;
		if (size & LBA_MASK)
			pglist_cnt++;
		return pglist_cnt;
	}
}

/* SCSI common defs */

struct inquiry_data {
	uint8_t device_type; /* periperal qualifier and device type */
	uint8_t rmb; /* Removable media */
	uint8_t version; /* ISO/IEC, ECMA, ANSI Version */
	uint8_t response_data; /* AERC , TrmTsk, NormACA, Response data format */
	uint8_t additional_length; /* Additional length for parameters */
	uint8_t protect;
	uint8_t mchangr; /* EncServ, VS, MulitP, MChangr, ACKREQq etc. */
	uint8_t linked; /* RelAdr, linked, CmdQue etc. */
	uint8_t vendor_id[8]; /* Vendor identification */
	uint8_t product_id[16]; /* Product identification */
	uint8_t revision_level[4]; /* Product revision level */
	uint8_t vendor_specific[20]; /* Vendor specific */
	uint16_t unused; /* Unused by us */
	uint16_t vd1;
	uint16_t vd2;
	uint16_t vd3;
	uint16_t vd4;
	uint16_t vd5;
	uint16_t vd6;
	uint16_t vd7;
	uint16_t vd8;
	uint8_t  rsvd[22];
} __attribute__ ((__packed__));

#define MAX_EVPD_PAGES				16

struct evpd_page_info {
	int num_pages;
	uint8_t page_code[MAX_EVPD_PAGES];
};

struct logical_unit_identifier {
	uint8_t code_set;
	uint8_t identifier_type;
	uint8_t rsvd;
	uint8_t identifier_length;
	uint8_t vendor_id[8];
	uint8_t product_id[16];
	uint8_t serial_number[32];
} __attribute__ ((__packed__));

struct logical_unit_naa_identifier {
	uint8_t code_set;
	uint8_t identifier_type;
	uint8_t reserved;
	uint8_t identifier_length;
	uint8_t naa_id[16];
} __attribute__ ((__packed__));

struct vital_product_page {
	uint8_t device_type;
	uint8_t page_code;
	uint8_t rsvd;
	uint8_t page_length;
	uint8_t page_type[0];
} __attribute__ ((__packed__));

struct device_identification_page {
	uint8_t device_type;
	uint8_t page_code;
	uint8_t rsvd;
	uint8_t page_length;
} __attribute__ ((__packed__));

struct device_identifier {
	uint8_t code_set;
	uint8_t identifier_type;
	uint8_t rsvd;
	uint8_t identifier_length;
	uint8_t identifier[0];
} __attribute__ ((__packed__));

struct serial_number_page {
	uint8_t device_type;
	uint8_t page_code;
	uint8_t rsvd;
	uint8_t page_length;
	uint8_t serial_number[32];
} __attribute__ ((__packed__));

/* Ctio sense op */
static inline void
ctio_allocate_sense(struct qsio_scsiio *ctio, int length)
{
	int buffer_len;
	int sense_offset = (ctio->init_int != TARGET_INT_ISCSI) ? 0 : 2;

	/* 4 incase for padding */
	buffer_len = length + sense_offset + 4;
	ctio->sense_data = zalloc(buffer_len, M_DEVBUF, M_WAITOK);
	ctio->scsi_status = SCSI_STATUS_CHECK_COND;
	ctio->sense_len = length;
}

static inline void
fill_sense_info(struct qs_sense_data *sense, uint8_t error_code, uint8_t sense_key, uint32_t info, uint8_t asc, uint8_t ascq)
{
	sense->error_code = error_code;
	sense->flags = sense_key;
	*((uint32_t *)sense->info) = info;
	sense->add_sense_code = asc;
	sense->add_sense_code_qual = ascq;	
	sense->extra_len =
		offsetof(struct qs_sense_data, extra_bytes) -
		offsetof(struct qs_sense_data, cmd_spec_info);
}

static inline void
ctio_construct_sense(struct qsio_scsiio *ctio, uint8_t error_code, uint8_t sense_key, uint32_t info, uint8_t asc, uint8_t ascq)
{
	struct qs_sense_data *sense;
	int sense_offset = (ctio->init_int != TARGET_INT_ISCSI) ? 0 : 2;

	ctio_allocate_sense(ctio, SSD_MIN_SIZE);
	sense = (struct qs_sense_data *)(ctio->sense_data+sense_offset);
	fill_sense_info(sense, error_code, sense_key, info, asc, ascq);
	return;
}

static inline void
ctio_free_sense(struct qsio_scsiio *ctio)
{
	if (!ctio->sense_data)
		return;

	free(ctio->sense_data, M_DEVBUF);
	ctio->sense_data = NULL;
	ctio->sense_len = 0;
}

#endif

#ifndef QSIO_CCB_H_
#define QSIO_CCB_H_

#ifndef FREEBSD
#include "queue.h"
#endif

enum {
	QS_IO_READ,
	QS_IO_WRITE,
};

enum {
	QSIO_DIR_IN		= 0x01,
	QSIO_DIR_OUT		= 0x02,
	QSIO_DIR_NONE		= 0x04,
	QSIO_DATA_DIR_IN	= 0x08,
	QSIO_DATA_DIR_OUT	= 0x10,
	QSIO_SEND_STATUS	= 0x20,
	QSIO_TYPE_CTIO		= 0x40,
	QSIO_TYPE_NOTIFY	= 0x80,
	QSIO_HBA_ERROR		= 0x100,
	QSIO_HBA_REQUEUE	= 0x200,
	QSIO_CTIO_ABORTED	= 0x400,
	QSIO_SEND_ABORT_STATUS	= 0x800,
	QSIO_IN_DEVQ		= 0x8000,
	/* Unused till 0x40000 */
	QSIO_BUFFERED		= 0x40000,
};

struct qs_sense_data {
	uint8_t error_code;
	uint8_t segment;
	uint8_t flags;
	uint8_t info[4];
	uint8_t extra_len;
	uint8_t cmd_spec_info[4];
	uint8_t add_sense_code;
	uint8_t add_sense_code_qual;
	uint8_t fru;
	uint8_t sense_key_spec[3];
	uint8_t extra_bytes[46];
} __attribute__ ((__packed__));

#define SENSE_LEN(sense_data)	((sense_data->extra_len + offsetof(struct qs_sense_data, cmd_spec_info)))


struct fcbridge;
struct qpriv {
	void *qcmd;
	struct fcbridge *fcbridge;
};

struct iscsi_cmnd;
struct iscsi_priv {
	struct iscsi_cmnd *cmnd;
	char *init_name;
};

struct node_priv {
	void *node_msg;
};

#ifdef FREEBSD 
struct vhba_priv {
	void *ccb;
	void *ldev;
};
#else
struct vhba_priv {
	struct scsi_cmnd *SCpnt;
	void (*done) (struct scsi_cmnd *);
};
#endif

struct tdevice;
struct qsio_hdr {
	void (*queue_fn) (void *);
	struct tdevice *tdevice;
	uint32_t target_lun;
	uint32_t flags;	
	union {
		struct qpriv qpriv;
		struct iscsi_priv ipriv;
		struct vhba_priv vpriv;
		struct node_priv npriv;
	} priv;
	STAILQ_ENTRY(qsio_hdr) c_list;
};

STAILQ_HEAD(ccb_list, qsio_hdr);

enum {
	NOTIFY_STATUS_OK	= 0x00,
	NOTIFY_STATUS_ERROR	= 0x01,
};

#define INITIATOR_LOGOUT	0xFF02

struct qsio_immed_notify {
	struct    qsio_hdr ccb_h;
	uint8_t  init_int;
	uint8_t  notify_status;         /* Returned NOTIFY status */
	uint16_t r_prt;
	uint32_t fn;
	uint64_t i_prt;
	uint64_t t_prt;
	uint32_t task_tag;
};

enum {
	TASK_TYPE_SIMPLE	= 0,
	TASK_TYPE_HEAD		= 1,
	TASK_TYPE_ORDERED	= 2,
};

struct qsio_scsiio {
	struct qsio_hdr ccb_h;
	uint8_t  *sense_data;
	uint8_t  cdb[16];
	uint8_t  scsi_status;
	uint8_t  task_attr;
	uint8_t  queued:1;
	uint8_t  init_int:3;
	uint8_t  pad1:4;
	uint8_t  sense_len;
	uint16_t r_prt;
	uint16_t pglist_cnt;
	uint8_t  *data_ptr;
	uint32_t dxfer_len;
	uint32_t task_tag;
	uint64_t i_prt;
	uint64_t t_prt;
	void     *istate;
	TAILQ_ENTRY(qsio_scsiio) ta_list;
};

TAILQ_HEAD(ctio_list, qsio_scsiio);
#endif

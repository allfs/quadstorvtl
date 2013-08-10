#ifndef QLA_QDEFS_H_
#define QLA_QDEFS_H_

#include <linux/version.h>

enum {
	QLA_HDR_TYPE_CTIO,
	QLA_HDR_TYPE_NOTIFY,
};

/* fabric independent task management function values */
enum tcm_tmreq_table {
	TMR_ABORT_TASK		= ABORT_TASK,
	TMR_ABORT_TASK_SET	= ABORT_TASK_SET,
	TMR_CLEAR_ACA		= 3,
	TMR_CLEAR_TASK_SET	= 4,
	TMR_LUN_RESET		= LOGICAL_UNIT_RESET,
	TMR_TARGET_WARM_RESET	= TARGET_RESET,
	TMR_TARGET_COLD_RESET	= TARGET_RESET,
	TMR_FABRIC_TMR		= 255,
};

#define	TRANSPORT_SENSE_BUFFER		SCSI_SENSE_BUFFERSIZE
#define TCM_MAX_COMMAND_SIZE		32

/* Used for struct se_cmd->se_cmd_flags */
enum se_cmd_flags_table {
	SCF_OVERFLOW_BIT		= 0x00008000,
	SCF_UNDERFLOW_BIT		= 0x00010000,
};

struct fcbridge;
struct se_session {
	struct kref sess_kref;
	struct qla_tgt_sess *fabric_sess_ptr;
};

struct se_cmd {
	u8			scsi_status;
	u8			task_attr;
	u8			data_dir;
	u8			tmr_function;
	u32			data_length;
	u32			orig_length;
	u32			se_cmd_flags;
	u32			residual_count;
	struct qsio_hdr		*ccb;
	struct scatterlist	*t_data_sg;
	unsigned int		t_data_nents;
	unsigned char           *t_task_cdb;
	unsigned char           __t_task_cdb[16];
	u32			notify_fn;
};

struct fcbridge* fcbridge_new(void *ha, uint32_t id);
void fcbridge_exit(struct fcbridge *fcbridge);
extern struct qla_tgt_func_tmpl qla_sc_template;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 0, 0)
#define alloc_workqueue(nm, a, b)	create_workqueue(nm)
#if (defined(RHEL_MAJOR) && RHEL_MAJOR == 6 && RHEL_MINOR < 3)
#define usleep_range(x, y)		msleep((x)/1000)
#define for_each_set_bit(bit, addr, size) for_each_bit(bit, addr, size)
#endif
#endif

#endif

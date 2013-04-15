#ifndef QLA_QDEFS_H_
#define QLA_QDEFS_H_

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

/* fabric independent task management response values */
enum tcm_tmrsp_table {
	TMR_FUNCTION_COMPLETE		= 0,
	TMR_TASK_DOES_NOT_EXIST		= 1,
	TMR_LUN_DOES_NOT_EXIST		= 2,
	TMR_TASK_STILL_ALLEGIANT	= 3,
	TMR_TASK_FAILOVER_NOT_SUPPORTED	= 4,
	TMR_TASK_MGMT_FUNCTION_NOT_SUPPORTED	= 5,
	TMR_FUNCTION_AUTHORIZATION_FAILED = 6,
	TMR_FUNCTION_REJECTED		= 255,
};

#define	TRANSPORT_SENSE_BUFFER		SCSI_SENSE_BUFFERSIZE
#define TCM_MAX_COMMAND_SIZE		32

/* Used for struct se_cmd->se_cmd_flags */
enum se_cmd_flags_table {
	SCF_SUPPORTED_SAM_OPCODE	= 0x00000001,
	SCF_TRANSPORT_TASK_SENSE	= 0x00000002,
	SCF_EMULATED_TASK_SENSE		= 0x00000004,
	SCF_SCSI_DATA_SG_IO_CDB		= 0x00000008,
	SCF_SCSI_CONTROL_SG_IO_CDB	= 0x00000010,
	SCF_SCSI_NON_DATA_CDB		= 0x00000020,
	SCF_SCSI_TMR_CDB		= 0x00000040,
	SCF_SCSI_CDB_EXCEPTION		= 0x00000080,
	SCF_SCSI_RESERVATION_CONFLICT	= 0x00000100,
	SCF_FUA				= 0x00000200,
	SCF_SE_LUN_CMD			= 0x00000800,
	SCF_SE_ALLOW_EOO		= 0x00001000,
	SCF_BIDI			= 0x00002000,
	SCF_SENT_CHECK_CONDITION	= 0x00004000,
	SCF_OVERFLOW_BIT		= 0x00008000,
	SCF_UNDERFLOW_BIT		= 0x00010000,
	SCF_SENT_DELAYED_TAS		= 0x00020000,
	SCF_ALUA_NON_OPTIMIZED		= 0x00040000,
	SCF_DELAYED_CMD_FROM_SAM_ATTR	= 0x00080000,
	SCF_UNUSED			= 0x00100000,
	SCF_PASSTHROUGH_SG_TO_MEM_NOALLOC = 0x00200000,
	SCF_ACK_KREF			= 0x00400000,
};

struct fcbridge;
struct se_session {
	struct kref sess_kref;
	struct qla_tgt_sess *fabric_sess_ptr;
};

struct se_tmr_req {
	/* Task Management function to be performed */
	u8			function;
	/* Task Management response to send */
	u8			response;
};

struct se_cmd {
	u8			scsi_status;
	u8			task_attr;
	u8			data_dir;
	u32			data_length;
	u32			orig_length;
	u32			se_cmd_flags;
	u32			residual_count;
	struct qsio_hdr		*ccb;
	struct scatterlist	*t_data_sg;
	struct se_tmr_req	*se_tmr_req;
	unsigned int		t_data_nents;
	unsigned char           *t_task_cdb;
	unsigned char           __t_task_cdb[16];
	u32			notify_fn;
};

struct fcbridge* fcbridge_new(void *ha, uint32_t id);
void fcbridge_exit(struct fcbridge *fcbridge);
extern struct qla_tgt_func_tmpl qla_sc_template;

#ifndef MSG_ACA_TAG
#define MSG_ACA_TAG  0x24    /* unsupported */
#endif

#endif

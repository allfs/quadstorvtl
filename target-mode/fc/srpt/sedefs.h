#ifndef QS_SEDEFS_H_
#define QS_SEDEFS_H_

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

struct se_session {
	struct srpt_device	*vha;
	struct srpt_rdma_ch 	*ch;
	atomic_t		cmds;
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
	u16			scsi_sense_length;
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
	int			type;
	int			local_pool;
	int			aborted;
	int			freed;
	uint32_t		tag;
	uint32_t		unpacked_lun;
        /* For SAM Task Attribute */
	int			sam_task_attr;
	enum dma_data_direction	data_direction;
	struct se_session 	*sess;
	STAILQ_ENTRY		(se_cmd) q_list;
};

enum ib_srq_type {
	IB_SRQT_BASIC,
	IB_SRQT_XRC
};

#endif

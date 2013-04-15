/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2011 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#ifndef _QLA_NLNK_H_
#define _QLA_NLNK_H_

#ifndef NETLINK_FCTRANSPORT
#define NETLINK_FCTRANSPORT	20
#endif
#define QL_FC_NL_GROUP_CNT	0

#define NLMSG_MIN_TYPE		0x10	/* 0x10: reserved control messages */
#define FC_TRANSPORT_MSG	NLMSG_MIN_TYPE + 1

#define SCSI_NL_VERSION		1
#define SCSI_NL_MAGIC		0xA1B2

/* 
 * Transport Message Types
 */
#define FC_NL_VNDR_SPECIFIC	0x8000

#ifndef SOL_NETLINK
#define SOL_NETLINK    270
#endif

#ifndef        FCH_EVT_LIP
#define        FCH_EVT_LIP             0x1
#endif

#ifndef        FCH_EVT_LINKUP
#define        FCH_EVT_LINKUP          0x2
#endif

#ifndef        FCH_EVT_LINKDOWN
#define        FCH_EVT_LINKDOWN        0x3
#endif

#ifndef        FCH_EVT_LIPRESET
#define        FCH_EVT_LIPRESET        0x4
#endif

#ifndef        FCH_EVT_RSCN
#define        FCH_EVT_RSCN            0x5
#endif


/*
 * Structures
 */

#ifndef SCSI_NETLINK_H
struct scsi_nl_hdr {
        uint8_t version;
        uint8_t transport;
        uint16_t magic;
        uint16_t msgtype;
        uint16_t msglen;
} __attribute__((aligned(sizeof(uint64_t))));
#endif

struct qla84_mgmt_param {
	union {
		struct {
			uint32_t start_addr;
		} mem; /* for QLA84_MGMT_READ/WRITE_MEM */ 
		struct {
			uint32_t id;
#define QLA84_MGMT_CONFIG_ID_UIF	1
#define QLA84_MGMT_CONFIG_ID_FCOE_COS	2
#define QLA84_MGMT_CONFIG_ID_PAUSE	3
#define QLA84_MGMT_CONFIG_ID_TIMEOUTS	4

			uint32_t param0;
			uint32_t param1;
		} config; /* for QLA84_MGMT_CHNG_CONFIG */

		struct {
			uint32_t type;
#define QLA84_MGMT_INFO_CONFIG_LOG_DATA		1 /* Get Config Log Data */
#define QLA84_MGMT_INFO_LOG_DATA		2 /* Get Log Data */
#define QLA84_MGMT_INFO_PORT_STAT		3 /* Get Port Statistics */
#define QLA84_MGMT_INFO_LIF_STAT		4 /* Get LIF Statistics  */
#define QLA84_MGMT_INFO_ASIC_STAT		5 /* Get ASIC Statistics */
#define QLA84_MGMT_INFO_CONFIG_PARAMS		6 /* Get Config Parameters */
#define QLA84_MGMT_INFO_PANIC_LOG		7 /* Get Panic Log */

			uint32_t context;
/*
 * context definitions for QLA84_MGMT_INFO_CONFIG_LOG_DATA
 */
#define IC_LOG_DATA_LOG_ID_DEBUG_LOG			0
#define IC_LOG_DATA_LOG_ID_LEARN_LOG			1
#define IC_LOG_DATA_LOG_ID_FC_ACL_INGRESS_LOG		2
#define IC_LOG_DATA_LOG_ID_FC_ACL_EGRESS_LOG		3
#define IC_LOG_DATA_LOG_ID_ETHERNET_ACL_INGRESS_LOG	4
#define IC_LOG_DATA_LOG_ID_ETHERNET_ACL_EGRESS_LOG	5
#define IC_LOG_DATA_LOG_ID_MESSAGE_TRANSMIT_LOG		6
#define IC_LOG_DATA_LOG_ID_MESSAGE_RECEIVE_LOG		7
#define IC_LOG_DATA_LOG_ID_LINK_EVENT_LOG		8
#define IC_LOG_DATA_LOG_ID_DCX_LOG			9

/*
 * context definitions for QLA84_MGMT_INFO_PORT_STAT
 */
#define IC_PORT_STATISTICS_PORT_NUMBER_ETHERNET_PORT0	0
#define IC_PORT_STATISTICS_PORT_NUMBER_ETHERNET_PORT1	1
#define IC_PORT_STATISTICS_PORT_NUMBER_NSL_PORT0	2
#define IC_PORT_STATISTICS_PORT_NUMBER_NSL_PORT1	3
#define IC_PORT_STATISTICS_PORT_NUMBER_FC_PORT0		4
#define IC_PORT_STATISTICS_PORT_NUMBER_FC_PORT1		5


/*
 * context definitions for QLA84_MGMT_INFO_LIF_STAT
 */
#define IC_LIF_STATISTICS_LIF_NUMBER_ETHERNET_PORT0	0
#define IC_LIF_STATISTICS_LIF_NUMBER_ETHERNET_PORT1	1
#define IC_LIF_STATISTICS_LIF_NUMBER_FC_PORT0		2
#define IC_LIF_STATISTICS_LIF_NUMBER_FC_PORT1		3
#define IC_LIF_STATISTICS_LIF_NUMBER_CPU		6

		} info; /* for QLA84_MGMT_GET_INFO */
	} u;
};

#define QLFC_MAX_AEN	256
struct qlfc_aen_entry {
	uint16_t event_code;
	uint16_t payload[3];
};

struct qlfc_aen_log {
	uint32_t num_events;
	struct qlfc_aen_entry aen[QLFC_MAX_AEN];
};

struct qla84_msg_mgmt {
	uint16_t cmd;
#define QLA84_MGMT_READ_MEM	0x00
#define QLA84_MGMT_WRITE_MEM	0x01
#define QLA84_MGMT_CHNG_CONFIG	0x02
#define QLA84_MGMT_GET_INFO	0x03
	uint16_t rsrvd;
	struct qla84_mgmt_param mgmtp;/* parameters for cmd */
	uint32_t len; /* bytes in payload following this struct */
	uint8_t payload[0]; /* payload for cmd */
};

struct msg_update_fw {
	/*
	 * diag_fw = 0  operational fw 
	 *	otherwise diagnostic fw 
	 * offset, len, fw_len are present to overcome the current limitation
	 * of 128Kb xfer size. The fw is sent in smaller chunks. Each chunk
	 * specifies the byte "offset" where it fits in the fw buffer. The
	 * number of bytes in each chunk is specified in "len". "fw_len" 
	 * is the total size of fw. The first chunk should start at offset = 0.
	 * When offset+len == fw_len, the fw is written to the HBA.
	 */
	uint32_t diag_fw; 
	uint32_t offset;/* start offset */
	uint32_t len;	/* num bytes in cur xfer */
	uint32_t fw_len; /* size of fw in bytes */
	uint8_t fw_bytes[0];
};

#define EXTERNAL_LOOPBACK		0xF2
#define ENABLE_INTERNAL_LOOPBACK	0x02
#define INTERNAL_LOOPBACK_MASK		0x000E
#define MAX_ELS_FRAME_PAYLOAD		252
#define ELS_OPCODE_BYTE			0x10

struct msg_loopback {
	uint16_t options;
	uint32_t tx_cnt;
	uint32_t iter_cnt;
	uint64_t tx_buf_address;
	uint32_t tx_buf_len;
	uint16_t reserved1[9];

	uint64_t rx_buf_address;
        uint32_t rx_buf_len;
        uint16_t comp_stat;
        uint16_t crc_err_cnt;
        uint16_t disparity_err_cnt;
        uint16_t frame_len_err_cnt;
        uint32_t iter_cnt_last_err;
        uint8_t  cmd_sent;
        uint8_t  reserved;
        uint16_t reserved2[7];

	/*
	 * offset, len, total_len are present to overcome the current limitation
	 * of 128Kb xfer size. The data is sent in smaller chunks. Each chunk
	 * specifies the byte "offset" where it fits in the data buffer. The
	 * number of bytes in each chunk is specified in "len". "total_len"
	 * is the total size of data. The first chunk should start at offset = 0.
	 * When offset+len == total_len, the data is written.
	 */
	uint32_t offset;/* start offset */
	uint32_t len;	/* num bytes in cur xfer */
	uint32_t total_len; /* size of data in bytes */
	uint8_t bytes[0];
} __attribute__ ((packed));

struct qla_scsi_addr {
	uint16_t bus;
	uint16_t target;
} __attribute__ ((packed));

struct qla_ext_dest_addr {
	union {
		uint8_t wwnn[8];
		uint8_t wwpn[8];
		uint8_t id[4];
		struct qla_scsi_addr scsi_addr;
	} dest_addr;
	uint16_t dest_type;
	uint16_t lun;
	uint16_t padding[2];
} __attribute__ ((packed));

struct qla_port_param {
	struct qla_ext_dest_addr fc_scsi_addr;
	uint16_t mode;
	uint16_t speed;
} __attribute__ ((packed));

struct qla_fcp_prio_param {
	uint8_t  version;
	uint8_t  oper;
#define QLFC_FCP_PRIO_DISABLE		0x0
#define QLFC_FCP_PRIO_ENABLE		0x1
#define QLFC_FCP_PRIO_GET_CONFIG	0x2
#define QLFC_FCP_PRIO_SET_CONFIG	0x3
	uint8_t  reserved[2];
	uint32_t fcp_prio_cfg_size;
	uint8_t fcp_prio_cfg[0];
} __attribute__ ((packed));

/* I2C FRU VPD */

#define MAX_FRU_SIZE	36

struct qla_field_address {
	uint16_t offset;
	uint16_t device;
	uint16_t option;
} __attribute__ ((packed));

struct qla_field_info {
	uint8_t version[MAX_FRU_SIZE];
} __attribute__ ((packed));

struct qla_image_version {
	struct qla_field_address field_address;
	struct qla_field_info field_info;
} __attribute__ ((packed));

struct qla_image_version_list {
	uint32_t count;
	struct qla_image_version version[0];
} __attribute__ ((packed));

struct qla_status_reg {
	struct qla_field_address field_address;
	uint8_t status_reg;
	uint8_t reserved[7];
} __attribute__ ((packed));

/****************************/

struct qla_fc_msg {

	uint64_t magic;
#define QL_FC_NL_MAGIC	0x107784DDFCAB1FC1ULL
	uint16_t host_no;
	uint16_t vmsg_datalen;

	uint32_t cmd;
#define QLA84_RESET		0x01
#define QLA84_UPDATE_FW		0x02
#define QLA84_MGMT_CMD		0x03
#define QLFC_GET_AEN		0x04
#define QLFC_LOOPBACK_CMD	0x05
#define QLFC_LOOPBACK_DATA	0x06
#define QLFC_IIDMA		0x07
#define QLFC_FCP_PRIO_CFG_CMD	0x08
#define QLFC_DIAG_MODE          0x09
#define QLFC_SET_FRU_VPD	0x0B
#define QLFC_READ_FRU_STATUS	0x0C
#define QLFC_WRITE_FRU_STATUS	0x0D


	uint32_t error; /* interface or resource error holder*/
#define EXT_STATUS_OK			0
#define EXT_STATUS_ERR			1
#define EXT_STATUS_INVALID_PARAM	6
#define EXT_STATUS_MAILBOX		11
#define EXT_STATUS_NO_MEMORY		17


	union {
		union {
			struct msg_reset {
				/*
				 * diag_fw = 0  for operational fw 
				 * otherwise diagnostic fw 
				 */
				uint32_t diag_fw; 
			} qla84_reset;

			struct msg_update_fw qla84_update_fw;
			struct qla84_msg_mgmt mgmt;
			struct msg_loopback qla_loopback;
		} utok;
	
		union {
			struct qla84_msg_mgmt mgmt;
			struct qlfc_aen_log aen_log;
			struct msg_loopback qla_loopback;
		} ktou;

		struct qla_port_param port_param;

		struct qla_fcp_prio_param fcp_prio_param;
		struct qla_image_version_list fru_img;
		struct qla_status_reg stat_reg;

		uint32_t diag_mode;
#define 	QLFC_RESET_DIAG_MODE	0x0
#define		QLFC_SET_DIAG_MODE	0x1
	} u;
} __attribute__ ((aligned (sizeof(uint64_t))));
	
#endif /* _QLA_NLNK_H_ */

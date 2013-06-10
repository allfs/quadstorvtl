#ifndef QS_SCSIDEFS_H_
#define QS_SCSIDEFS_H_

#define MAX_EVPD_PAGES		16
#define READ_BIT(a, bit) ((a >> bit) & 0x1)
#define READ_24(a, b, c) ((a << 16) | (b << 8) | c)
#define READ_NIBBLE_LOW(a) ((a) & 0xF)
#define READ_NIBBLE_HIGH(a) (((a >> 4) & 0xF))

struct page_info {
	int num_pages;
	uint8_t page_code[MAX_EVPD_PAGES];
};

struct mode_header {
	uint8_t medium_type; /* Medium type */
	uint8_t wp; /* Write protect, buffered mode etc */
} __attribute__ ((__packed__));

struct disconnect_reconnect_page {
	uint8_t page_code; /* Page code, includes ps bit */
	uint8_t page_length; /* page length */
	uint8_t buffer_full_ratio;
	uint8_t buffer_empty_ratio;
	uint16_t bus_inactivity_limit;
	uint16_t disconnect_time_limit;
	uint16_t connect_time_limit;
	uint16_t maximum_burst_size;
	uint8_t dtdc; /*emdp, fard, ... dmtc */
	uint8_t rsvd;
	uint16_t first_burst_size;
} __attribute__ ((__packed__));

struct logical_block_provisioning_page {
	uint8_t device_type;
	uint8_t page_code;
	uint16_t page_length;
	uint8_t threshold_exponent;
	uint8_t dp;
	uint8_t provisioning_type;
	uint8_t rsvd;
} __attribute__ ((__packed__));

#define TDISK_MAXIMUM_TRANSFER_LENGTH			256 /* 1 MB */
#define TDISK_OPTIMAL_TRANSFER_LENGTH			32 /* 256 KB */
#define TDISK_UNMAP_LBA_COUNT				512 /* 2 MB */

#define TDISK_MAXIMUM_TRANSFER_LENGTH_LEGACY		2048/* 256 K */
#define TDISK_OPTIMAL_TRANSFER_LENGTH_LEGACY		256 /* 64 KB */
#define TDISK_UNMAP_LBA_COUNT_LEGACY			4096 /* 2 MB */

#define TDISK_MAXIMUM_UNMAP_BLOCK_DESCRIPTOR_COUNT	16
#define TDISK_XCOPY_SEGMENT_LENGTH			(2 * 1024 * 1024)
#define TDISK_XCOPY_SEGMENT_LENGTH_MAX			(8 * 1024 * 1024)

struct unmap_block_descriptor {
	uint64_t lba;
	uint32_t num_blocks;
	uint32_t rsvd;
} __attribute__ ((__packed__));

struct extended_inquiry_page {
	uint8_t device_type;
	uint8_t page_code;
	uint8_t rsvd;
	uint8_t page_length;
	uint8_t ref_chk;
	uint8_t simpsup;
	uint8_t v_sup;
	uint8_t rsvd1[57];
} __attribute__ ((__packed__));

struct block_device_characteristics_page {
	uint8_t device_type;
	uint8_t page_code;
	uint16_t page_length;
	uint16_t medium_rotation_rate;
	uint8_t rsvd1;
	uint8_t form_factor;
	uint8_t rsvd2[56];
};
	
struct block_limits_page {
	uint8_t device_type;
	uint8_t page_code;
	uint16_t page_length;
	uint8_t rsvd;
	uint8_t maximum_compare_write_length;
	uint16_t optimal_transfer_length_granularity;
	uint32_t maximum_transfer_length;
	uint32_t optimal_transfer_length;
	uint32_t maximum_prefetch_xd_transfer_length;
	uint32_t maximum_unmap_lba_count;
	uint32_t maximum_unmap_block_descriptor_count;
	uint32_t optimal_unmap_granularity;
	uint32_t unmap_granularity_alignment;
	uint8_t rsvd2[28];
} __attribute__ ((__packed__));

struct threshold_descriptor {
	uint8_t threshold_type;
	uint8_t threshold_resource;
	uint16_t reserved;
	uint32_t threshold_count;
} __attribute__ ((__packed__));

struct logical_block_provisioning_mode_page {
	uint8_t page_code;
	uint8_t subpage_code;
	uint8_t pad;
	uint8_t page_length;
	uint8_t situa;
	uint8_t reserved[11];
	struct threshold_descriptor desc;
} __attribute__ ((__packed__));

struct rw_error_recovery_page {
	uint8_t page_code;
	uint8_t page_length;
	uint8_t dcr;
	uint8_t read_retry_count;
	uint8_t obsolete[3];
	uint8_t tpere;
	uint8_t write_retry_count;
	uint8_t reserved;
	uint16_t recovery_type_limit;
} __attribute__ ((__packed__));

struct control_mode_page {
	uint8_t page_code;
	uint8_t page_length;
	uint8_t tst;
	uint8_t qalgo;
	uint8_t tas;
	uint8_t autoload;
	uint16_t ready_aer_period;
	uint16_t busy_timeout_period;
	uint16_t self_test_period;
} __attribute__ ((__packed__));

struct control_mode_dp_page {
	uint8_t page_code;
	uint8_t sub_page_code;
	uint16_t page_length;
	uint8_t lbp_method;
	uint8_t lbp_info_length;
	uint8_t lbp_w;
	uint8_t reserved[25];
} __attribute__ ((__packed__));

	
#define MODE_SENSE_CURRENT_VALUES		0x00
#define MODE_SENSE_CHANGEABLE_VALUES		0x01
#define MODE_SENSE_DEFAULT_VALUES		0x02
#define MODE_SENSE_SAVED_VALUES			0x03
struct mode_parameter_header6 {
	uint8_t mode_data_length;
	uint8_t medium_type;
	uint8_t device_specific_parameter;
	uint8_t block_descriptor_length;
} __attribute__ ((__packed__));

struct mode_parameter_header10 {
	uint16_t mode_data_length;
	uint8_t medium_type;
	uint8_t device_specific_parameter;
	uint8_t rsvd1;
	uint8_t rsvd2;
	uint16_t block_descriptor_length;
} __attribute__ ((__packed__));

struct mode_parameter_block_descriptor {
	uint8_t density_code;
	uint8_t num_blocks[3];
	uint8_t reserved;
	uint8_t block_length[3];
} __attribute__ ((__packed__));

struct read_buffer_header {
	uint32_t buffer_capacity; /* Includes reserved byte */
};

struct receive_diagnostic_header {
	uint8_t page_code;
	uint8_t page_code_specific;
	uint16_t page_length;
};

struct scsi_log_page {
	uint8_t page_code;
	uint8_t reserved;
	uint16_t page_length;
	uint8_t page_data[0];
} __attribute__ ((__packed__));

struct log_parameter {
	uint16_t parameter_code;
	uint8_t parameter_flags;
	uint8_t parameter_length;
} __attribute__ ((__packed__));

struct log_parameter32 {
	uint16_t parameter_code;
	uint8_t parameter_flags;
	uint8_t parameter_length;
	uint32_t parameter_value;
} __attribute__ ((__packed__));

struct log_parameter16 {
	uint16_t parameter_code;
	uint8_t parameter_flags;
	uint8_t parameter_length;
	uint16_t parameter_value;
} __attribute__ ((__packed__));

struct log_parameter8 {
	uint16_t parameter_code;
	uint8_t parameter_flags;
	uint8_t parameter_length;
	uint8_t parameter_value;
} __attribute__ ((__packed__));

/* Log Pages */
#define WRITE_ERROR_LOG_PAGE		0x02
#define READ_ERROR_LOG_PAGE		0x03
#define VOLUME_STATISTICS_LOG_PAGE	0x17
#define COMPRESSION_LOG_PAGE		0x32
#define TAPE_ALERT_LOG_PAGE		0x2E
#define TAPE_CAPACITY_LOG_PAGE		0x31 /* Check if specific to drive */

#ifndef SYNCHRONIZE_CACHE_16
#define SYNCHRONIZE_CACHE_16		0x91
#endif

#ifndef VERIFY_12 
#define VERIFY_12			0xaf
#endif

#ifndef VERIFY_16 
#define VERIFY_16			0x8f
#endif

#ifndef UNMAP
#define UNMAP				0x42
#endif

#ifndef WRITE_SAME
#define WRITE_SAME			0x41
#endif

#ifndef WRITE_SAME_16
#define WRITE_SAME_16			0x93
#endif

#ifndef SECURITY_PROTOCOL_IN
#define SECURITY_PROTOCOL_IN		0xA2
#endif

#ifndef SECURITY_PROTOCOL_OUT
#define SECURITY_PROTOCOL_OUT		0xB5
#endif

#ifndef EXTENDED_COPY
#define EXTENDED_COPY			0x83
#endif

#ifndef RECEIVE_COPY_RESULTS
#define RECEIVE_COPY_RESULTS		0x84
#endif

#ifndef COMPARE_AND_WRITE
#define COMPARE_AND_WRITE		0x89
#endif

#ifndef PERSISTENT_RESERVE_IN
#define PERSISTENT_RESERVE_IN		0x5e
#endif

#ifndef PERSISTENT_RESERVE_OUT
#define PERSISTENT_RESERVE_OUT		0x5f
#endif

#ifndef EXCHANGE_MEDIUM
#define EXCHANGE_MEDIUM		0xa6
#endif
#ifndef POSITION_TO_ELEMENT
#define POSITION_TO_ELEMENT   0x2b
#endif
#ifndef INITIALIZE_ELEMENT_STATUS
#define INITIALIZE_ELEMENT_STATUS	0x07
#endif
#ifndef INITIALIZE_ELEMENT_STATUS_WITH_RANGE
#define INITIALIZE_ELEMENT_STATUS_WITH_RANGE	0x37
#endif
#ifndef INITIALIZE_ELEMENT_STATUS_WITH_RANGEP
#define INITIALIZE_ELEMENT_STATUS_WITH_RANGEP	0xE7
#endif
#ifndef POSITION_TO_ELEMENT
#define POSITION_TO_ELEMENT		0x2b
#endif
#ifndef LOAD_UNLOAD
#define LOAD_UNLOAD			0x1B
#endif
#ifndef REWIND
#define REWIND				0x01
#endif
#ifndef LOCATE
#define LOCATE				0x2B
#endif
#ifndef LOCATE_16
#define LOCATE_16			0x92
#endif
#ifndef REPORT_DENSITY_SUPPORT
#define REPORT_DENSITY_SUPPORT		0x44
#endif
#ifndef READ_ATTRIBUTE
#define READ_ATTRIBUTE			0x8C
#endif
#ifndef WRITE_ATTRIBUTE
#define WRITE_ATTRIBUTE			0x8D
#endif
#ifndef SET_CAPACITY
#define SET_CAPACITY			0x0B
#endif
#ifndef FORMAT_MEDIUM
#define FORMAT_MEDIUM			0x04
#endif
#ifndef RECEIVE_DIAGNOSTIC_RESULTS
#define RECEIVE_DIAGNOSTIC_RESULTS	0x1C
#endif
#ifndef SEND_DIAGNOSTIC
#define SEND_DIAGNOSTIC			0x1D
#endif
#ifndef LOCATE_16
#define LOCATE_16			0x92
#endif
#endif

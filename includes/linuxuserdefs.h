#ifndef LINUXUSERDEFS_H_
#define LINUXUSERDEFS_H_

#define SCSI_STATUS_OK                  0x00
#define SCSI_STATUS_CHECK_COND          0x02
#define SCSI_STATUS_COND_MET            0x04
#define SCSI_STATUS_BUSY                0x08
#define SCSI_STATUS_INTERMED            0x10
#define SCSI_STATUS_INTERMED_COND_MET   0x14
#define SCSI_STATUS_RESERV_CONFLICT     0x18
#define SCSI_STATUS_CMD_TERMINATED      0x22    /* Obsolete in SAM-2 */
#define SCSI_STATUS_QUEUE_FULL          0x28
#define SCSI_STATUS_ACA_ACTIVE          0x30
#define SCSI_STATUS_TASK_ABORTED        0x40

#define SSD_KEY_UNIT_ATTENTION		UNIT_ATTENTION
#define SSD_KEY_NOT_READY		NOT_READY
#define SSD_KEY_BLANK_CHECK		BLANK_CHECK
#define SSD_KEY_HARDWARE_ERROR		HARDWARE_ERROR
#define SSD_KEY_ILLEGAL_REQUEST		ILLEGAL_REQUEST
#define SSD_KEY_RECOVERED_ERROR		RECOVERED_ERROR
#define SSD_KEY_ABORTED_COMMAND		ABORTED_COMMAND
#define SSD_KEY_NO_SENSE		NO_SENSE
#define SSD_ERRCODE                     0x7F
#define         SSD_CURRENT_ERROR       0x70
#define         SSD_DEFERRED_ERROR      0x71
#define SSD_ERRCODE_VALID       0x80    
#define SSD_ILI         0x20
#define SSD_EOM         0x40
#define SSD_FILEMARK    0x80
#define T_SEQUENTIAL			TYPE_TAPE
#define T_CHANGER			TYPE_MEDIUM_CHANGER
#define T_DIRECT			TYPE_DISK
#define MODE_SENSE_6			MODE_SENSE
#define MODE_SELECT_6			MODE_SELECT
#define PREVENT_ALLOW			ALLOW_MEDIUM_REMOVAL
#define REPORT_LUNS           		0xa0

#ifndef be64toh
#if __BYTE_ORDER == __BIG_ENDIAN
#define htole16(x)		bswap_16(x)
#define le16toh(x)		bswap_16(x)
#define htole32(x)		bswap_32(x)
#define le32toh(x)		bswap_32(x)
#define htole64(x)		bswap_64(x)
#define le64toh(x)		bswap_64(x)
#define htobe16(x)		(x)
#define be16toh(x)		(x)
#define htobe32(x)		(x)
#define be32toh(x)		(x)
#define htobe64(x)		(x)
#define be64toh(x)		(x)
#elif __BYTE_ORDER == __LITTLE_ENDIAN
#define htole16(x)		(x)
#define le16toh(x)		(x)
#define htole32(x)		(x)
#define le32toh(x)		(x)
#define htole64(x)		(x)
#define le64toh(x)		(x)
#define htobe16(x)		bswap_16(x)
#define be16toh(x)		bswap_16(x)
#define htobe32(x)		bswap_32(x)
#define be32toh(x)		bswap_32(x)
#define htobe64(x)		bswap_64(x)
#define be64toh(x)		bswap_64(x)
#else
#error "unknown endianess!"
#endif
#endif

#define MTCOMP		MTCOMPRESSION
#define MTSETBSIZ	MTSETBLK
#define MTCACHE		MTSETDRVBUFFER

#define MOUNT_PATH	"/bin/mount"
#define UMOUNT_PATH	"/bin/umount"
#define DISK_IDENT_SIZE		256
#endif

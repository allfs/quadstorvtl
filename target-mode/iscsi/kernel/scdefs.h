#ifndef SCDEFS_H_
#define SCDEFS_H_

#include "iscsi.h" 

#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
void digest_write_ctio(struct crypto_tfm *tfm, struct iscsi_cmnd *cmnd, struct qsio_scsiio *ctio, u8 *crc);
void digest_read_ctio(struct crypto_tfm *tfm, struct iscsi_cmnd *cmnd, struct qsio_scsiio *ctio, u8 *crc);
#else
void digest_write_ctio(struct hash_desc *hash, struct iscsi_cmnd *cmnd, struct qsio_scsiio *ctio, u8 *crc);
void digest_read_ctio(struct hash_desc *hash, struct iscsi_cmnd *cmnd, struct qsio_scsiio *ctio, u8 *crc);
#endif
#else
void digest_write_ctio(struct chksum_ctx *mctx, struct iscsi_cmnd *cmnd, struct qsio_scsiio *ctio, u8 *crc);
void digest_read_ctio(struct chksum_ctx *mctx, struct iscsi_cmnd *cmnd, struct qsio_scsiio *ctio, u8 *crc);
#endif
void iscsi_cmnd_recv_pdu(struct iscsi_conn *conn, struct qsio_scsiio *ctio, u32 offset, __u32 size);
struct qsio_scsiio * iscsi_construct_ctio(struct iscsi_conn *conn, struct iscsi_cmnd *cmnd);
int iscsi_target_new_device_cb (struct tdevice *newdevice);
int iscsi_target_remove_device_cb(struct tdevice *removedevice, int tid, void *hpriv);
void iscsi_target_disable_device_cb(struct tdevice *removedevice, int tid, void *hpriv);
void iscsi_cmnd_send_pdu(struct iscsi_conn *conn, struct iscsi_cmnd *cmnd);
int do_recv_ctio(struct iscsi_conn *conn, int state);
int do_send_ctio(struct iscsi_conn *conn, int state);

extern struct qs_interface_cbs icbs;
extern uint64_t t_prt;
#endif

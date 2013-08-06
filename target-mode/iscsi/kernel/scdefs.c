#include "iscsi.h"
#include "digest.h"
#include "iscsi_dbg.h"
#include "scdefs.h"

#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
void digest_write_ctio(struct crypto_tfm *tfm, struct iscsi_cmnd *cmnd,
			     struct qsio_scsiio *ctio, u8 *crc)
{
	struct scatterlist sg[8];
	__u32 datasize;
	__u8 pad_bytes[4];
	__u32 pad;
	struct iscsi_data_in_hdr *rsp_hdr = (struct iscsi_data_in_hdr *)&cmnd->pdu.bhs;

	crypto_digest_init(tfm);
	if (rsp_hdr->cmd_status == SAM_STAT_CHECK_CONDITION)
	{
		sg[0].page = virt_to_page(ctio->sense_data);
		sg[0].offset = page_offset(ctio->sense_data);
		sg[0].length = ctio->sense_len + 2; 
		crypto_digest_update(tfm, sg, 1);
	}
	else if (ctio->pglist_cnt)
	{
		int i, j = 0;
		struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);
		int pg_offset;
		int min;

		pg_offset = cmnd->orig_start_pg_offset;
		for (i = cmnd->orig_start_pg_idx; i <= cmnd->end_pg_idx; i++)
		{
			struct pgdata *pgtmp = pglist[i];

			if (j == 8)
			{
				crypto_digest_update(tfm, sg, j);
				j = 0;
			}

			if (i == cmnd->end_pg_idx)
				min = cmnd->end_pg_offset - pg_offset;
			else
				min = pgtmp->pg_len - pg_offset;

			sg[j].page = pgtmp->page;
			sg[j].offset = pg_offset + pgtmp->pg_offset;
			sg[j].length = min;
			j++;
			pg_offset = 0;
		}
		crypto_digest_update(tfm, sg, j);
	}
	else
	{
		sg[0].page = virt_to_page(ctio->data_ptr);
		sg[0].offset = page_offset(ctio->data_ptr);
		sg[0].length = ctio->dxfer_len;
		crypto_digest_update(tfm, sg, 1);
	}

	datasize = cmnd->pdu.datasize;
	pad = datasize & 0x03;
	if (pad && datasize)
	{
		memset(pad_bytes, 0, 4);
		pad = 4 - pad;
		sg[0].page = virt_to_page(pad_bytes);
		sg[0].offset = page_offset(pad_bytes);
		sg[0].length = pad;
		crypto_digest_update(tfm, sg, 1);
	}

	crypto_digest_final(tfm, crc);
}

void digest_read_ctio(struct crypto_tfm *tfm, struct iscsi_cmnd *cmnd,
			     struct qsio_scsiio *ctio, u8 *crc)
{
	__u32 datasize;
	__u8 pad_bytes[4];
	__u32 pad;

	crypto_digest_init(tfm);
	if (ctio->pglist_cnt && ctio->dxfer_len)
	{
		int i, j = 0, min, max;
		int pg_offset;
		struct scatterlist sg[8];
		struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);

		pg_offset = cmnd->orig_start_pg_offset;
		if (pg_offset)
			max = cmnd->read_pg_idx+1;
		else
			max = cmnd->read_pg_idx;

		for (i = cmnd->orig_start_pg_idx; i < max; i++)
		{
			struct pgdata *pgtmp = pglist[i];
			if (j == 8)
			{
				crypto_digest_update(tfm, sg, j);
				j = 0;
			}

			if (i == cmnd->read_pg_idx)
				min = cmnd->read_pg_offset - pg_offset;
			else
				min = pgtmp->pg_len - pg_offset;

			sg[j].page = pgtmp->page;
			sg[j].offset = pg_offset + pgtmp->pg_offset;
			sg[j].length = min;
			pg_offset = 0;
			j++;
		}

		crypto_digest_update(tfm, sg, j);
	}
	else if (ctio->dxfer_len)
	{
		struct scatterlist sg[1];

		sg[0].page = virt_to_page(ctio->data_ptr);
		sg[0].offset = page_offset(ctio->data_ptr);
		sg[0].length = ctio->dxfer_len;
		crypto_digest_update(tfm, sg, 1);
	}
 
	datasize = cmnd->pdu.datasize;
	pad = datasize & 0x03;
	if (pad && datasize)
	{
		struct scatterlist sg[1];
		memset(pad_bytes, 0, 4);
		pad = 4 - pad;
		sg[0].page = virt_to_page(pad_bytes);
		sg[0].offset = page_offset(pad_bytes);
		sg[0].length = pad;
		crypto_digest_update(tfm, sg, 1);
	}

	crypto_digest_final(tfm, crc);
}
#else
void digest_write_ctio(struct hash_desc *hash, struct iscsi_cmnd *cmnd,
			     struct qsio_scsiio *ctio, u8 *crc)
{
	__u32 datasize;
	__u8 pad_bytes[4];
	__u32 pad;
	struct iscsi_data_in_hdr *rsp_hdr = (struct iscsi_data_in_hdr *)&cmnd->pdu.bhs;
	int nbytes = 0;

	crypto_hash_init(hash);
	if (rsp_hdr->cmd_status == SAM_STAT_CHECK_CONDITION)
	{
		struct scatterlist sg[1];
		sg_set_page(&sg[0], virt_to_page(ctio->sense_data), ctio->sense_len + 2,  page_offset(ctio->sense_data));
		crypto_hash_update(hash, sg, ctio->sense_len+2);
	}
	else if (ctio->pglist_cnt)
	{
		int i, j = 0;
		struct scatterlist sg[8];
		struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);
		int pg_offset;
		int min;

		pg_offset = cmnd->orig_start_pg_offset;
		for (i = cmnd->orig_start_pg_idx; i <= cmnd->end_pg_idx; i++)
		{
			struct pgdata *pgtmp = pglist[i];

			if (j == 8)
			{
				crypto_hash_update(hash, sg, nbytes);
				j = 0;
				nbytes = 0;
			}

			if (i == cmnd->end_pg_idx)
				min = cmnd->end_pg_offset - pg_offset;
			else
				min = pgtmp->pg_len - pg_offset;

			sg_set_page(&sg[j], pgtmp->page, min, pgtmp->pg_offset + pg_offset);
			nbytes += min;
			j++;
			pg_offset = 0;
		}
		crypto_hash_update(hash, sg, nbytes);
	}
	else
	{
		struct scatterlist sg[1];
		sg_set_page(&sg[0], virt_to_page(ctio->data_ptr), ctio->dxfer_len, page_offset(ctio->data_ptr));
		crypto_hash_update(hash, sg, ctio->dxfer_len);
	}
 
	datasize = cmnd->pdu.datasize;
	pad = datasize & 0x03;
	if (pad && datasize)
	{
		struct scatterlist sg[1];
		memset(pad_bytes, 0, 4);
		pad = 4 - pad;
		sg_set_page(&sg[0], virt_to_page(pad_bytes), pad, page_offset(pad_bytes));
		crypto_hash_update(hash, sg, pad);
	}

	crypto_hash_final(hash, crc);
}

void digest_read_ctio(struct hash_desc *hash, struct iscsi_cmnd *cmnd,
			     struct qsio_scsiio *ctio, u8 *crc)
{
	__u32 datasize;
	__u8 pad_bytes[4];
	__u32 pad;

	crypto_hash_init(hash);
	if (ctio->pglist_cnt && ctio->dxfer_len)
	{
		int i, j = 0, nbytes = 0, min, max;
		int pg_offset;
		struct scatterlist sg[8];
		struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);

		pg_offset = cmnd->orig_start_pg_offset;
		if (pg_offset)
			max = cmnd->read_pg_idx+1;
		else
			max = cmnd->read_pg_idx;

		for (i = cmnd->orig_start_pg_idx; i < max; i++)
		{
			struct pgdata *pgtmp = pglist[i];
			if (j == 8)
			{
				crypto_hash_update(hash, sg, nbytes);
				j = 0;
				nbytes = 0;
			}

			if (i == cmnd->read_pg_idx)
				min = cmnd->read_pg_offset - pg_offset;
			else
				min = pgtmp->pg_len - pg_offset;

			sg_set_page(&sg[j], pgtmp->page, min, pgtmp->pg_offset + pg_offset);
			nbytes += min;
			pg_offset = 0;
			j++;
		}

		crypto_hash_update(hash, sg, nbytes);
	}
	else if (ctio->dxfer_len)
	{
		struct scatterlist sg[1];

		sg_set_page(&sg[0], virt_to_page(ctio->data_ptr), ctio->dxfer_len, page_offset(ctio->data_ptr));
		crypto_hash_update(hash, sg, ctio->dxfer_len);
	}
 
	datasize = cmnd->pdu.datasize;
	pad = datasize & 0x03;
	if (pad && datasize)
	{
		struct scatterlist sg[1];
		memset(pad_bytes, 0, 4);
		pad = 4 - pad;
		sg_set_page(&sg[0], virt_to_page(pad_bytes), pad, page_offset(pad_bytes));
		crypto_hash_update(hash, sg, pad);
	}

	crypto_hash_final(hash, crc);
}
#endif
#else
void digest_write_ctio(struct chksum_ctx *mctx, struct iscsi_cmnd *cmnd,
			     struct qsio_scsiio *ctio, u8 *crc)
{
	__u32 datasize;
	__u8 pad_bytes[4];
	__u32 pad;
	struct iscsi_data_in_hdr *rsp_hdr = (struct iscsi_data_in_hdr *)&cmnd->pdu.bhs;

	chksum_init(mctx);
	if (rsp_hdr->cmd_status == SAM_STAT_CHECK_CONDITION)
	{
		chksum_update(mctx, ctio->sense_data, ctio->sense_len + 2);
	}
	else if (ctio->pglist_cnt)
	{
		int i;
		struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);
		int pg_offset;
		int min;

		pg_offset = cmnd->orig_start_pg_offset;
		for (i = cmnd->orig_start_pg_idx; i <= cmnd->end_pg_idx; i++)
		{
			struct pgdata *pgtmp = pglist[i];

			if (i == cmnd->end_pg_idx)
				min = cmnd->end_pg_offset - pg_offset;
			else
				min = pgtmp->pg_len - pg_offset;

			chksum_update(mctx, (u8 *)(pgdata_page_address(pgtmp)) + pgtmp->pg_offset + pg_offset, min);
			pg_offset = 0;
		}
	}
	else
	{
		chksum_update(mctx, ctio->data_ptr, ctio->dxfer_len);
	}

	datasize = cmnd->pdu.datasize;
	pad = datasize & 0x03;
	if (pad && datasize)
	{
		memset(pad_bytes, 0, 4);
		pad = 4 - pad;
		chksum_update(mctx, pad_bytes, pad);
	}

	chksum_final(mctx, crc);
}

void digest_read_ctio(struct chksum_ctx *mctx, struct iscsi_cmnd *cmnd,
			     struct qsio_scsiio *ctio, u8 *crc)
{
	__u32 datasize;
	__u8 pad_bytes[4];
	__u32 pad;

	chksum_init(mctx);
	if (ctio->pglist_cnt && ctio->dxfer_len)
	{
		int i, min, max;
		int pg_offset;
		struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);

		pg_offset = cmnd->orig_start_pg_offset;
		if (pg_offset)
			max = cmnd->read_pg_idx+1;
		else
			max = cmnd->read_pg_idx;

		for (i = cmnd->orig_start_pg_idx; i < max; i++)
		{
			struct pgdata *pgtmp = pglist[i];

			if (i == cmnd->read_pg_idx)
				min = cmnd->read_pg_offset - pg_offset;
			else
				min = pgtmp->pg_len - pg_offset;

			chksum_update(mctx, (u8 *)(pgdata_page_address(pgtmp)) + pgtmp->pg_offset + pg_offset, min);
			pg_offset = 0;
		}
	}
	else if (ctio->dxfer_len)
	{
		chksum_update(mctx, ctio->data_ptr, ctio->dxfer_len);
	}
 
	datasize = cmnd->pdu.datasize;
	pad = datasize & 0x03;
	if (pad && datasize)
	{
		memset(pad_bytes, 0, 4);
		pad = 4 - pad;
		chksum_update(mctx, pad_bytes, pad);
	}

	chksum_final(mctx, crc);
}
#endif

static struct iscsi_cmnd *
create_rsp_setup2(struct iscsi_cmnd *req, struct qsio_scsiio *ctio, int length, int buffer_offset, __u32 *data_sn, int is_last)
{
	struct iscsi_cmnd *rsp;
	struct iscsi_data_in_hdr *rsp_hdr;
	struct iscsi_scsi_cmd_hdr *req_hdr = cmnd_hdr(req);

	rsp = iscsi_cmnd_create_rsp_cmnd(req, is_last);
	rsp_hdr = (struct iscsi_data_in_hdr *)&rsp->pdu.bhs;
	rsp_hdr->opcode = ISCSI_OP_SCSI_DATA_IN;
	rsp_hdr->itt = req_hdr->itt;
	rsp_hdr->ttt = cpu_to_be32(ISCSI_RESERVED_TAG);
	rsp_hdr->buffer_offset = cpu_to_be32(buffer_offset);
	rsp_hdr->data_sn = cpu_to_be32((*data_sn)++);

	if (ctio->dxfer_len <= buffer_offset)
		rsp->pdu.datasize = 0;
	else if (length > (ctio->dxfer_len - buffer_offset))
		rsp->pdu.datasize = ctio->dxfer_len - buffer_offset;
	else
		rsp->pdu.datasize = length;

	rsp->ctio = ctio;
	rsp->write_iov_offset = buffer_offset;
	rsp->write_iov_len = length;
	return rsp;
}

static void
create_scsi_data_rsp(struct iscsi_cmnd *req, struct qsio_scsiio *ctio)
{
	struct iscsi_cmnd *rsp = NULL;
	struct iscsi_data_in_hdr *rsp_hdr;
        struct iscsi_sess_param *param = &req->conn->session->param;
	u32 size;
	QS_LIST_HEAD(send);
	int remaining;
	int done = 0;
	int seq_done = 0;
	int is_last = 0;
	__u32 data_sn = 0;
	int orig_dxfer_len = ctio->dxfer_len;

	size = cmnd_read_size(req);
	if ((size - ctio->dxfer_len) < 0)
	{
		ctio->dxfer_len = size; 
	}

	if ((ctio->dxfer_len) & 0x3)
	{
		int pad = 4 - ((ctio->dxfer_len) & 0x3);
		memset(ctio->data_ptr+ctio->dxfer_len, 0, pad);
	}

	remaining = (ctio->dxfer_len + 3) & -4;
	while (done != remaining)
	{
		int min;

		min = (remaining - done);
		if (min > param->max_xmit_data_length)
			min = param->max_xmit_data_length;

		if ((min + seq_done) > param->max_burst_length)
			min = param->max_burst_length - seq_done;

		if (((done+min) == remaining))
		{
			is_last = 1; 
		}

		rsp = create_rsp_setup2(req, ctio, min, done, &data_sn, is_last);
		list_add_tail(&rsp->list, &send);
		done += min;
		seq_done += min;

		if (seq_done == param->max_burst_length)
		{
			seq_done = 0;
			if (done != remaining)
			{
				rsp_hdr = (struct iscsi_data_in_hdr *)&rsp->pdu.bhs;
				rsp_hdr->flags = ISCSI_FLG_FINAL;
			}
		}
	}

	rsp_hdr = (struct iscsi_data_in_hdr *)&rsp->pdu.bhs;
	rsp_hdr->flags = ISCSI_FLG_FINAL| ISCSI_FLG_STATUS;
	rsp_hdr->cmd_status = SAM_STAT_GOOD;

	if ((size - orig_dxfer_len) > 0) {
		rsp_hdr->flags |= ISCSI_FLG_RESIDUAL_UNDERFLOW;
		rsp_hdr->residual_count = cpu_to_be32(size - orig_dxfer_len);
	}
	else if ((size - orig_dxfer_len) < 0)
	{
		rsp_hdr->flags |= ISCSI_FLG_RESIDUAL_OVERFLOW;
		rsp_hdr->residual_count = cpu_to_be32(orig_dxfer_len - size); 
	}

	iscsi_cmnds_init_write(&send, 1);
	return;
}

/* For iSCSI padding */
static void
correct_pglist(struct qsio_scsiio *ctio)
{
	struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);
	int pglist_cnt = ctio->pglist_cnt;
	struct pgdata *pgtmp;
	int pad;

	if (!ctio->dxfer_len)
		return;

	if (!(ctio->dxfer_len & 0x3))
		return;

	pad = 4 - (ctio->dxfer_len & 0x3);
	pgtmp = pglist[pglist_cnt - 1];
	memset((u8 *)pgdata_page_address(pgtmp)+pgtmp->pg_len, 0, pad);
	pgtmp->pg_len += pad;
}

static struct iscsi_cmnd *
create_rsp_setup(struct iscsi_cmnd *req, struct qsio_scsiio *ctio, int length, int buffer_offset, int *start_pg_idx, int *end_pg_idx, int *start_pg_offset, int *end_pg_offset, __u32 *data_sn, int is_last)
{
	struct iscsi_cmnd *rsp;
	struct iscsi_data_in_hdr *rsp_hdr;
	struct iscsi_scsi_cmd_hdr *req_hdr = cmnd_hdr(req);
	int i;
	struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);
	int done = 0;

	rsp = iscsi_cmnd_create_rsp_cmnd(req, is_last);
	rsp_hdr = (struct iscsi_data_in_hdr *)&rsp->pdu.bhs;
	rsp_hdr->opcode = ISCSI_OP_SCSI_DATA_IN;
	rsp_hdr->itt = req_hdr->itt;
	rsp_hdr->ttt = cpu_to_be32(ISCSI_RESERVED_TAG);
	rsp_hdr->buffer_offset = cpu_to_be32(buffer_offset);
	rsp_hdr->data_sn = cpu_to_be32((*data_sn)++);

	if (ctio->dxfer_len <= buffer_offset)
		rsp->pdu.datasize = 0;
	else if (length > (ctio->dxfer_len - buffer_offset))
		rsp->pdu.datasize = ctio->dxfer_len - buffer_offset;
	else
		rsp->pdu.datasize = length;

	rsp->ctio = ctio;

	rsp->start_pg_idx = rsp->orig_start_pg_idx = *end_pg_idx;
	rsp->start_pg_offset = rsp->orig_start_pg_offset = *end_pg_offset;

	for (i = *end_pg_idx; i < ctio->pglist_cnt; i++)
	{
		struct pgdata *pgtmp = pglist[i];
		int pg_offset;
		int min;

		if (i == *end_pg_idx)
		{
			min = pgtmp->pg_len - *end_pg_offset;
			pg_offset = *end_pg_offset;
		}
		else
		{
			min = pgtmp->pg_len;
			pg_offset = 0;
		}

		if ((done + min) > length)
			min = (length - done);
		done += min;
		pg_offset += min;
		if (done == length)
		{
			rsp->end_pg_idx = *end_pg_idx = i;
			rsp->end_pg_offset = *end_pg_offset = pg_offset;
			if (pg_offset == pgtmp->pg_len)
			{
				(*end_pg_idx)++;
				*end_pg_offset = 0;
			}
			return rsp;
		}
	}
	BUG();
	return NULL; 
}

static void
create_scsi_data_rsp2(struct iscsi_cmnd *req, struct qsio_scsiio *ctio, int sendstatus, uint32_t *ret_residual, uint8_t *ret_flags)
{
	struct iscsi_cmnd *rsp = NULL;
	struct iscsi_data_in_hdr *rsp_hdr = NULL;
        struct iscsi_sess_param *param = &req->conn->session->param;
	u32 size;
	int remaining;
	int start_pg_idx = 0;
	int end_pg_idx = 0;
	int start_pg_offset = 0;
	int end_pg_offset = 0;
	int done = 0;
	int seq_done = 0;
	int is_last = 0;
	__u32 data_sn = 0;
	QS_LIST_HEAD(send);

	correct_pglist(ctio);
	remaining = (ctio->dxfer_len + 3) & -4;

	while (done != remaining)
	{
		int min;

		min = (remaining - done);
		if (min > param->max_xmit_data_length)
			min = param->max_xmit_data_length;

		if ((min + seq_done) > param->max_burst_length)
			min = param->max_burst_length - seq_done;

		if (sendstatus && ((done+min) == remaining))
		{
			is_last = 1; 
		}

		rsp = create_rsp_setup(req, ctio, min, done, &start_pg_idx, &end_pg_idx, &start_pg_offset, &end_pg_offset, &data_sn, is_last);

		list_add_tail(&rsp->list, &send);
		done += min;
		seq_done += min;
		if (seq_done == param->max_burst_length)
		{
			seq_done = 0;
			if (done != remaining)
			{
				rsp_hdr = (struct iscsi_data_in_hdr *)&rsp->pdu.bhs;
				rsp_hdr->flags = ISCSI_FLG_FINAL;
			}
		}
	}

	rsp_hdr = (struct iscsi_data_in_hdr *)&rsp->pdu.bhs;
	rsp_hdr->flags = ISCSI_FLG_FINAL;

	if (sendstatus)
	{
		rsp_hdr->flags |= ISCSI_FLG_STATUS;
		rsp_hdr->cmd_status = SAM_STAT_GOOD;
	}

	size = cmnd_read_size(req);
	if (size < ctio->dxfer_len)
	{
		struct iscsi_scsi_cmd_hdr *req_hdr = cmnd_hdr(req);
		DEBUG_WARN_NEW("create_scsi_data_rsp2: cmd read_size %u dxfer_len %u cmd %x scb %x\n", size, ctio->dxfer_len, ctio->cdb[0], req_hdr->scb[0]);
	}

	if (sendstatus && size > ctio->dxfer_len) {
		rsp_hdr->flags |= ISCSI_FLG_RESIDUAL_UNDERFLOW;
		rsp_hdr->residual_count = cpu_to_be32(size - ctio->dxfer_len);
	}
	else if (size > ctio->dxfer_len) {
		*ret_flags = ISCSI_FLG_RESIDUAL_UNDERFLOW;
		*ret_residual =  cpu_to_be32(size - ctio->dxfer_len); 
	}
	iscsi_cmnds_init_write(&send, sendstatus);
	return;
}

static void
create_scsi_rsp(struct iscsi_cmnd *req, struct qsio_scsiio *ctio)
{
	struct iscsi_cmnd *rsp;
	struct iscsi_scsi_cmd_hdr *req_hdr = cmnd_hdr(req);
	struct iscsi_scsi_rsp_hdr *rsp_hdr;

	rsp = iscsi_cmnd_create_rsp_cmnd(req, 1);

	rsp_hdr = (struct iscsi_scsi_rsp_hdr *)&rsp->pdu.bhs;
	rsp_hdr->opcode = ISCSI_OP_SCSI_RSP;
	rsp_hdr->flags = ISCSI_FLG_FINAL;
	rsp_hdr->response = ISCSI_RESPONSE_COMMAND_COMPLETED;
	rsp_hdr->cmd_status = ctio->scsi_status;
	rsp_hdr->itt = req_hdr->itt;
	rsp->pdu.datasize = 0;
	rsp->ctio = ctio;
	iscsi_cmnd_init_write(rsp, 1);
	return;
}

static struct iscsi_cmnd *
iscsi_create_sense_rsp(struct iscsi_cmnd *req, struct qsio_scsiio *ctio, uint32_t ret_residual, uint8_t ret_flags)
{
	struct iscsi_cmnd *rsp;
	struct iscsi_scsi_rsp_hdr *rsp_hdr;

	rsp = iscsi_cmnd_create_rsp_cmnd(req, 1);

	rsp_hdr = (struct iscsi_scsi_rsp_hdr *)&rsp->pdu.bhs;
	rsp_hdr->opcode = ISCSI_OP_SCSI_RSP;
	rsp_hdr->flags = ISCSI_FLG_FINAL;
	rsp_hdr->response = ISCSI_RESPONSE_COMMAND_COMPLETED;
	rsp_hdr->cmd_status = SAM_STAT_CHECK_CONDITION;
	rsp_hdr->itt = cmnd_hdr(req)->itt;
	rsp->ctio = ctio;
	rsp->pdu.datasize = ctio->sense_len+2;
	if ((ctio->sense_len+2) & 0x3)
	{
		int pad = 4 - ((ctio->sense_len+2) & 0x3);
		/* We take this away now, since the sense allocation takes care of padding */
		memset(ctio->sense_data+ctio->sense_len+2, 0, pad);
	}
	*((__u16 *)ctio->sense_data) = cpu_to_be16(ctio->sense_len);

	if (ret_flags) {
		rsp_hdr->flags |= ret_flags;
		rsp_hdr->residual_count = ret_residual;
	}
	iscsi_cmnd_init_write(rsp, 1);
	return rsp;
}

void
iscsi_cmnd_recv_pdu(struct iscsi_conn *conn, struct qsio_scsiio *ctio, u32 offset, __u32 size)
{
	struct iscsi_cmnd *cmnd = conn->read_cmnd;
	int read_pg_idx, read_pg_offset;

	conn->read_ctio = ctio;
	conn->read_size = (size + 3) & -4;
	conn->read_offset = offset;

	ctio_idx_offset(offset, &read_pg_idx, &read_pg_offset);
	cmnd->orig_start_pg_idx = cmnd->read_pg_idx = read_pg_idx;
	cmnd->orig_start_pg_offset = cmnd->read_pg_offset = read_pg_offset;
}

static void
iscsi_send_ccb(void *ccb_void)
{
	struct qsio_scsiio *ctio = (struct qsio_scsiio *)ccb_void;
	struct iscsi_priv *priv = &ctio->ccb_h.priv.ipriv;
	struct iscsi_cmnd *cmnd = priv->cmnd;
	uint32_t ret_residual = 0;
	uint8_t ret_flags = 0;
	struct ccb_list ctio_list;

	STAILQ_INIT(&ctio_list);
	(*icbs.device_remove_ctio)(ctio, &ctio_list);

	if ((ctio->ccb_h.flags & QSIO_CTIO_ABORTED) && (ctio->ccb_h.flags & QSIO_SEND_ABORT_STATUS)) {
		(*icbs.ctio_free_data)(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASC, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASCQ);
	}
	else if (cmnd_tmfabort(cmnd) && !ctio->queued && cmnd_sendabort(cmnd)) {
		DEBUG_BUG_ON(ctio->queued);
		(*icbs.ctio_free_data)(ctio);
		ctio_construct_sense(ctio, SSD_CURRENT_ERROR, SSD_KEY_ABORTED_COMMAND, 0, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASC, COMMANDS_CLEARED_BY_ANOTHER_INITIATOR_ASCQ);
	}

	if (ctio->scsi_status == SCSI_STATUS_CHECK_COND) {
		if (ctio->pglist_cnt && ctio->dxfer_len)
		{
			create_scsi_data_rsp2(cmnd, ctio, 0, &ret_residual, &ret_flags);
		}
		iscsi_create_sense_rsp(cmnd, ctio, ret_residual, ret_flags);
		goto send;
	}

	if (!ctio->dxfer_len)
		create_scsi_rsp(cmnd, ctio);
	else if (!ctio->pglist_cnt)
		create_scsi_data_rsp(cmnd, ctio);
	else
		create_scsi_data_rsp2(cmnd, ctio, 1, &ret_residual, &ret_flags);
send:
	(*icbs.device_queue_ctio_list)(&ctio_list);
}

uint64_t t_prt;

struct qsio_scsiio *
iscsi_construct_ctio(struct iscsi_conn *conn, struct iscsi_cmnd *cmnd)
{
	struct qsio_scsiio *ctio;
	struct iscsi_priv *priv;
	struct iscsi_scsi_cmd_hdr *hdr = cmnd_hdr(cmnd);
	struct iscsi_target *target =  conn->session->target;
	uint8_t task_attr;

	if (!target->tdevice)
		return NULL;

	ctio = (*icbs.ctio_new)(Q_NOWAIT);
	if (!ctio)
	{
		DEBUG_WARN("iscsi_construct_ctio: unable to allocate a new ctio object\n");
		return NULL;
	}

	if (unlikely(!t_prt))
		t_prt = (*icbs.get_tprt)();

	ctio->i_prt[0] = conn->session->sid;
	ctio->t_prt[0] = t_prt;
	ctio->r_prt = ISCSI_RPORT_START;
	ctio->init_int = TARGET_INT_ISCSI;
	memcpy(ctio->cdb, hdr->scb, 16);
	ctio->ccb_h.flags = QSIO_DIR_OUT;
	ctio->ccb_h.flags |= QSIO_TYPE_CTIO;
	ctio->ccb_h.queue_fn = iscsi_send_ccb;
	ctio->ccb_h.target_lun = translate_lun(hdr->lun);
	ctio->ccb_h.tdevice = target->tdevice;

	switch ((cmnd->pdu.bhs.flags & ISCSI_CMD_ATTR_MASK)) {
		case ISCSI_CMD_UNTAGGED:
		case ISCSI_CMD_SIMPLE:
			task_attr = MSG_SIMPLE_TASK;
			break;
		case ISCSI_CMD_ORDERED:
			task_attr = MSG_ORDERED_TASK;
			break;
		case ISCSI_CMD_HEAD_OF_QUEUE:
			task_attr = MSG_HEAD_OF_QUEUE_TASK;
			break;
		default:
			task_attr = MSG_SIMPLE_TASK;
			break;
	}

	ctio->task_attr = task_attr;
	ctio->task_tag = cmnd_itt(cmnd);

	priv = &ctio->ccb_h.priv.ipriv;
	priv->cmnd = cmnd;
	priv->init_name = cmnd->conn->session->initiator;

	return ctio;
}

int
iscsi_target_new_device_cb (struct tdevice *newdevice)
{
	struct target_info tinfo;
	int retval;
	struct iscsi_target *target;

	memset(&tinfo, 0, sizeof(struct target_info));
	tinfo.tid = (*icbs.device_tid)(newdevice);
	sprintf(tinfo.name, "%u", tinfo.tid);
	retval = target_add(&tinfo);
	if (retval != 0)
	{
		return -1;
	}
	target = target_lookup_by_id(tinfo.tid);
	if (!target)
	{
		return -1;
	}
	target->tdevice = newdevice;
	(*icbs.device_set_vhba_id)(newdevice, tinfo.tid, TARGET_INT_ISCSI);
	return 0;
}

void
iscsi_target_disable_device_cb(struct tdevice *device, int tid, void *hpriv)
{
	struct iscsi_target *target;

	target = target_lookup_by_id(tid);
	if (!target)
		return;
	atomic_set(&target->disabled, 1);
}

int
iscsi_target_remove_device_cb(struct tdevice *device, int tid, void *hpriv)
{
	struct iscsi_target *target;

	target = target_lookup_by_id(tid);
	if (!target)
		return -1;
	return target_del(tid);
}

void iscsi_cmnd_send_pdu(struct iscsi_conn *conn, struct iscsi_cmnd *cmnd)
{
	conn->ctio = cmnd->ctio;
	conn->write_offset = 0;
	conn->write_size += (cmnd->pdu.datasize + 3) & -4;
}

static int 
read_ctio_skip_data(struct iscsi_conn *conn, int *ret_done)
{
	__u8 *buffer;
	struct msghdr msg;
	struct iovec iov[1];
#ifdef LINUX
	mm_segment_t oldfs;
#else
	struct uio uio;
	int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
#endif
	int res;

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;


	while (conn->read_size)
	{
		int min;

		min = conn->read_size;
		if (min > PAGE_SIZE)
		{
			min = PAGE_SIZE;
		}
		buffer = malloc(min, M_IET, M_WAITOK);

		iov[0].iov_base = buffer;
		iov[0].iov_len  = min;

#ifdef LINUX
		oldfs = get_fs();
		set_fs(get_ds());
		res = sock_recvmsg(conn->sock, &msg, iov[0].iov_len, MSG_DONTWAIT | MSG_NOSIGNAL);
		set_fs(oldfs);
#else
		uio_fill(&uio, iov, 1, min, UIO_READ);
		res = soreceive(conn->sock, NULL, &uio, NULL, NULL, &flags);
		map_result(&res, &uio, min, 1);
#endif
		free(buffer, M_IET);
		if (res <= 0) {
			switch (res) {
			case -EAGAIN:
			case -ERESTARTSYS:
				return 0;
			default:
				break;
			}
			DEBUG_WARN_NEW("read %d failed asked %d\n", res, min);
			return -1;
		}
		conn->read_size -= res;
		conn->read_offset += res;
		*(ret_done) += res;
	}
	return 0;
}

static inline int
do_recv_pglist(struct iscsi_conn *conn)
{
	struct qsio_scsiio *ctio = conn->read_ctio; 
	struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);
	struct pgdata *pgtmp;
	int i, j;
	int pg_offset;
	int max_length;
	__u32 done = 0;
#ifdef LINUX
	struct msghdr msg;
	mm_segment_t oldfs;
#else
	struct uio uio;
	int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
#endif
	int res;

	ctio_idx_offset(conn->read_offset, &i, &pg_offset);
	for (j = 0; (i < ctio->pglist_cnt && j < ISCSI_CONN_IOV_MAX); i++)
	{

		pgtmp = pglist[i];
		max_length = pgtmp->pg_len - pg_offset;
		if (max_length > (conn->read_size - done))
			max_length = (conn->read_size - done);

		conn->read_iov[j].iov_base = (u8 *)pgdata_page_address(pgtmp) + pgtmp->pg_offset + pg_offset;
		conn->read_iov[j].iov_len = max_length;
		j++;
		done += max_length;
		pg_offset = 0;

		if (done == conn->read_size)
			break; 
	}

#ifdef LINUX
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = conn->read_iov;
	msg.msg_iovlen = j;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;

	oldfs = get_fs();
	set_fs(get_ds());
	res = sock_recvmsg(conn->sock, &msg, done, MSG_DONTWAIT | MSG_NOSIGNAL);
	set_fs(oldfs);
#else
	uio_fill(&uio, conn->read_iov, j, done, UIO_READ);
	res = soreceive(conn->sock, NULL, &uio, NULL, NULL, &flags);
	map_result(&res, &uio, done, 0);
#endif

	if (res <= 0)
	{
		switch (res) {
			case -EAGAIN:
			case -ERESTARTSYS:
				return res;
			default:
				break;
		}
		(*icbs.ctio_free_data)(conn->read_ctio);
		conn->read_ctio = NULL;
		DEBUG_WARN_NEW("Failed to receive ctio data\n");
		conn_close(conn);
		return res;
	}

	conn->read_size -= res;
	conn->read_offset += res;
	return res;
}

int do_recv_ctio(struct iscsi_conn *conn, int state)
{
	__u32 offset = conn->read_offset;
	int done = 0;
	struct iovec iov[1];
	struct qsio_scsiio *ctio = conn->read_ctio; 
	int iov_len;
#ifdef LINUX
	struct msghdr msg;
	mm_segment_t oldfs;
#else
	struct uio uio;
	int flags = MSG_DONTWAIT | MSG_NOSIGNAL;
#endif

	if (conn->read_offset >= ctio->dxfer_len && conn->read_size)
		goto skip_data;

	if (ctio->pglist_cnt)
	{
		done = do_recv_pglist(conn);
		if (done <= 0)
		{
			return done;
		}
	}
	else
	{
		if (ctio->dxfer_len > 0)
		{

			iov[0].iov_base = ctio->data_ptr+offset;
			iov[0].iov_len  = ctio->dxfer_len-offset;
			iov_len = iov[0].iov_len;
#ifdef LINUX
			msg.msg_name = NULL;
			msg.msg_namelen = 0;
			msg.msg_iov = iov;
			msg.msg_iovlen = 1;
			msg.msg_control = NULL;
			msg.msg_controllen = 0;
			msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;

			oldfs = get_fs();
			set_fs(get_ds());
			done = sock_recvmsg(conn->sock, &msg, iov[0].iov_len, MSG_DONTWAIT | MSG_NOSIGNAL);
			set_fs(oldfs);
#else
			uio_fill(&uio, iov, 1, iov_len, UIO_READ);
			done = soreceive(conn->sock, NULL, &uio, NULL, NULL, &flags);
			map_result(&done, &uio, iov_len, 0);
#endif
			if (done <= 0) {
				switch (done) {
					case -EAGAIN:
					case -ERESTARTSYS:
						break;
					default:
						DEBUG_WARN("do_recv_ctio: sock_recvmsg failed with done %d for len %d\n", (int)done, (int)iov_len);
						(*icbs.ctio_free_data)(conn->read_ctio);
						conn->read_ctio = NULL;
						conn_close(conn);
						break;
				}
				return done;
			}
			conn->read_size -= done;
			conn->read_offset += done;
		}
	}

skip_data:
	if (conn->read_offset >= ctio->dxfer_len && conn->read_size)
	{
		int retval;
		int skip_done = 0;

		retval = read_ctio_skip_data(conn, &skip_done);
		if (retval != 0) {
			DEBUG_WARN("do_recv_ctio: reading trailing bytes failed\n");
			(*icbs.ctio_free_data)(conn->read_ctio);
			conn->read_ctio = NULL;
			conn_close(conn);
			return retval; 
		}
		done += skip_done;
	}


	if (!conn->read_size)
		conn->read_state = state;

	return done;
}

int do_send_ctio(struct iscsi_conn *conn, int state)
{
#ifdef LINUX
	struct msghdr msg;
	mm_segment_t oldfs;
	struct file *file;
#else
	struct uio uio;
	int flags;
#endif
	struct iovec iov[1];
	struct qsio_scsiio *ctio = conn->ctio; 
	struct iovec *iop;
	int saved_size, size;
	int res = 0;
	
	struct iscsi_data_in_hdr *rsp_hdr = (struct iscsi_data_in_hdr *)&conn->write_cmnd->pdu.bhs;

	if (!test_bit(CONN_ACTIVE, &conn->state)) {
		return -(EIO);
	}

#ifdef LINUX
	file = conn->file;
#endif
	saved_size = size = conn->write_size;
	iop = conn->write_iop;
	
	if (iop) while (1) {
#ifdef LINUX
		loff_t off = 0;
		int rest;
#endif
		unsigned long count;
		struct iovec *vec;
		int len = 0;

		vec = iop;
		for (count = 0; vec->iov_len; count++, vec++) {
			len += vec->iov_len;
		}
#ifdef LINUX
		oldfs = get_fs();
		set_fs(KERNEL_DS);
		res = vfs_writev(file, (struct iovec __user *) iop, count, &off);
		set_fs(oldfs);
#else
		uio_fill(&uio, iop, count, len, UIO_WRITE);
		flags = MSG_DONTWAIT | MSG_NOSIGNAL;
		res = sosend(conn->sock, NULL, &uio, NULL, NULL, flags, curthread);
		map_result(&res, &uio, len, 0);
#endif
		dprintk(D_DATA, "%#Lx:%u: %d(%ld)\n",
			(unsigned long long) conn->session->sid, conn->cid,
			res, (long) iop->iov_len);
		if (unlikely(res <= 0)) {
			if (res == -EAGAIN || res == -EINTR) {
				conn->write_iop = iop;
				goto out_iov;
			}
			goto err;
		}
		size -= res;
		if (res == len) {
			conn->write_iop = NULL;
			if (size)
				break;
			goto out_iov;
		}

#ifdef LINUX
		rest = res;
		while (iop->iov_len && iop->iov_len <= rest && rest) {
			rest -= iop->iov_len;
			iop++;
		}

		iop->iov_base = (u8 *)iop->iov_base + rest;
		iop->iov_len -= rest;
#else
		iop = uio.uio_iov;
#endif

		if (!iop->iov_len) {
			conn->write_iop = NULL;
			if (size)
				break;
			goto out_iov;
		}
	}

#ifdef LINUX
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = iov;
	msg.msg_iovlen = 1;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = MSG_DONTWAIT | MSG_NOSIGNAL;
#endif

	if (rsp_hdr->cmd_status == SAM_STAT_CHECK_CONDITION)
	{
		int min_len = ((ctio->sense_len + 2 + 3) & -4) - conn->write_offset;
		iov[0].iov_base = ctio->sense_data+conn->write_offset;
		iov[0].iov_len  = min_len;
#ifdef LINUX
		oldfs = get_fs();
		set_fs(get_ds());
		res = sock_sendmsg(conn->sock, &msg, min_len);
		set_fs(oldfs);
#else
		uio_fill(&uio, iov, 1, min_len, UIO_WRITE);
		flags = MSG_DONTWAIT | MSG_NOSIGNAL;
		res = sosend(conn->sock, NULL, &uio, NULL, NULL, flags, curthread);
		map_result(&res, &uio, min_len, 0);
#endif

		if (unlikely(res <= 0)) {
			switch (res)
			{
				case -EAGAIN:
				case -EINTR:
					break;
				default:
					goto err;
			}
			goto out_iov;
		}
		size -= res;
		conn->write_offset += res;
	}
	else if (ctio->pglist_cnt > 0)
	{
		struct pgdata **pglist = (struct pgdata **)(ctio->data_ptr);
		struct iscsi_cmnd *write_cmnd = conn->write_cmnd;
#ifdef LINUX
		struct socket *sock = conn->sock;

		ssize_t (*sendpage)(struct socket *, pagestruct_t *, int, size_t, int);
#endif
		int i;
		int min;
		int pg_offset;
		__u32 flags;

#ifdef LINUX
		sendpage = sock->ops->sendpage ? sock->ops->sendpage : sock_no_sendpage;
#endif
		for (i = write_cmnd->start_pg_idx; i <= write_cmnd->end_pg_idx; i++)
		{
			struct pgdata *pgtmp = pglist[i];

again:
			pg_offset = write_cmnd->start_pg_offset;
			if (i == write_cmnd->end_pg_idx)
				min = write_cmnd->end_pg_offset - pg_offset;
			else
				min = pgtmp->pg_len - pg_offset;

#ifdef LINUX
			flags = MSG_DONTWAIT | MSG_NOSIGNAL;
			if (i != write_cmnd->end_pg_idx)
			{
				flags |= MSG_MORE;	
			}

			res = sendpage(sock, pgtmp->page, pg_offset + pgtmp->pg_offset, min, flags); 
#else
			iov[0].iov_base = (u8 *)pgdata_page_address(pgtmp) + pgtmp->pg_offset + pg_offset;
			iov[0].iov_len  = min;
			uio_fill(&uio, iov, 1, min, UIO_WRITE);
			flags = MSG_DONTWAIT | MSG_NOSIGNAL;
			res = sosend(conn->sock, NULL, &uio, NULL, NULL, flags, curthread);
			map_result(&res, &uio, min, 0);
#endif
			if (unlikely(res <= 0)) {
				switch (res)
				{
					case -EAGAIN:
					case -EINTR:
						break;
					default:
						goto err;
				}
				goto out_iov;
			}

			size -= res;
			write_cmnd->start_pg_offset += res;

			if (i == write_cmnd->end_pg_idx && write_cmnd->end_pg_offset == write_cmnd->start_pg_offset)
			{
				goto out_iov;
			}

			if (write_cmnd->start_pg_offset < pgtmp->pg_len)
			{
				goto again; 
			}

			write_cmnd->start_pg_offset = 0;
			write_cmnd->start_pg_idx++;
		}
	}
	else if (ctio->dxfer_len > 0)
	{
		struct iscsi_cmnd *write_cmnd = conn->write_cmnd;
		int iov_len;

		iov[0].iov_base = ctio->data_ptr+write_cmnd->write_iov_offset+conn->write_offset;
		iov[0].iov_len = iov_len = write_cmnd->write_iov_len - conn->write_offset;
#ifdef LINUX
		oldfs = get_fs();
		set_fs(get_ds());
		res = sock_sendmsg(conn->sock, &msg, iov[0].iov_len);
		set_fs(oldfs);
#else
		uio_fill(&uio, iov, 1, iov_len, UIO_WRITE);
		flags = MSG_DONTWAIT | MSG_NOSIGNAL;
		res = sosend(conn->sock, NULL, &uio, NULL, NULL, flags, curthread);
		map_result(&res, &uio, iov_len, 0);
#endif
		if (unlikely(res <= 0)) {
			switch (res)
			{
				case -EAGAIN:
				case -EINTR:
					break;
				default:
					goto err;
			}
			goto out_iov;
		}
		size -= res;
		conn->write_offset += res;
	}

out_iov:
	conn->write_size = size;

	if (res == -EAGAIN) {
		set_conn_wspace_wait(conn);
	}

	if ((saved_size == size) && res < 0)
	{
		return res;
	}

	return saved_size - size;
err:
	conn->write_size = 0;
	eprintk("error %d at %#Lx:%u\n", res,
		(unsigned long long) conn->session->sid, conn->cid);
	exit_tx(conn, res);
	return res;
}

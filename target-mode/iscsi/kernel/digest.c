/*
 * iSCSI digest handling.
 * (C) 2004 - 2006 Xiranet Communications GmbH <arne.redlich@xiranet.com>
 * This code is licensed under the GPL.
 */

#include "iscsi.h"
#include "digest.h"
#include "iscsi_dbg.h"
#include "scdefs.h"

void digest_alg_available(unsigned int *val)
{
#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	if (*val & DIGEST_CRC32C && !crypto_alg_available("crc32c", 0)) {
		printk("CRC32C digest algorithm not available in kernel\n");
		*val |= ~DIGEST_CRC32C;
	}
#else
	if (*val & DIGEST_CRC32C && !crypto_has_alg("crc32c", 0, CRYPTO_ALG_ASYNC)) {
		printk("CRC32C digest algorithm not available in kernel\n");
		*val |= ~DIGEST_CRC32C;
	}
#endif
#endif
}

/**
 * initialize support for digest calculation.
 *
 * digest_init -
 * @conn: ptr to connection to make use of digests
 *
 * @return: 0 on success, < 0 on error
 */
#ifdef LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
int digest_init(struct iscsi_conn *conn)
{
	int err = 0;

	if (!(conn->hdigest_type & DIGEST_ALL))
		conn->hdigest_type = DIGEST_NONE;

	if (!(conn->ddigest_type & DIGEST_ALL))
		conn->ddigest_type = DIGEST_NONE;

	if (conn->hdigest_type & DIGEST_CRC32C ||
	    conn->ddigest_type & DIGEST_CRC32C) {
		conn->rx_hash.tfm = crypto_alloc_hash("crc32c", 0,
						      CRYPTO_ALG_ASYNC);
		conn->rx_hash.flags = 0;
		if (IS_ERR(conn->rx_hash.tfm)) {
			conn->rx_hash.tfm = NULL;
			err = -ENOMEM;
			goto out;
		}

		conn->tx_hash.tfm = crypto_alloc_hash("crc32c", 0,
						      CRYPTO_ALG_ASYNC);
		conn->tx_hash.flags = 0;
		if (IS_ERR(conn->tx_hash.tfm)) {
			conn->tx_hash.tfm = NULL;
			err = -ENOMEM;
			goto out;
		}
	}

out:
	if (err)
		digest_cleanup(conn);

	return err;
}
#else
int digest_init(struct iscsi_conn *conn)
{
	int err = 0;

	if (!(conn->hdigest_type & DIGEST_ALL))
		conn->hdigest_type = DIGEST_NONE;

	if (!(conn->ddigest_type & DIGEST_ALL))
		conn->ddigest_type = DIGEST_NONE;

	if (conn->hdigest_type & DIGEST_CRC32C || conn->ddigest_type & DIGEST_CRC32C) {
		conn->rx_digest_tfm = crypto_alloc_tfm("crc32c", 0);
		if (!conn->rx_digest_tfm) {
			err = -ENOMEM;
			goto out;
		}

		conn->tx_digest_tfm = crypto_alloc_tfm("crc32c", 0);
		if (!conn->tx_digest_tfm) {
			err = -ENOMEM;
			goto out;
		}
	}

out:
	if (err)
		digest_cleanup(conn);

	return err;
}
#endif
#else
int digest_init(struct iscsi_conn *conn)
{
	if (!(conn->hdigest_type & DIGEST_ALL))
		conn->hdigest_type = DIGEST_NONE;

	if (!(conn->ddigest_type & DIGEST_ALL))
		conn->ddigest_type = DIGEST_NONE;

	return 0;
}
#endif

/**
 * free resources used for digest calculation.
 *
 * digest_cleanup -
 * @conn: ptr to connection that made use of digests
 */
#ifdef LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
void digest_cleanup(struct iscsi_conn *conn)
{
	if (conn->tx_hash.tfm)
		crypto_free_hash(conn->tx_hash.tfm);
	if (conn->rx_hash.tfm)
		crypto_free_hash(conn->rx_hash.tfm);
}
#else
void digest_cleanup(struct iscsi_conn *conn)
{
	if (conn->tx_digest_tfm)
		crypto_free_tfm(conn->tx_digest_tfm);
	if (conn->rx_digest_tfm)
		crypto_free_tfm(conn->rx_digest_tfm);
}
#endif
#else
void digest_cleanup(struct iscsi_conn *conn) {}
#endif

/**
 * debug handling of header digest errors:
 * simulates a digest error after n PDUs / every n-th PDU of type
 * HDIGEST_ERR_CORRUPT_PDU_TYPE.
 */
static inline void __dbg_simulate_header_digest_error(struct iscsi_cmnd *cmnd)
{
#define HDIGEST_ERR_AFTER_N_CMNDS 1000
#define HDIGEST_ERR_ONLY_ONCE     1
#define HDIGEST_ERR_CORRUPT_PDU_TYPE ISCSI_OP_SCSI_CMD
#define HDIGEST_ERR_CORRUPT_PDU_WITH_DATA_ONLY 0

	static int num_cmnds = 0;
	static int num_errs = 0;

	if (cmnd_opcode(cmnd) == HDIGEST_ERR_CORRUPT_PDU_TYPE) {
		if (HDIGEST_ERR_CORRUPT_PDU_WITH_DATA_ONLY) {
			if (cmnd->pdu.datasize)
				num_cmnds++;
		} else
			num_cmnds++;
	}

	if ((num_cmnds == HDIGEST_ERR_AFTER_N_CMNDS)
	    && (!(HDIGEST_ERR_ONLY_ONCE && num_errs))) {
		printk("*** Faking header digest error ***\n");
		printk("\tcmnd: 0x%x, itt 0x%x, sn 0x%x\n",
		       cmnd_opcode(cmnd),
		       be32_to_cpu(cmnd->pdu.bhs.itt),
		       be32_to_cpu(cmnd->pdu.bhs.sn));
		cmnd->hdigest = ~cmnd->hdigest;
		/* make things even worse by manipulating header fields */
		cmnd->pdu.datasize += 8;
		num_errs++;
		num_cmnds = 0;
	}
	return;
}

/**
 * debug handling of data digest errors:
 * simulates a digest error after n PDUs / every n-th PDU of type
 * DDIGEST_ERR_CORRUPT_PDU_TYPE.
 */
static inline void __dbg_simulate_data_digest_error(struct iscsi_cmnd *cmnd)
{
#define DDIGEST_ERR_AFTER_N_CMNDS 50
#define DDIGEST_ERR_ONLY_ONCE     1
#define DDIGEST_ERR_CORRUPT_PDU_TYPE   ISCSI_OP_SCSI_DATA_OUT
#define DDIGEST_ERR_CORRUPT_UNSOL_DATA_ONLY 0

	static int num_cmnds = 0;
	static int num_errs = 0;

	if ((cmnd->pdu.datasize)
	    && (cmnd_opcode(cmnd) == DDIGEST_ERR_CORRUPT_PDU_TYPE)) {
		switch (cmnd_opcode(cmnd)) {
		case ISCSI_OP_SCSI_DATA_OUT:
			if ((DDIGEST_ERR_CORRUPT_UNSOL_DATA_ONLY)
			    && (cmnd->pdu.bhs.ttt != ISCSI_RESERVED_TAG))
				break;
		default:
			num_cmnds++;
		}
	}

	if ((num_cmnds == DDIGEST_ERR_AFTER_N_CMNDS)
	    && (!(DDIGEST_ERR_ONLY_ONCE && num_errs))
	    && (cmnd->pdu.datasize)
	    && (!cmnd->conn->read_overflow)) {
		printk("*** Faking data digest error: ***");
		printk("\tcmnd 0x%x, itt 0x%x, sn 0x%x\n",
		       cmnd_opcode(cmnd),
		       be32_to_cpu(cmnd->pdu.bhs.itt),
		       be32_to_cpu(cmnd->pdu.bhs.sn));
		cmnd->ddigest = ~cmnd->ddigest;
		num_errs++;
		num_cmnds = 0;
	}
}

/* Copied from linux-iscsi initiator and slightly adjusted */

#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
#define SETSG(sg, p, l) do {					\
	(sg).page = virt_to_page((p));				\
	(sg).offset = ((unsigned long)(p) & ~PAGE_CACHE_MASK);	\
	(sg).length = (l);					\
} while (0)
#else
#define SETSG(sg, p, l) sg_set_page(&sg, virt_to_page((p)), ((unsigned long)(p) & ~PAGE_CACHE_MASK), l) 
#endif
#endif

#ifdef LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
static void digest_header(struct hash_desc *hash, struct iscsi_pdu *pdu,
			  u8 *crc)
{
	struct scatterlist sg[2];
	unsigned int nbytes = sizeof(struct iscsi_hdr);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	sg_init_table(sg, pdu->ahssize ? 2 : 1);
#else
	memset(sg, 0, (sizeof(struct scatterlist) * 2));
#endif

	sg_set_buf(&sg[0], &pdu->bhs, nbytes);
	if (pdu->ahssize) {
		sg_set_buf(&sg[1], pdu->ahs, pdu->ahssize);
		nbytes += pdu->ahssize;
	}

	crypto_hash_init(hash);
	crypto_hash_update(hash, sg, nbytes);
	crypto_hash_final(hash, crc);
}
#else
static void digest_header(struct crypto_tfm *tfm, struct iscsi_pdu *pdu, u8 *crc)
{
	struct scatterlist sg[2];
	int i = 0;

	SETSG(sg[i], &pdu->bhs, sizeof(struct iscsi_hdr));
	i++;
	if (pdu->ahssize) {
		SETSG(sg[i], pdu->ahs, pdu->ahssize);
		i++;
	}

	crypto_digest_init(tfm);
	crypto_digest_update(tfm, sg, i);
	crypto_digest_final(tfm, crc);
}
#endif
#else
static void digest_header(struct chksum_ctx *mctx, struct iscsi_pdu *pdu, u8 *crc)
{
	chksum_init(mctx);
	chksum_update(mctx, (u8 *)(&pdu->bhs), sizeof(struct iscsi_hdr));
	if (pdu->ahssize) {
		chksum_update(mctx, (u8 *)(pdu->ahs), pdu->ahssize);
	}

	chksum_final(mctx, crc); 
}
#endif

int digest_rx_header(struct iscsi_cmnd *cmnd)
{
	u32 crc;

#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	digest_header(cmnd->conn->rx_digest_tfm, &cmnd->pdu, (u8 *) &crc);
#else
	digest_header(&cmnd->conn->rx_hash, &cmnd->pdu, (u8 *) &crc);
#endif
#else
	digest_header(&cmnd->conn->rx_ctx, &cmnd->pdu, (u8 *) &crc);
#endif
	if (crc != cmnd->hdigest)
		return -EIO;

	return 0;
}

void digest_tx_header(struct iscsi_cmnd *cmnd)
{
#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
	digest_header(cmnd->conn->tx_digest_tfm, &cmnd->pdu, (u8 *) &cmnd->hdigest);
#else
	digest_header(&cmnd->conn->tx_hash, &cmnd->pdu, (u8 *) &cmnd->hdigest);
#endif
#else
	digest_header(&cmnd->conn->tx_ctx, &cmnd->pdu, (u8 *) &cmnd->hdigest);
#endif
}

#ifdef LINUX
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23))
static void digest_data(struct hash_desc *hash, struct iscsi_cmnd *cmnd,
			struct tio *tio, u32 offset, u8 *crc)
{
	struct scatterlist *sg = cmnd->conn->hash_sg;
	u32 size, length;
	int i, idx, count;
	unsigned int nbytes;

	size = cmnd->pdu.datasize;
	nbytes = size = (size + 3) & ~3;

	offset += tio->offset;
	idx = offset >> PAGE_CACHE_SHIFT;
	offset &= ~PAGE_CACHE_MASK;
	count = get_pgcnt(size, offset);
	assert(idx + count <= tio->pg_cnt);

	assert(count <= ISCSI_CONN_IOV_MAX);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	sg_init_table(sg, ARRAY_SIZE(cmnd->conn->hash_sg));
#else
	memset(sg, 0, (sizeof(struct scatterlist) * ARRAY_SIZE(cmnd->conn->hash_sg)));
#endif
	crypto_hash_init(hash);

	for (i = 0; size; i++) {
		if (offset + size > PAGE_CACHE_SIZE)
			length = PAGE_CACHE_SIZE - offset;
		else
			length = size;

		sg_set_page(&sg[i], tio->pvec[idx + i], length, offset);
		size -= length;
		offset = 0;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24))
	sg_mark_end(&sg[i - 1]);
#endif

	crypto_hash_update(hash, sg, nbytes);
	crypto_hash_final(hash, crc);
}
#else
static void digest_data(struct crypto_tfm *tfm, struct iscsi_cmnd *cmnd,
			struct tio *tio, u32 offset, u8 *crc)
{
	struct scatterlist *sg = cmnd->conn->hash_sg;
	u32 size, length;
	int i, idx, count;

	size = cmnd->pdu.datasize;
	size = (size + 3) & ~3;

	offset += tio->offset;
	idx = offset >> PAGE_CACHE_SHIFT;
	offset &= ~PAGE_CACHE_MASK;
	count = get_pgcnt(size, offset);
	assert(idx + count <= tio->pg_cnt);

	assert(count <= ISCSI_CONN_IOV_MAX);

	crypto_digest_init(tfm);

	for (i = 0; size; i++) {
		if (offset + size > PAGE_CACHE_SIZE)
			length = PAGE_CACHE_SIZE - offset;
		else
			length = size;

		sg[i].page = tio->pvec[idx + i];
		sg[i].offset = offset;
		sg[i].length = length;
		size -= length;
		offset = 0;
	}

	crypto_digest_update(tfm, sg, count);
	crypto_digest_final(tfm, crc);
}
#endif
#else
static void digest_data(struct chksum_ctx *mctx, struct iscsi_cmnd *cmnd,
			struct tio *tio, u32 offset, u8 *crc)
{
	u32 size, length;
	int i, idx, count;

	size = cmnd->pdu.datasize;
	size = (size + 3) & ~3;

	offset += tio->offset;
	idx = offset >> PAGE_CACHE_SHIFT;
	offset &= ~PAGE_CACHE_MASK;
	count = get_pgcnt(size, offset);
	assert(idx + count <= tio->pg_cnt);

	assert(count <= ISCSI_CONN_IOV_MAX);

	chksum_init(mctx);

	for (i = 0; size; i++) {
		if (offset + size > PAGE_CACHE_SIZE)
			length = PAGE_CACHE_SIZE - offset;
		else
			length = size;

		chksum_update(mctx, (u8*)(page_address(tio->pvec[idx + i])) + offset, length);
		size -= length;
		offset = 0;
	}

	chksum_final(mctx, crc);
}
#endif

int digest_rx_data(struct iscsi_cmnd *cmnd)
{
	struct tio *tio;
	struct iscsi_cmnd *scsi_cmnd;
	struct iscsi_data_out_hdr *req;
	u32 offset, crc;

	switch (cmnd_opcode(cmnd)) {
	case ISCSI_OP_SCSI_REJECT:
	case ISCSI_OP_PDU_REJECT:
	case ISCSI_OP_DATA_REJECT:
		return 0;
	case ISCSI_OP_SCSI_DATA_OUT:
		scsi_cmnd = cmnd->req;
		req = (struct iscsi_data_out_hdr *) &cmnd->pdu.bhs;
		tio = scsi_cmnd->tio;
		offset = be32_to_cpu(req->buffer_offset);
		break;
	default:
		tio = cmnd->tio;
		offset = 0;
	}

	if (cmnd->conn->read_ctio)
	{
#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
		digest_read_ctio(cmnd->conn->rx_digest_tfm, cmnd, cmnd->conn->read_ctio, (u8 *)&crc);
#else
		digest_read_ctio(&cmnd->conn->rx_hash, cmnd, cmnd->conn->read_ctio, (u8 *)&crc);
#endif
#else
		digest_read_ctio(&cmnd->conn->rx_ctx, cmnd, cmnd->conn->read_ctio, (u8 *)&crc);
#endif
	}
	else
	{
#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
		digest_data(cmnd->conn->rx_digest_tfm, cmnd, tio, offset, (u8 *) &crc);
#else
		digest_data(&cmnd->conn->rx_hash, cmnd, tio, offset, (u8 *) &crc);
#endif
#else
		digest_data(&cmnd->conn->rx_ctx, cmnd, tio, offset, (u8 *) &crc);
#endif
	}


	if (!cmnd->conn->read_overflow &&
	    (cmnd_opcode(cmnd) != ISCSI_OP_PDU_REJECT)) {
		if (crc != cmnd->ddigest)
			return -EIO;
	}

	return 0;
}

void digest_tx_data(struct iscsi_cmnd *cmnd)
{
	struct tio *tio = cmnd->tio;
	struct iscsi_data_out_hdr *req = (struct iscsi_data_out_hdr *)&cmnd->pdu.bhs;

	if (cmnd->ctio)
	{
#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
		digest_write_ctio(cmnd->conn->tx_digest_tfm, cmnd, cmnd->ctio, (u8 *)&cmnd->ddigest);
#else
		digest_write_ctio(&cmnd->conn->tx_hash, cmnd, cmnd->ctio, (u8 *)&cmnd->ddigest);
#endif
#else
		digest_write_ctio(&cmnd->conn->tx_ctx, cmnd, cmnd->ctio, (u8 *)&cmnd->ddigest);
#endif
	}
	else
	{
		assert(tio);
#ifdef LINUX
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,23))
		digest_data(cmnd->conn->tx_digest_tfm, cmnd, tio,
			    be32_to_cpu(req->buffer_offset), (u8 *) &cmnd->ddigest);
#else
		digest_data(&cmnd->conn->tx_hash, cmnd, tio,
			    be32_to_cpu(req->buffer_offset), (u8 *) &cmnd->ddigest);
#endif
#else
		digest_data(&cmnd->conn->tx_ctx, cmnd, tio,
			    be32_to_cpu(req->buffer_offset), (u8 *) &cmnd->ddigest);
#endif
	}
}

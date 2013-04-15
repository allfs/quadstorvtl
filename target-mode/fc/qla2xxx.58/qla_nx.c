/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2011 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#define MASK(n)			((1ULL<<(n))-1)
#define MN_WIN(addr) (((addr & 0x1fc0000) >> 1) | ((addr >> 25) & 0x3ff))
#define OCM_WIN(addr) (((addr & 0x1ff0000) >> 1) | ((addr >> 25) & 0x3ff))
#define MS_WIN(addr) (addr & 0x0ffc0000)
#define QLA82XX_PCI_MN_2M   (0)
#define QLA82XX_PCI_MS_2M   (0x80000)
#define QLA82XX_PCI_OCM0_2M (0xc0000)
#define VALID_OCM_ADDR(addr) (((addr) & 0x3f800) != 0x3f800)
#define GET_MEM_OFFS_2M(addr) (addr & MASK(18))

/* CRB window related */
#define CRB_BLK(off)	((off >> 20) & 0x3f)
#define CRB_SUBBLK(off)	((off >> 16) & 0xf)
#define CRB_WINDOW_2M	(0x130060)
#define QLA82XX_PCI_CAMQM_2M_END	(0x04800800UL)
#define CRB_HI(off)	((qla82xx_crb_hub_agt[CRB_BLK(off)] << 20) | \
			((off) & 0xf0000))
#define QLA82XX_PCI_CAMQM_2M_BASE	(0x000ff800UL)
#define CRB_INDIRECT_2M	(0x1e0000UL)

#define MAX_CRB_XFORM 60
static unsigned long crb_addr_xform[MAX_CRB_XFORM];
int qla82xx_crb_table_initialized;

#define qla82xx_crb_addr_transform(name) \
		(crb_addr_xform[QLA82XX_HW_PX_MAP_CRB_##name] = \
		QLA82XX_HW_CRB_HUB_AGT_ADR_##name << 20)

static void qla82xx_crb_addr_transform_setup(void)
{
	qla82xx_crb_addr_transform(XDMA);
	qla82xx_crb_addr_transform(TIMR);
	qla82xx_crb_addr_transform(SRE);
	qla82xx_crb_addr_transform(SQN3);
	qla82xx_crb_addr_transform(SQN2);
	qla82xx_crb_addr_transform(SQN1);
	qla82xx_crb_addr_transform(SQN0);
	qla82xx_crb_addr_transform(SQS3);
	qla82xx_crb_addr_transform(SQS2);
	qla82xx_crb_addr_transform(SQS1);
	qla82xx_crb_addr_transform(SQS0);
	qla82xx_crb_addr_transform(RPMX7);
	qla82xx_crb_addr_transform(RPMX6);
	qla82xx_crb_addr_transform(RPMX5);
	qla82xx_crb_addr_transform(RPMX4);
	qla82xx_crb_addr_transform(RPMX3);
	qla82xx_crb_addr_transform(RPMX2);
	qla82xx_crb_addr_transform(RPMX1);
	qla82xx_crb_addr_transform(RPMX0);
	qla82xx_crb_addr_transform(ROMUSB);
	qla82xx_crb_addr_transform(SN);
	qla82xx_crb_addr_transform(QMN);
	qla82xx_crb_addr_transform(QMS);
	qla82xx_crb_addr_transform(PGNI);
	qla82xx_crb_addr_transform(PGND);
	qla82xx_crb_addr_transform(PGN3);
	qla82xx_crb_addr_transform(PGN2);
	qla82xx_crb_addr_transform(PGN1);
	qla82xx_crb_addr_transform(PGN0);
	qla82xx_crb_addr_transform(PGSI);
	qla82xx_crb_addr_transform(PGSD);
	qla82xx_crb_addr_transform(PGS3);
	qla82xx_crb_addr_transform(PGS2);
	qla82xx_crb_addr_transform(PGS1);
	qla82xx_crb_addr_transform(PGS0);
	qla82xx_crb_addr_transform(PS);
	qla82xx_crb_addr_transform(PH);
	qla82xx_crb_addr_transform(NIU);
	qla82xx_crb_addr_transform(I2Q);
	qla82xx_crb_addr_transform(EG);
	qla82xx_crb_addr_transform(MN);
	qla82xx_crb_addr_transform(MS);
	qla82xx_crb_addr_transform(CAS2);
	qla82xx_crb_addr_transform(CAS1);
	qla82xx_crb_addr_transform(CAS0);
	qla82xx_crb_addr_transform(CAM);
	qla82xx_crb_addr_transform(C2C1);
	qla82xx_crb_addr_transform(C2C0);
	qla82xx_crb_addr_transform(SMB);
	qla82xx_crb_addr_transform(OCM0);
	/*
	 * Used only in P3 just define it for P2 also.
	 */
	qla82xx_crb_addr_transform(I2C0);

	qla82xx_crb_table_initialized = 1;
}

struct crb_128M_2M_block_map crb_128M_2M_map[64] = {
    {{{0, 0,         0,         0} } },		/* 0: PCI */
    {{{1, 0x0100000, 0x0102000, 0x120000},	/* 1: PCIE */
	  {1, 0x0110000, 0x0120000, 0x130000},
	  {1, 0x0120000, 0x0122000, 0x124000},
	  {1, 0x0130000, 0x0132000, 0x126000},
	  {1, 0x0140000, 0x0142000, 0x128000},
	  {1, 0x0150000, 0x0152000, 0x12a000},
	  {1, 0x0160000, 0x0170000, 0x110000},
	  {1, 0x0170000, 0x0172000, 0x12e000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {1, 0x01e0000, 0x01e0800, 0x122000},
	  {0, 0x0000000, 0x0000000, 0x000000} } },
	{{{1, 0x0200000, 0x0210000, 0x180000} } },/* 2: MN */
    {{{0, 0,         0,         0} } },	    /* 3: */
    {{{1, 0x0400000, 0x0401000, 0x169000} } },/* 4: P2NR1 */
    {{{1, 0x0500000, 0x0510000, 0x140000} } },/* 5: SRE   */
    {{{1, 0x0600000, 0x0610000, 0x1c0000} } },/* 6: NIU   */
    {{{1, 0x0700000, 0x0704000, 0x1b8000} } },/* 7: QM    */
    {{{1, 0x0800000, 0x0802000, 0x170000},  /* 8: SQM0  */
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {1, 0x08f0000, 0x08f2000, 0x172000} } },
    {{{1, 0x0900000, 0x0902000, 0x174000},	/* 9: SQM1*/
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {1, 0x09f0000, 0x09f2000, 0x176000} } },
    {{{0, 0x0a00000, 0x0a02000, 0x178000},	/* 10: SQM2*/
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {1, 0x0af0000, 0x0af2000, 0x17a000} } },
    {{{0, 0x0b00000, 0x0b02000, 0x17c000},	/* 11: SQM3*/
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {0, 0x0000000, 0x0000000, 0x000000},
      {1, 0x0bf0000, 0x0bf2000, 0x17e000} } },
	{{{1, 0x0c00000, 0x0c04000, 0x1d4000} } },/* 12: I2Q */
	{{{1, 0x0d00000, 0x0d04000, 0x1a4000} } },/* 13: TMR */
	{{{1, 0x0e00000, 0x0e04000, 0x1a0000} } },/* 14: ROMUSB */
	{{{1, 0x0f00000, 0x0f01000, 0x164000} } },/* 15: PEG4 */
	{{{0, 0x1000000, 0x1004000, 0x1a8000} } },/* 16: XDMA */
	{{{1, 0x1100000, 0x1101000, 0x160000} } },/* 17: PEG0 */
	{{{1, 0x1200000, 0x1201000, 0x161000} } },/* 18: PEG1 */
	{{{1, 0x1300000, 0x1301000, 0x162000} } },/* 19: PEG2 */
	{{{1, 0x1400000, 0x1401000, 0x163000} } },/* 20: PEG3 */
	{{{1, 0x1500000, 0x1501000, 0x165000} } },/* 21: P2ND */
	{{{1, 0x1600000, 0x1601000, 0x166000} } },/* 22: P2NI */
	{{{0, 0,         0,         0} } },	/* 23: */
	{{{0, 0,         0,         0} } },	/* 24: */
	{{{0, 0,         0,         0} } },	/* 25: */
	{{{0, 0,         0,         0} } },	/* 26: */
	{{{0, 0,         0,         0} } },	/* 27: */
	{{{0, 0,         0,         0} } },	/* 28: */
	{{{1, 0x1d00000, 0x1d10000, 0x190000} } },/* 29: MS */
    {{{1, 0x1e00000, 0x1e01000, 0x16a000} } },/* 30: P2NR2 */
    {{{1, 0x1f00000, 0x1f10000, 0x150000} } },/* 31: EPG */
	{{{0} } },				/* 32: PCI */
	{{{1, 0x2100000, 0x2102000, 0x120000},	/* 33: PCIE */
	  {1, 0x2110000, 0x2120000, 0x130000},
	  {1, 0x2120000, 0x2122000, 0x124000},
	  {1, 0x2130000, 0x2132000, 0x126000},
	  {1, 0x2140000, 0x2142000, 0x128000},
	  {1, 0x2150000, 0x2152000, 0x12a000},
	  {1, 0x2160000, 0x2170000, 0x110000},
	  {1, 0x2170000, 0x2172000, 0x12e000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000},
	  {0, 0x0000000, 0x0000000, 0x000000} } },
	{{{1, 0x2200000, 0x2204000, 0x1b0000} } },/* 34: CAM */
	{{{0} } },				/* 35: */
	{{{0} } },				/* 36: */
	{{{0} } },				/* 37: */
	{{{0} } },				/* 38: */
	{{{0} } },				/* 39: */
	{{{1, 0x2800000, 0x2804000, 0x1a4000} } },/* 40: TMR */
	{{{1, 0x2900000, 0x2901000, 0x16b000} } },/* 41: P2NR3 */
	{{{1, 0x2a00000, 0x2a00400, 0x1ac400} } },/* 42: RPMX1 */
	{{{1, 0x2b00000, 0x2b00400, 0x1ac800} } },/* 43: RPMX2 */
	{{{1, 0x2c00000, 0x2c00400, 0x1acc00} } },/* 44: RPMX3 */
	{{{1, 0x2d00000, 0x2d00400, 0x1ad000} } },/* 45: RPMX4 */
	{{{1, 0x2e00000, 0x2e00400, 0x1ad400} } },/* 46: RPMX5 */
	{{{1, 0x2f00000, 0x2f00400, 0x1ad800} } },/* 47: RPMX6 */
	{{{1, 0x3000000, 0x3000400, 0x1adc00} } },/* 48: RPMX7 */
	{{{0, 0x3100000, 0x3104000, 0x1a8000} } },/* 49: XDMA */
	{{{1, 0x3200000, 0x3204000, 0x1d4000} } },/* 50: I2Q */
	{{{1, 0x3300000, 0x3304000, 0x1a0000} } },/* 51: ROMUSB */
	{{{0} } },				/* 52: */
	{{{1, 0x3500000, 0x3500400, 0x1ac000} } },/* 53: RPMX0 */
	{{{1, 0x3600000, 0x3600400, 0x1ae000} } },/* 54: RPMX8 */
	{{{1, 0x3700000, 0x3700400, 0x1ae400} } },/* 55: RPMX9 */
	{{{1, 0x3800000, 0x3804000, 0x1d0000} } },/* 56: OCM0 */
	{{{1, 0x3900000, 0x3904000, 0x1b4000} } },/* 57: CRYPTO */
	{{{1, 0x3a00000, 0x3a04000, 0x1d8000} } },/* 58: SMB */
	{{{0} } },				/* 59: I2C0 */
	{{{0} } },				/* 60: I2C1 */
	{{{1, 0x3d00000, 0x3d04000, 0x1dc000} } },/* 61: LPC */
	{{{1, 0x3e00000, 0x3e01000, 0x167000} } },/* 62: P2NC */
	{{{1, 0x3f00000, 0x3f01000, 0x168000} } }	/* 63: P2NR0 */
};

/*
 * top 12 bits of crb internal address (hub, agent)
 */
unsigned qla82xx_crb_hub_agt[64] =
{
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PS,
	QLA82XX_HW_CRB_HUB_AGT_ADR_MN,
	QLA82XX_HW_CRB_HUB_AGT_ADR_MS,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SRE,
	QLA82XX_HW_CRB_HUB_AGT_ADR_NIU,
	QLA82XX_HW_CRB_HUB_AGT_ADR_QMN,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SQN0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SQN1,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SQN2,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SQN3,
	QLA82XX_HW_CRB_HUB_AGT_ADR_I2Q,
	QLA82XX_HW_CRB_HUB_AGT_ADR_TIMR,
	QLA82XX_HW_CRB_HUB_AGT_ADR_ROMUSB,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN4,
	QLA82XX_HW_CRB_HUB_AGT_ADR_XDMA,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN1,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN2,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGN3,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGND,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGNI,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGS0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGS1,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGS2,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGS3,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGSI,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SN,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_EG,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PS,
	QLA82XX_HW_CRB_HUB_AGT_ADR_CAM,
	0,
	0,
	0,
	0,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_TIMR,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX1,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX2,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX3,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX4,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX5,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX6,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX7,
	QLA82XX_HW_CRB_HUB_AGT_ADR_XDMA,
	QLA82XX_HW_CRB_HUB_AGT_ADR_I2Q,
	QLA82XX_HW_CRB_HUB_AGT_ADR_ROMUSB,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX8,
	QLA82XX_HW_CRB_HUB_AGT_ADR_RPMX9,
	QLA82XX_HW_CRB_HUB_AGT_ADR_OCM0,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_SMB,
	QLA82XX_HW_CRB_HUB_AGT_ADR_I2C0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_I2C1,
	0,
	QLA82XX_HW_CRB_HUB_AGT_ADR_PGNC,
	0,
};
/* Device states */
char *q_dev_state[] = {
	 "Unknown",
	"Cold",
	"Initializing",
	"Ready",
	"Need Reset",
	"Need Quiescent",
	"Failed",
	"Quiescent",
};

char *qdev_state(uint32_t dev_state)
{
	return q_dev_state[dev_state];
}

/*
 * Set the CRB window based on the offset.
 * Return 0 if successful; 1 otherwise
*/
void qla82xx_pci_change_crbwindow_128M(scsi_qla_host_t *ha, int wndw)
{
	WARN_ON(1);
}

/*
 * In: 'off' is offset from CRB space in 128M pci map
 * Out: 'off' is 2M pci map addr
 * side effect: lock crb window
 */
static void
qla82xx_pci_set_crbwindow_2M(scsi_qla_host_t *ha, ulong *off)
{
	u32 win_read;

	ha->crb_win = CRB_HI(*off);
	writel(ha->crb_win,
		(void *)(CRB_WINDOW_2M + ha->nx_pcibase));

	/* Read back value to make sure write has gone through before trying
	* to use it. */
	win_read = RD_REG_DWORD((void *)(CRB_WINDOW_2M + ha->nx_pcibase));
	if (win_read != ha->crb_win) {
		DEBUG2(qla_printk(KERN_INFO, ha,
		    "%s: Written crbwin (0x%x) != Read crbwin (0x%x), "
		    "off=0x%lx\n", __func__, ha->crb_win, win_read, *off));
	}
	*off = (*off & MASK(16)) + CRB_INDIRECT_2M + ha->nx_pcibase;
}

static inline unsigned long
qla82xx_pci_set_crbwindow(scsi_qla_host_t *ha, u64 off)
{
	/*
	* See if we are currently pointing to the region we want to use next.
	*/
	if ((off >= QLA82XX_CRB_PCIX_HOST) && (off < QLA82XX_CRB_DDR_NET)) {
		/*
		* No need to change window. PCIX and PCIE regs are in both
		* windows.
		*/
		return off;
	}

	if ((off >= QLA82XX_CRB_PCIX_HOST) && (off < QLA82XX_CRB_PCIX_HOST2)) {
		/* We are in first CRB window */
		if (ha->curr_window != 0)
			qla82xx_pci_change_crbwindow_128M(ha, 0);

		return off;
	}

	if ((off > QLA82XX_CRB_PCIX_HOST2) && (off < QLA82XX_CRB_MAX)) {
		/* We are in second CRB window */
		off = off - QLA82XX_CRB_PCIX_HOST2 + QLA82XX_CRB_PCIX_HOST;

		if (ha->curr_window != 1) {
			qla82xx_pci_change_crbwindow_128M(ha, 1);
			return off;
		}

		if ((off >= QLA82XX_PCI_DIRECT_CRB) &&
		    (off < QLA82XX_PCI_CAMQM_MAX)) {
			/*
			* We are in the QM or direct access register region - do
			* nothing
			*/
			return off;
		}
	}
	/* strange address given */
	dump_stack();
	qla_printk(KERN_WARNING, ha,
		"Warning: qla82xx_pci_set_crbwindow called with"
		" an unknown address(0x%llx)\n", (unsigned long long)off);
	return off;
}

#define CRB_WIN_LOCK_TIMEOUT 100000000

int qla82xx_crb_win_lock(scsi_qla_host_t *ha)
{
	int i;
	int done = 0, timeout = 0;

	while (!done) {
		/* acquire semaphore3 from PCI HW block */
		done = qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM7_LOCK));
		if (done == 1)
			break;
		if (timeout >= CRB_WIN_LOCK_TIMEOUT)
			return -1;
		timeout++;

		/* Yield CPU */
		if (!in_atomic())
			schedule();
		else {
			for (i = 0; i < 20; i++)
				cpu_relax();    /*This a nop instr on i386*/
		}
	}
	qla82xx_wr_32(ha, QLA82XX_CRB_WIN_LOCK_ID, ha->portnum);
	return 0;
}

void qla82xx_crb_win_unlock(scsi_qla_host_t *ha)
{
	qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM7_UNLOCK));
}

int
qla82xx_pci_get_crb_addr_2M(scsi_qla_host_t *ha, ulong *off)
{
	struct crb_128M_2M_sub_block_map *m;

	if (*off >= QLA82XX_CRB_MAX)
		return -1;

	if (*off >= QLA82XX_PCI_CAMQM && (*off < QLA82XX_PCI_CAMQM_2M_END)) {
		*off = (*off - QLA82XX_PCI_CAMQM) +
		    QLA82XX_PCI_CAMQM_2M_BASE + ha->nx_pcibase;
		return 0;
	}

	if (*off < QLA82XX_PCI_CRBSPACE)
		return -1;

	*off -= QLA82XX_PCI_CRBSPACE;
	/*
	 * Try direct map
	 */

	m = &crb_128M_2M_map[CRB_BLK(*off)].sub_block[CRB_SUBBLK(*off)];

	if (m->valid && (m->start_128M <= *off) && (m->end_128M > *off)) {
		*off = *off + m->start_2M - m->start_128M + ha->nx_pcibase;
		return 0;
	}

	/*
	 * Not in direct map, use crb window
	 */
	return 1;
}

int
qla82xx_wr_32(scsi_qla_host_t *ha, ulong off, u32 data)
{
	unsigned long flags = 0;
	int rv;

	rv = qla82xx_pci_get_crb_addr_2M(ha, &off);

	BUG_ON(rv == -1);

	if (rv == 1) {
		write_lock_irqsave(&ha->hw_lock, flags);
		qla82xx_crb_win_lock(ha);
		qla82xx_pci_set_crbwindow_2M(ha, &off);
	}

	writel(data, (void __iomem *)off);

	if (rv == 1) {
		qla82xx_crb_win_unlock(ha);
		write_unlock_irqrestore(&ha->hw_lock, flags);
	}
	return 0;
}

int
qla82xx_rd_32(scsi_qla_host_t *ha, ulong off)
{
	unsigned long flags = 0;
	int rv;
	u32 data;

	rv = qla82xx_pci_get_crb_addr_2M(ha, &off);

	BUG_ON(rv == -1);

	if (rv == 1) {
		write_lock_irqsave(&ha->hw_lock, flags);
		qla82xx_crb_win_lock(ha);
		qla82xx_pci_set_crbwindow_2M(ha, &off);
	}

	data = RD_REG_DWORD((void __iomem *)off);

	if (rv == 1) {
		qla82xx_crb_win_unlock(ha);
		write_unlock_irqrestore(&ha->hw_lock, flags);
	}
	return data;
}

#define IDC_LOCK_TIMEOUT 100000000

int qla82xx_idc_lock(scsi_qla_host_t *ha)
{
	int i;
	int done = 0, timeout = 0;

	while (!done) {
		/* acquire semaphore5 from PCI HW block */
		done = qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM5_LOCK));
		if (done == 1)
			break;
		if (timeout >= IDC_LOCK_TIMEOUT)
			return -1;

		timeout++;

		/* Yield CPU */
		if (!in_interrupt())
			schedule();
		else {
			for (i = 0; i < 20; i++)
				cpu_relax();    /*This a nop instr on i386*/
		}
	}

	return 0;
}

void qla82xx_idc_unlock(scsi_qla_host_t *ha)
{
	qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM5_UNLOCK));
}

/*  PCI Windowing for DDR regions.  */
#define QLA82XX_ADDR_IN_RANGE(addr, low, high) \
	(((addr) <= (high)) && ((addr) >= (low)))

/*
* check memory access boundary.
* used by test agent. Support ddr access only for now
*/
static unsigned long
qla82xx_pci_mem_bound_check(scsi_qla_host_t *ha,
    unsigned long long addr, int size)
{
	if (!QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_DDR_NET,
	    QLA82XX_ADDR_DDR_NET_MAX) ||
	    !QLA82XX_ADDR_IN_RANGE(addr + size - 1, QLA82XX_ADDR_DDR_NET,
	    QLA82XX_ADDR_DDR_NET_MAX) ||
	    ((size != 1) && (size != 2) && (size != 4) && (size != 8))) {
		return 0;
	}
	return 1;
}

int qla82xx_pci_set_window_warning_count;

unsigned long
qla82xx_pci_set_window(scsi_qla_host_t *ha, unsigned long long addr)
{
	int window;
	u32 win_read;

	if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_DDR_NET,
	    QLA82XX_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		window = MN_WIN(addr);
		ha->ddr_mn_window = window;
		qla82xx_wr_32(ha, ha->mn_win_crb | QLA82XX_PCI_CRBSPACE, window);
		win_read = qla82xx_rd_32(ha, ha->mn_win_crb | QLA82XX_PCI_CRBSPACE);
		if ((win_read << 17) != window) {
			qla_printk(KERN_WARNING, ha,
			    "%s: Written MNwin (0x%x) != Read MNwin (0x%x)\n",
			    __func__, window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_DDR_NET;
	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_OCM0,
	    QLA82XX_ADDR_OCM0_MAX)) {
		unsigned int temp1;
		if ((addr & 0x00ff800) == 0xff800) {
			qla_printk(KERN_WARNING, ha,
			    "%s: QM access not handled.\n", __func__);
			addr = -1UL;
		}

		window = OCM_WIN(addr);
		ha->ddr_mn_window = window;
		qla82xx_wr_32(ha, ha->mn_win_crb | QLA82XX_PCI_CRBSPACE, window);
		win_read = qla82xx_rd_32(ha, ha->mn_win_crb | QLA82XX_PCI_CRBSPACE);
		temp1 = ((window & 0x1FF) << 7) |
		    ((window & 0x0FFFE0000) >> 17);
		if (win_read != temp1) {
			qla_printk(KERN_WARNING, ha,
			    "%s: Written OCMwin (0x%x) != Read OCMwin (0x%x)\n",
			    __func__, temp1, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_OCM0_2M;

	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_QDR_NET,
	    QLA82XX_P3_ADDR_QDR_NET_MAX)) {
		/* QDR network side */
		window = MS_WIN(addr);
		ha->qdr_sn_window = window;
		qla82xx_wr_32(ha, ha->ms_win_crb | QLA82XX_PCI_CRBSPACE, window);
		win_read = qla82xx_rd_32(ha, ha->ms_win_crb | QLA82XX_PCI_CRBSPACE);
		if (win_read != window) {
			qla_printk(KERN_WARNING, ha,
			    "%s: Written MSwin (0x%x) != Read MSwin (0x%x)\n",
			    __func__, window, win_read);
		}
		addr = GET_MEM_OFFS_2M(addr) + QLA82XX_PCI_QDR_NET;
	} else {
		/*
		* peg gdb frequently accesses memory that doesn't exist,
		* this limits the chit chat so debugging isn't slowed down.
		*/
		if ((qla82xx_pci_set_window_warning_count++ < 8) ||
		    (qla82xx_pci_set_window_warning_count%64 == 0)) {
			qla_printk(KERN_WARNING, ha,
			    "%s: Warning:%s Unknown address range!\n", __func__,
			    QLA2XXX_DRIVER_NAME);
		}
		addr = -1UL;
	}

	return addr;
}

/* check if address is in the same windows as the previous access */
static int qla82xx_pci_is_same_window(scsi_qla_host_t *ha,
				      unsigned long long addr)
{
	int			window;
	unsigned long long	qdr_max;

	qdr_max = QLA82XX_P3_ADDR_QDR_NET_MAX;

	if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_DDR_NET,
	    QLA82XX_ADDR_DDR_NET_MAX)) {
		/* DDR network side */
		BUG();	/* MN access can not come here */
	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_OCM0,
	    QLA82XX_ADDR_OCM0_MAX)) {
		return 1;
	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_OCM1,
	    QLA82XX_ADDR_OCM1_MAX)) {
		return 1;
	} else if (QLA82XX_ADDR_IN_RANGE(addr, QLA82XX_ADDR_QDR_NET, qdr_max)) {
		/* QDR network side */
		window = ((addr - QLA82XX_ADDR_QDR_NET) >> 22) & 0x3f;
		if (ha->qdr_sn_window == window)
			return 1;
	}

	return 0;
}

static int qla82xx_pci_mem_read_direct(scsi_qla_host_t *ha,
	u64 off, void *data, int size)
{
	unsigned long   flags;
	void           *addr = NULL;
	int             ret = 0;
	u64             start;
	uint8_t         *mem_ptr = NULL;
	unsigned long   mem_base;
	unsigned long   mem_page;

	write_lock_irqsave(&ha->hw_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = qla82xx_pci_set_window(ha, off);
	if ((start == -1UL) ||
	    (qla82xx_pci_is_same_window(ha, off + size - 1) == 0)) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		qla_printk(KERN_ERR, ha, "Out of bound PCI memory access. "
		    "offset is 0x%llx\n", (unsigned long long)off);
		return -1;
	}

	write_unlock_irqrestore(&ha->hw_lock, flags);
	mem_base = pci_resource_start(ha->pdev, 0);
	mem_page = start & PAGE_MASK;
	/* Map two pages whenever user tries to access addresses in two
	   consecutive pages.
	 */
	if (mem_page != ((start + size - 1) & PAGE_MASK))
		mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE * 2);
	else
		mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE);
	if (mem_ptr == 0UL) {
		*(u8  *)data = 0;
		return -1;
	}
	addr = mem_ptr;
	addr += start & (PAGE_SIZE - 1);
	write_lock_irqsave(&ha->hw_lock, flags);

	switch (size) {
	case 1:
		*(u8  *)data = readb(addr);
		break;
	case 2:
		*(u16 *)data = readw(addr);
		break;
	case 4:
		*(u32 *)data = readl(addr);
		break;
	case 8:
		*(u64 *)data = readq(addr);
		break;
	default:
		ret = -1;
		break;
	}
	write_unlock_irqrestore(&ha->hw_lock, flags);

	if (mem_ptr)
		iounmap(mem_ptr);
	return ret;
}

static int
qla82xx_pci_mem_write_direct(scsi_qla_host_t *ha, u64 off, void *data, int size)
{
	unsigned long   flags;
	void           *addr = NULL;
	int             ret = 0;
	u64             start;
	uint8_t         *mem_ptr = NULL;
	unsigned long   mem_base;
	unsigned long   mem_page;

	write_lock_irqsave(&ha->hw_lock, flags);

	/*
	 * If attempting to access unknown address or straddle hw windows,
	 * do not access.
	 */
	start = qla82xx_pci_set_window(ha, off);
	if ((start == -1UL) ||
	    (qla82xx_pci_is_same_window(ha, off + size - 1) == 0)) {
		write_unlock_irqrestore(&ha->hw_lock, flags);
		qla_printk(KERN_ERR, ha,
		    "Out of bound PCI memory access. Offset is 0x%llx\n",
		    (unsigned long long)off);
		return -1;
	}

	write_unlock_irqrestore(&ha->hw_lock, flags);
	mem_base = pci_resource_start(ha->pdev, 0);
	mem_page = start & PAGE_MASK;
	/* Map two pages whenever user tries to access addresses in two
	   consecutive pages.
	 */
	if (mem_page != ((start + size - 1) & PAGE_MASK))
		mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE*2);
	else
		mem_ptr = ioremap(mem_base + mem_page, PAGE_SIZE);
	if (mem_ptr == 0UL)
		return -1;

	addr = mem_ptr;
	addr += start & (PAGE_SIZE - 1);
	write_lock_irqsave(&ha->hw_lock, flags);

	switch (size) {
	case 1:
		writeb(*(u8  *)data, addr);
		break;
	case 2:
		writew(*(u16 *)data, addr);
		break;
	case 4:
		writel(*(u32 *)data, addr);
		break;
	case 8:
		writeq(*(u64 *)data, addr);
		break;
	default:
		ret = -1;
		break;
	}
	write_unlock_irqrestore(&ha->hw_lock, flags);
	if (mem_ptr)
		iounmap(mem_ptr);
	return ret;
}

unsigned long qla82xx_decode_crb_addr(unsigned long addr)
{
	int i;
	unsigned long base_addr, offset, pci_base;

	if (!qla82xx_crb_table_initialized)
		qla82xx_crb_addr_transform_setup();

	pci_base = ADDR_ERROR;
	base_addr = addr & 0xfff00000;
	offset = addr & 0x000fffff;

	for (i = 0; i < MAX_CRB_XFORM; i++) {
		if (crb_addr_xform[i] == base_addr) {
			pci_base = i << 20;
			break;
		}
	}

	if (pci_base == ADDR_ERROR)
		return pci_base;

	return pci_base + offset;
}

static long rom_max_timeout = 100;
static long qla82xx_rom_lock_timeout = 100;

int
qla82xx_rom_lock(scsi_qla_host_t *ha)
{
	int i;
	int done = 0, timeout = 0;

	while (!done) {
		/* acquire semaphore2 from PCI HW block */

		done = qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM2_LOCK));
		if (done == 1)
			break;
		if (timeout >= qla82xx_rom_lock_timeout) {
			qla_printk(KERN_WARNING, ha,
				"%s(%ld) Failed to acquire rom lock\n",
				__func__, ha->host_no);
			return -1;
		}
		timeout++;

		/*
		 * Yield CPU
		 */
		if (!in_atomic())
			schedule();
		else {
			for (i = 0; i < 20; i++)
				cpu_relax(); /*This a nop instr on i386*/
		}
	}
	qla82xx_wr_32(ha, QLA82XX_ROM_LOCK_ID, ROM_LOCK_DRIVER);
	return 0;
}

void
qla82xx_rom_unlock(scsi_qla_host_t *ha)
{
	qla82xx_rd_32(ha, QLA82XX_PCIE_REG(PCIE_SEM2_UNLOCK));
}

int
qla82xx_wait_rom_busy(scsi_qla_host_t *ha)
{
	long timeout = 0;
	long done = 0 ;

	while (done == 0) {
		done = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_STATUS);
		done &= 4;
		timeout++;
		if (timeout >= rom_max_timeout) {
			qla_printk(KERN_WARNING, ha,
			    "Timeout reached  waiting for rom busy\n");
			return -1;
		}
	}
	return 0;
}

int
qla82xx_wait_rom_done(scsi_qla_host_t *ha)
{
	long timeout = 0;
	long done = 0 ;

	while (done == 0) {
		done = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_STATUS);
		done &= 2;
		timeout++;
		if (timeout >= rom_max_timeout) {
			qla_printk(KERN_ERR, ha,
			    "Timeout reached  waiting for rom done");
			return -1;
		}
	}
	return 0;
}

int
qla82xx_md_rw_32(scsi_qla_host_t *ha, uint32_t off, u32 data, uint8_t flag)
{
	uint32_t  off_value, rval = 0;

	WRT_REG_DWORD((void *)(CRB_WINDOW_2M + ha->nx_pcibase),
	    (off & 0xFFFF0000));

	/* Read back value to make sure write has gone through */
	RD_REG_DWORD((void *)(CRB_WINDOW_2M + ha->nx_pcibase));
	off_value  = (off & 0x0000FFFF);

	if (flag)
		WRT_REG_DWORD((void *)
		    (off_value + CRB_INDIRECT_2M + ha->nx_pcibase),
		    data);
	else
		rval = RD_REG_DWORD((void *)
		    (off_value + CRB_INDIRECT_2M + ha->nx_pcibase));

	return rval;
}

int
qla82xx_do_rom_fast_read(scsi_qla_host_t *ha, int addr, int *valp)
{
	/* Dword reads to flash. */
	qla82xx_md_rw_32(ha, MD_DIRECT_ROM_WINDOW, (addr & 0xFFFF0000), 1);
	*valp = qla82xx_md_rw_32(ha, MD_DIRECT_ROM_READ_BASE +
	    (addr & 0x0000FFFF), 0 , 0);
	return 0;
}

int
qla82xx_rom_fast_read(scsi_qla_host_t *ha, int addr, int *valp)
{
	int ret, loops = 0;

	while ((qla82xx_rom_lock(ha) != 0) && (loops < 50000)) {
		udelay(100);
		schedule();
		loops++;
	}
	if (loops >= 50000) {
		qla_printk(KERN_ERR, ha,
		    "%s() qla82xx_rom_lock failed\n", __func__);
		return -1;
	}
	ret = qla82xx_do_rom_fast_read(ha, addr, valp);
	qla82xx_rom_unlock(ha);
	return ret;
}

static void qla82xx_rom_lock_recovery(scsi_qla_host_t *ha)
{
	if (qla82xx_rom_lock(ha))
		qla_printk(KERN_INFO, ha,
			"%s() Resetting the rom lock\n", __func__);
	qla82xx_rom_unlock(ha);
}

int
qla82xx_read_status_reg(scsi_qla_host_t *ha, uint32_t *val)
{
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_RDSR);
	qla82xx_wait_rom_busy(ha);
	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "Error waiting for rom done\n");
		return -1;
	}

	*val = qla82xx_rd_32(ha, QLA82XX_ROMUSB_ROM_RDATA);
	return 0;
}

int
qla82xx_flash_wait_write_finish(scsi_qla_host_t *ha)
{
	long timeout = 0;
	uint32_t done = 1 ;
	uint32_t val;
	int ret = 0;

	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 0);
	while ((done != 0) && (ret == 0)) {
		ret = qla82xx_read_status_reg(ha, &val);
		done = val & 1;
		timeout++;
		udelay(10);
		cond_resched();
		if (timeout >= 50000) {
			qla_printk(KERN_WARNING, ha,
			    "Timeout reached  waiting for write finish");
			return -1;
		}
	}
	return ret;
}

int
qla82xx_flash_set_write_enable(scsi_qla_host_t *ha)
{
	uint32_t val;
	qla82xx_wait_rom_busy(ha);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 0);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_WREN);
	qla82xx_wait_rom_busy(ha);
	if (qla82xx_wait_rom_done(ha))
		return -1;
	if (qla82xx_read_status_reg(ha, &val) != 0)
		return -1;
	if ((val & 2) != 2)
		return -1;
	return 0;
}

int
qla82xx_write_status_reg(scsi_qla_host_t *ha, uint32_t val)
{
	if (qla82xx_flash_set_write_enable(ha))
			return -1;
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_WDATA, val);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, 0x1);
	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "Error waiting for rom done\n");
		return -1;
	}

	return qla82xx_flash_wait_write_finish(ha);
}


int
qla82xx_write_disable_flash(scsi_qla_host_t *ha)
{
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_WRDI);
	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "Error waiting for rom done\n");
		return -1;
	}

	return 0;
}

int ql82xx_rom_lock_d(scsi_qla_host_t *ha)
{
	int loops = 0;
	while ((qla82xx_rom_lock(ha) != 0) && (loops < 50000)) {
		udelay(100);
		cond_resched();
		loops++;
	}
	if (loops >= 50000) {
		qla_printk(KERN_WARNING, ha, "ROM lock failed\n");
		return -1;
	}
	return 0;;
}

int
qla82xx_write_flash_dword(scsi_qla_host_t *ha, uint32_t flashaddr,
     uint32_t data)
{
	int ret = 0;

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		qla_printk(KERN_INFO, ha,
		    "%s(%ld): ROM Lock failed\n", __func__, ha->host_no);
		return ret;
	}

	if (qla82xx_flash_set_write_enable(ha))
		goto done_write;

	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_WDATA, data);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ADDRESS, flashaddr);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 3);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_PP);
	qla82xx_wait_rom_busy(ha);
	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "Error waiting for rom done\n");
		ret = -1;
		goto done_write;
	}

	ret = qla82xx_flash_wait_write_finish(ha);

done_write:
	qla82xx_rom_unlock(ha);
	return ret;
}

#define BLOCK_PROTECT_BITS 0x0F

/*
 * Reset all block protect bits
 */
int
qla82xx_unprotect_flash(scsi_qla_host_t *ha)
{
	int ret;
	uint32_t val;

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		qla_printk(KERN_ERR, ha,
		    "%s(%ld): ROM Lock failed\n", __func__, ha->host_no);
		return ret;
	}
	ret = qla82xx_read_status_reg(ha, &val);
	if (ret < 0)
		goto done_unprotect;

	val &= ~(BLOCK_PROTECT_BITS << 2);
	ret = qla82xx_write_status_reg(ha, val);
	if (ret < 0) {
		val |= (BLOCK_PROTECT_BITS << 2);
		qla82xx_write_status_reg(ha, val);
	}

	if (qla82xx_write_disable_flash(ha) != 0)
		qla_printk(KERN_WARNING, ha, "Write disable failed\n");
done_unprotect:
	qla82xx_rom_unlock(ha);
	return ret;
}

int
qla82xx_protect_flash(scsi_qla_host_t *ha)
{
	int ret;
	uint32_t val;

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		qla_printk(KERN_INFO, ha,
		    "%s(%ld): ROM Lock failed\n", __func__, ha->host_no);
		return ret;
	}
	ret = qla82xx_read_status_reg(ha, &val);
	if (ret < 0)
		goto done_protect;

	val |= (BLOCK_PROTECT_BITS << 2);
	/* LOCK all sectors */
	ret = qla82xx_write_status_reg(ha, val);
	if (ret < 0)
		qla_printk(KERN_WARNING, ha, "Write status register failed\n");

	if (qla82xx_write_disable_flash(ha) != 0)
		qla_printk(KERN_WARNING, ha, "Write disable failed\n");

done_protect:
	qla82xx_rom_unlock(ha);
	return ret;
}

int
qla82xx_erase_sector(scsi_qla_host_t *ha, int addr)
{
	int ret = 0;

	ret = ql82xx_rom_lock_d(ha);
	if (ret < 0) {
		qla_printk(KERN_INFO, ha,
		    "%s(%ld): ROM Lock failed\n", __func__, ha->host_no);
		return ret;
	}


	qla82xx_flash_set_write_enable(ha);

	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ADDRESS, addr);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_ABYTE_CNT, 3);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_ROM_INSTR_OPCODE, M25P_INSTR_SE);

	if (qla82xx_wait_rom_done(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "Error waiting for rom done\n");
		ret = -1;
		goto done;
	}

	ret = qla82xx_flash_wait_write_finish(ha);

done:
	qla82xx_rom_unlock(ha);
	return ret;
}


/* This routine does CRB initialize sequence
 *  to put the ISP into operational state
 */
int qla82xx_pinit_from_rom(scsi_qla_host_t *ha, int verbose)
{
	int addr, val;
	int i ;
	struct crb_addr_pair *buf;
	unsigned long off;
	unsigned offset, n;

	/* Halt all the indiviual PEGs and other blocks of the ISP */
	qla82xx_rom_lock(ha);

	/* disable all I2Q */
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x10, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x14, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x18, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x1c, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x20, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_I2Q + 0x24, 0x0);

	/* diable all niu interrupts */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x40, 0xff);
	/* disable xge rx/tx */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x70000, 0x00);
	/* disable xg1 rx/tx */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x80000, 0x00);
	/* disable sideband mac */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x90000, 0x00);
	/* disable ap0 mac */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0xa0000, 0x00);
	/* disable ap1 mac */
	qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0xb0000, 0x00);

	/* halt sre */
	val = qla82xx_rd_32(ha, QLA82XX_CRB_SRE + 0x1000);
	qla82xx_wr_32(ha, QLA82XX_CRB_SRE + 0x1000, val & (~(0x1)));

	/* halt epg */
	qla82xx_wr_32(ha, QLA82XX_CRB_EPG + 0x1300, 0x1);

	/* halt timers */
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x0, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x8, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x10, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x18, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x100, 0x0);
	qla82xx_wr_32(ha, QLA82XX_CRB_TIMER + 0x200, 0x0);

	/* halt pegs */
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_0 + 0x3c, 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_1 + 0x3c, 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_2 + 0x3c, 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_3 + 0x3c, 1);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_4 + 0x3c, 1);
	msleep(5);

	/* big hammer */
	if (test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags))
		/* don't reset CAM block on reset */
		qla82xx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0xfeffffff);
	else
		qla82xx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0xffffffff);

	qla82xx_rom_unlock(ha);

	/* Read the signature value from the flash.
	 * Offset 0: Contain signature (0xcafecafe)
	 * Offset 4: Offset and number of addr/value pairs
	 * that present in CRB initialize sequence
	 */
	if (qla82xx_rom_fast_read(ha, 0, &n) != 0 || n != 0xcafecafeUL ||
	    qla82xx_rom_fast_read(ha, 4, &n) != 0) {
		qla_printk(KERN_WARNING, ha,
		    "[ERROR] Reading crb_init area: n: %08x\n", n);
		return -1;
	}

	/* Offset in flash = lower 16 bits
	 * Number of entries = upper 16 bits
	 */
	offset = n & 0xffffU;
	n = (n >> 16) & 0xffffU;

	/* number of addr/value pair should not exceed 1024 entries */
	if (n  >= 1024) {
		qla_printk(KERN_WARNING, ha,
		    "%s: %s:n=0x%x [ERROR] Card flash not initialized.\n",
		    QLA2XXX_DRIVER_NAME, __func__, n);
		return -1;
	}

	qla_printk(KERN_INFO, ha,
	    "%s: %d CRB init values found in ROM.\n", QLA2XXX_DRIVER_NAME, n);

	buf = kmalloc(n * sizeof(struct crb_addr_pair), GFP_KERNEL);
	if (buf == NULL) {
		qla_printk(KERN_WARNING, ha,
		    "%s: [ERROR] Unable to malloc memory.\n",
		    QLA2XXX_DRIVER_NAME);
		return -1;
	}

	for (i = 0; i < n; i++) {
		if (qla82xx_rom_fast_read(ha, 8*i + 4*offset, &val) != 0 ||
		    qla82xx_rom_fast_read(ha, 8*i + 4*offset + 4, &addr) != 0) {
			kfree(buf);
			return -1;
		}

		buf[i].addr = addr;
		buf[i].data = val;
	}

	for (i = 0; i < n; i++) {
		/* Translate internal CRB initialization
		 * address to PCI bus address
		 */
		off = qla82xx_decode_crb_addr((unsigned long)buf[i].addr) +
		    QLA82XX_PCI_CRBSPACE;
		/* Not all CRB  addr/value pair to be written,
		 * some of them are skipped
		 */

		/* skipping cold reboot MAGIC */
		if (off == QLA82XX_CAM_RAM(0x1fc))
			continue;

		/* do not reset PCI */
		if (off == (ROMUSB_GLB + 0xbc))
			continue;

		/* skip core clock, so that firmware can increase the clock */
		if (off == (ROMUSB_GLB + 0xc8))
			continue;

		/* skip the function enable register */
		if (off == QLA82XX_PCIE_REG(PCIE_SETUP_FUNCTION))
			continue;

		if (off == QLA82XX_PCIE_REG(PCIE_SETUP_FUNCTION2))
			continue;

		if ((off & 0x0ff00000) == QLA82XX_CRB_SMB)
			continue;

		if ((off & 0x0ff00000) == QLA82XX_CRB_DDR_NET)
			continue;

		if (off == ADDR_ERROR) {
			qla_printk(KERN_WARNING, ha,
			    "%s: [ERROR] Unknown addr: 0x%08lx\n",
			    QLA2XXX_DRIVER_NAME, buf[i].addr);
			continue;
		}

		qla82xx_wr_32(ha, off, buf[i].data);

		/* ISP requires much bigger delay to settle down,
		 * else crb_window returns 0xffffffff
		 */
		if (off == QLA82XX_ROMUSB_GLB_SW_RESET)
			msleep(1000);

		/* ISP requires millisec delay between
		 * successive CRB register updates
		 */
		msleep(1);
	}

	kfree(buf);

	/* Resetting the data and instruction cache */
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_D+0xec, 0x1e);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_D+0x4c, 8);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_I+0x4c, 8);

	/* Clear all protocol processing engines */
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_0+0x8, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_0+0xc, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_1+0x8, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_1+0xc, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_2+0x8, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_2+0xc, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_3+0x8, 0);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_3+0xc, 0);

	return 0;
}

int
qla82xx_pci_mem_read_2M(scsi_qla_host_t *ha,
		u64 off, void *data, int size)
{
	int i, j = 0, k, start, end, loop, sz[2], off0[2];
	int	      shift_amount;
	uint32_t      temp;
	uint64_t      off8, val, mem_crb, word[2] = {0, 0};

	/*
	 * If not MN, go check for MS or invalid.
	 */

	if (off >= QLA82XX_ADDR_QDR_NET && off <= QLA82XX_P3_ADDR_QDR_NET_MAX)
		mem_crb = QLA82XX_CRB_QDR_NET;
	else {
		mem_crb = QLA82XX_CRB_DDR_NET;
		if (qla82xx_pci_mem_bound_check(ha, off, size) == 0)
			return qla82xx_pci_mem_read_direct(ha,
			    off, data, size);
	}

	off8 = off & 0xfffffff0;
	off0[0] = off & 0xf;
	sz[0] = (size < (16 - off0[0])) ? size : (16 - off0[0]);
	shift_amount = 4;

	loop = ((off0[0] + size - 1) >> shift_amount) + 1;
	off0[1] = 0;
	sz[1] = size - sz[0];

	for (i = 0; i < loop; i++) {
		temp = off8 + (i << shift_amount);
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_ADDR_LO, temp);
		temp = 0;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_ADDR_HI, temp);
		temp = MIU_TA_CTL_ENABLE;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);
		temp = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = qla82xx_rd_32(ha, mem_crb + MIU_TEST_AGT_CTRL);
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				dev_err(&ha->pdev->dev,
				    "failed to read through agent\n");
			break;
		}

		start = off0[i] >> 2;
		end   = (off0[i] + sz[i] - 1) >> 2;
		for (k = start; k <= end; k++) {
			temp = qla82xx_rd_32(ha,
					mem_crb + MIU_TEST_AGT_RDDATA(k));
			word[i] |= ((uint64_t)temp << (32 * (k & 1)));
		}
	}

	if (j >= MAX_CTL_CHECK)
		return -1;

	if ((off0[0] & 7) == 0) {
		val = word[0];
	} else {
		val = ((word[0] >> (off0[0] * 8)) & (~(~0ULL << (sz[0] * 8)))) |
			((word[1] & (~(~0ULL << (sz[1] * 8)))) << (sz[0] * 8));
	}

	switch (size) {
	case 1:
		*(uint8_t  *)data = val;
		break;
	case 2:
		*(uint16_t *)data = val;
		break;
	case 4:
		*(uint32_t *)data = val;
		break;
	case 8:
		*(uint64_t *)data = val;
		break;
	}
	return 0;
}

int
qla82xx_pci_mem_write_2M(scsi_qla_host_t *ha,
		u64 off, void *data, int size)
{
	int i, j, ret = 0, loop, sz[2], off0;
	int scale, shift_amount, startword;
	uint32_t temp;
	uint64_t off8, mem_crb, tmpw, word[2] = {0, 0};

	/*
	 * If not MN, go check for MS or invalid.
	 */
	if (off >= QLA82XX_ADDR_QDR_NET && off <= QLA82XX_P3_ADDR_QDR_NET_MAX)
		mem_crb = QLA82XX_CRB_QDR_NET;
	else {
		mem_crb = QLA82XX_CRB_DDR_NET;
		if (qla82xx_pci_mem_bound_check(ha, off, size) == 0)
			return qla82xx_pci_mem_write_direct(ha,
			    off, data, size);
	}

	off0 = off & 0x7;
	sz[0] = (size < (8 - off0)) ? size : (8 - off0);
	sz[1] = size - sz[0];

	off8 = off & 0xfffffff0;
	loop = (((off & 0xf) + size - 1) >> 4) + 1;
	shift_amount = 4;
	scale = 2;
	startword = (off & 0xf)/8;

	for (i = 0; i < loop; i++) {
		if (qla82xx_pci_mem_read_2M(ha, off8 +
		    (i << shift_amount), &word[i * scale], 8))
			return -1;
	}

	switch (size) {
	case 1:
		tmpw = *((uint8_t *)data);
		break;
	case 2:
		tmpw = *((uint16_t *)data);
		break;
	case 4:
		tmpw = *((uint32_t *)data);
		break;
	case 8:
	default:
		tmpw = *((uint64_t *)data);
		break;
	}

	if (sz[0] == 8) {
		word[startword] = tmpw;
	} else {
		word[startword] &=
		    ~((~(~0ULL << (sz[0] * 8))) << (off0 * 8));
		word[startword] |= tmpw << (off0 * 8);
	}
	if (sz[1] != 0) {
		word[startword+1] &= ~(~0ULL << (sz[1] * 8));
		word[startword+1] |= tmpw >> (sz[0] * 8);
	}


	for (i = 0; i < loop; i++) {
		temp = off8 + (i << shift_amount);
		qla82xx_wr_32(ha, mem_crb+MIU_TEST_AGT_ADDR_LO, temp);
		temp = 0;
		qla82xx_wr_32(ha, mem_crb+MIU_TEST_AGT_ADDR_HI, temp);
		temp = word[i * scale] & 0xffffffff;
		qla82xx_wr_32(ha, mem_crb+MIU_TEST_AGT_WRDATA_LO, temp);
		temp = (word[i * scale] >> 32) & 0xffffffff;
		qla82xx_wr_32(ha, mem_crb+MIU_TEST_AGT_WRDATA_HI, temp);
		temp = word[i*scale + 1] & 0xffffffff;
		qla82xx_wr_32(ha, mem_crb +
		    MIU_TEST_AGT_WRDATA_UPPER_LO, temp);
		temp = (word[i*scale + 1] >> 32) & 0xffffffff;
		qla82xx_wr_32(ha, mem_crb +
		    MIU_TEST_AGT_WRDATA_UPPER_HI, temp);

		temp = MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);
		temp = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE | MIU_TA_CTL_WRITE;
		qla82xx_wr_32(ha, mem_crb + MIU_TEST_AGT_CTRL, temp);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			temp = qla82xx_rd_32(ha, mem_crb + MIU_TEST_AGT_CTRL);
			if ((temp & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				dev_err(&ha->pdev->dev,
				    "failed to write through agent\n");
			ret = -1;
			break;
		}
	}

	return ret;
}


int
qla82xx_fw_load_from_flash(scsi_qla_host_t *ha)
{
	int  i;
	long size = 0;
	long flashaddr = BOOTLD_START, memaddr = BOOTLD_START;
	u64 data;
	u32 high, low;
	size = (IMAGE_START - BOOTLD_START) / 8;

	for (i = 0; i < size; i++) {
		if ((qla82xx_rom_fast_read(ha, flashaddr, (int *)&low)) ||
		    (qla82xx_rom_fast_read(ha, flashaddr + 4, (int *)&high))) {
			return -1;
		}
		data = ((u64)high << 32) | low ;
		qla82xx_pci_mem_write_2M(ha, memaddr, &data, 8);
		flashaddr += 8;
		memaddr += 8;

		if (i % 0x1000 == 0)
			msleep(1);
	}

	udelay(100);

	read_lock(&ha->hw_lock);
	qla82xx_wr_32(ha, QLA82XX_CRB_PEG_NET_0 + 0x18, 0x1020);
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, 0x80001e);
	read_unlock(&ha->hw_lock);
	return 0;
}

/* PCI related functions */
char *
qla82xx_pci_info_str(struct scsi_qla_host *ha, char *str)
{
	int pcie_reg;
	char lwstr[6];
	uint16_t lnk;

	pcie_reg = pci_find_capability(ha->pdev, PCI_CAP_ID_EXP);
	pci_read_config_word(ha->pdev, pcie_reg + PCI_EXP_LNKSTA, &lnk);
	ha->link_width = (lnk >> 4) & 0x3f;

	strcpy(str, "PCIe (");
	strcat(str, "2.5Gb/s ");
	snprintf(lwstr, sizeof(lwstr), "x%d)", ha->link_width);
	strcat(str, lwstr);
	return str;
}

int
qla82xx_iospace_config(scsi_qla_host_t *ha)
{
	uint32_t len = 0;
	pci_request_regions(ha->pdev, QLA2XXX_DRIVER_NAME);

	len = pci_resource_len(ha->pdev, 0);
	ha->nx_pcibase = (unsigned long)ioremap(pci_resource_start(ha->pdev, 0),
	    len);
	if (!ha->nx_pcibase) {
		qla_printk(KERN_ERR, ha,
		    "cannot remap MMIO (%s), aborting\n", pci_name(ha->pdev));
		pci_release_regions(ha->pdev);
		goto iospace_error_exit;
	}

	/* mapping of IO base pointer */
	ha->iobase = (device_reg_t __iomem *)((uint8_t *)ha->nx_pcibase +
	    0xbc000 + (ha->pdev->devfn << 11));

	if (!ql2xdbwr) {
		ha->nxdb_wr_ptr = (unsigned long)ioremap((pci_resource_start(
		    ha->pdev, 4) + (ha->pdev->devfn << 12)), 4);
		if (!ha->nxdb_wr_ptr) {
			qla_printk(KERN_ERR, ha,
			    "cannot remap MMIO (%s), aborting\n",
			    pci_name(ha->pdev));
			pci_release_regions(ha->pdev);
			goto iospace_error_exit;
		}

		/* Mapping of IO base pointer,
		 * door bell read and write pointer
		 */
		ha->nxdb_rd_ptr = (uint8_t *) ha->nx_pcibase + (512 * 1024) +
		    (ha->pdev->devfn * 8);
	} else {
		ha->nxdb_wr_ptr = (ha->pdev->devfn == 6 ? QLA82XX_CAMRAM_DB1 :
		     QLA82XX_CAMRAM_DB2);
	}

	/* Nx - TODO - Multi queue initialization */
	return 0;
iospace_error_exit:
	return -ENOMEM;
}

/* GS related functions */

/* Initialization related functions */

/**
 * qla82xx_pci_config() - Setup ISP82xx PCI configuration registers.
 * @ha: HA context
 *
 * Returns 0 on success.
*/
int
qla82xx_pci_config(scsi_qla_host_t *ha)
{
	pci_set_master(ha->pdev);

	/* TODO - Check for memory write invalidate setting
	 * Any parity settings
	 * Adjust Max Read Request size
	 * PCI disable ROM
	 */
	ha->chip_revision = ha->pdev->revision;

	return 0;
}

/**
 * qla82xx_reset_chip() - Setup ISP82xx PCI configuration registers.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
void
qla82xx_reset_chip(scsi_qla_host_t *ha)
{
	ha->isp_ops->disable_intrs(ha);
	/* Reset logic code */
}

void qla82xx_config_rings(struct scsi_qla_host *ha)
{
	struct device_reg_82xx __iomem *reg = &ha->iobase->isp82;
	struct init_cb_81xx *icb;

	/* Setup ring parameters in initialization control block. */
	icb = (struct init_cb_81xx *)ha->init_cb;
	icb->request_q_outpointer = __constant_cpu_to_le16(0);
	icb->response_q_inpointer = __constant_cpu_to_le16(0);
	icb->request_q_length = cpu_to_le16(ha->request_q_length);
	icb->response_q_length = cpu_to_le16(ha->response_q_length);
	icb->request_q_address[0] = cpu_to_le32(LSD(ha->request_dma));
	icb->request_q_address[1] = cpu_to_le32(MSD(ha->request_dma));
	icb->response_q_address[0] = cpu_to_le32(LSD(ha->response_dma));
	icb->response_q_address[1] = cpu_to_le32(MSD(ha->response_dma));

	WRT_REG_DWORD((unsigned long  __iomem *)&reg->req_q_out[0], 0);
	WRT_REG_DWORD((unsigned long  __iomem *)&reg->rsp_q_in[0], 0);
	WRT_REG_DWORD((unsigned long  __iomem *)&reg->rsp_q_out[0], 0);
}

void qla82xx_reset_adapter(struct scsi_qla_host *ha)
{
	ha->flags.online = 0;

	qla2x00_try_to_stop_firmware(ha);

	ha->isp_ops->disable_intrs(ha);
}

int qla82xx_check_cmdpeg_state(struct scsi_qla_host *ha)
{
	u32 val = 0;
	int retries = 60;

	do {
		read_lock(&ha->hw_lock);
		val = qla82xx_rd_32(ha, CRB_CMDPEG_STATE);
		read_unlock(&ha->hw_lock);

		switch (val) {
		case PHAN_INITIALIZE_COMPLETE:
		case PHAN_INITIALIZE_ACK:
			return QLA_SUCCESS;
		case PHAN_INITIALIZE_FAILED:
			break;
		default:
			break;
		}
		DEBUG3(printk(KERN_INFO
		    "CRB_CMDPEG_STATE: 0x%x and retries: 0x%x\n",
		    val, retries));

		msleep(500);

	} while (--retries);

	qla_printk(KERN_INFO, ha,
	    "Cmd Peg initialization failed: 0x%x.\n", val);

	val = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_PEGTUNE_DONE);

	read_lock(&ha->hw_lock);
	qla82xx_wr_32(ha, CRB_CMDPEG_STATE, PHAN_INITIALIZE_FAILED);
	read_unlock(&ha->hw_lock);
	return QLA_FUNCTION_FAILED;
}

int qla82xx_check_rcvpeg_state(struct scsi_qla_host *ha)
{
	u32 val = 0;
	int retries = 60;

	do {
		read_lock(&ha->hw_lock);
		val = qla82xx_rd_32(ha, CRB_RCVPEG_STATE);
		read_unlock(&ha->hw_lock);

		switch (val) {
		case PHAN_INITIALIZE_COMPLETE:
		case PHAN_INITIALIZE_ACK:
			return QLA_SUCCESS;
		case PHAN_INITIALIZE_FAILED:
			break;
		default:
			break;
		}

		DEBUG3(printk(KERN_INFO
		    "CRB_RCVPEG_STATE: 0x%x and retries: 0x%x\n",
		    val, retries));

		msleep(500);

	} while (--retries);

	qla_printk(KERN_INFO, ha,
	    "Rcv Peg initialization failed: 0x%x.\n", val);

	read_lock(&ha->hw_lock);
	qla82xx_wr_32(ha, CRB_RCVPEG_STATE, PHAN_INITIALIZE_FAILED);
	read_unlock(&ha->hw_lock);
	return QLA_FUNCTION_FAILED;
}



/* ISR related functions */
uint32_t qla82xx_isr_int_target_mask_enable[8] = {
	ISR_INT_TARGET_MASK, ISR_INT_TARGET_MASK_F1,
	ISR_INT_TARGET_MASK_F2, ISR_INT_TARGET_MASK_F3,
	ISR_INT_TARGET_MASK_F4, ISR_INT_TARGET_MASK_F5,
	ISR_INT_TARGET_MASK_F7, ISR_INT_TARGET_MASK_F7
};

uint32_t qla82xx_isr_int_target_status[8] = {
	ISR_INT_TARGET_STATUS, ISR_INT_TARGET_STATUS_F1,
	ISR_INT_TARGET_STATUS_F2, ISR_INT_TARGET_STATUS_F3,
	ISR_INT_TARGET_STATUS_F4, ISR_INT_TARGET_STATUS_F5,
	ISR_INT_TARGET_STATUS_F7, ISR_INT_TARGET_STATUS_F7
};

static struct qla82xx_legacy_intr_set legacy_intr[] = {
	{
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F0,
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS,
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK,
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(0) },
	{
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F1,
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F1,
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F1,
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(1) },
	{
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F2,
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F2,
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F2,
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(2) },
	{
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F3,
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F3,
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F3,
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(3) },
	{
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F4,
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F4,
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F4,
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(4) },
	{
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F5,
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F5,
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F5,
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(5) },
	{
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F6,
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F6,
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F6,
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(6) },
	{
		.int_vec_bit	=	PCIX_INT_VECTOR_BIT_F7,
		.tgt_status_reg	=	ISR_INT_TARGET_STATUS_F7,
		.tgt_mask_reg	=	ISR_INT_TARGET_MASK_F7,
		.pci_int_reg	=	ISR_MSI_INT_TRIGGER(7) },
};

/**
 * qla82xx_mbx_completion() - Process mailbox command completions.
 * @ha: SCSI driver HA context
 * @mb0: Mailbox0 register
 */
void
qla82xx_mbx_completion(scsi_qla_host_t *ha, uint16_t mb0)
{
	uint16_t	cnt;
	uint16_t __iomem *wptr;
	struct device_reg_82xx __iomem *reg = &ha->iobase->isp82;
	wptr = (uint16_t __iomem *)&reg->mailbox_out[1];

	/* Load return mailbox registers. */
	ha->flags.mbox_int = 1;
	ha->mailbox_out[0] = mb0;

	for (cnt = 1; cnt < ha->mbx_count; cnt++) {
		ha->mailbox_out[cnt] = RD_REG_WORD(wptr);
		wptr++;
	}

	if (ha->mcp) {
		DEBUG3(qla_printk(KERN_INFO, ha,
		    "%s(%ld): Got mailbox completion. cmd=%x.\n",
		    __func__, ha->host_no, ha->mcp->mb[0]));
	} else {
		DEBUG2_3(qla_printk(KERN_ERR, ha,
		    "%s(%ld): MBX pointer ERROR!\n", __func__, ha->host_no));
	}
}

/**
 * qla82xx_intr_handler() - Process interrupts for the ISP82XX
 * @irq:
 * @dev_id: SCSI driver HA context
 * @regs:
 *
 * Called by system whenever the host adapter generates an interrupt.
 *
 * Returns handled flag.
 */
irqreturn_t
qla82xx_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	scsi_qla_host_t	*ha;
	struct device_reg_82xx __iomem *reg;
	int		status = 0, status1 = 0;
	unsigned long	flags;
	unsigned long	iter;
	uint32_t	stat;
	uint16_t	mb[4];

	ha = (scsi_qla_host_t *) dev_id;
	if (!ha) {
		printk(KERN_INFO
		    "%s(): NULL host pointer\n", __func__);
		return IRQ_NONE;
	}

	if (unlikely(pci_channel_offline(ha->pdev)))
		return IRQ_HANDLED;

	/* Check for valid INTa */
	if (!ha->flags.msi_enabled) {
		status = qla82xx_rd_32(ha, ISR_INT_VECTOR);
		if (!(status & ha->nx_legacy_intr.int_vec_bit))
			return IRQ_NONE;

		status1 = qla82xx_rd_32(ha, ISR_INT_STATE_REG);
		if (!ISR_IS_LEGACY_INTR_TRIGGERED(status1))
			return IRQ_NONE;
	}

	/* clear the interrupt */
	qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_status_reg, 0xffffffff);

	/* read twice to ensure write is flushed */
	qla82xx_rd_32(ha, ISR_INT_VECTOR);
	qla82xx_rd_32(ha, ISR_INT_VECTOR);

	reg = &ha->iobase->isp82;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (iter = 1; iter--; ) {

		if (RD_REG_DWORD(&reg->host_int)) {
			stat = RD_REG_DWORD(&reg->host_status);
			if ((stat & HSRX_RISC_INT) == 0)
				break;

			switch (stat & 0xff) {
			case 0x1:
			case 0x2:
			case 0x10:
			case 0x11:
				qla82xx_mbx_completion(ha, MSW(stat));
				status |= MBX_INTERRUPT;
				break;
			case 0x12:
				mb[0] = MSW(stat);
				mb[1] = RD_REG_WORD(&reg->mailbox_out[1]);
				mb[2] = RD_REG_WORD(&reg->mailbox_out[2]);
				mb[3] = RD_REG_WORD(&reg->mailbox_out[3]);
				qla2x00_async_event(ha, mb);
				break;
			case 0x13:
				qla24xx_process_response_queue(ha);
				break;
			default:
				DEBUG2(printk(KERN_WARNING
				    "scsi(%ld): Unrecognized interrupt type "
				    "(%d).\n", ha->host_no, stat & 0xff));
				break;
			}
		}
		WRT_REG_DWORD(&reg->host_int, 0);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	if (!ha->flags.msi_enabled)
		qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg, 0xfbff);

#ifdef QL_DEBUG_LEVEL_17
	if (!irq && ha->flags.eeh_busy)
		qla_printk(KERN_WARNING, ha,
		    "isr: status %x, cmd_flags %lx, mbox_int %x, stat %x\n",
		    status, ha->mbx_cmd_flags, ha->flags.mbox_int, stat);
#endif

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
	    (status & MBX_INTERRUPT) && ha->flags.mbox_int) {
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
		complete(&ha->mbx_intr_comp);
	}
	return IRQ_HANDLED;
}

irqreturn_t
qla82xx_msix_default(int irq, void *dev_id, struct pt_regs *regs)
{
	scsi_qla_host_t	*ha;
	struct device_reg_82xx __iomem *reg;
	int		status = 0;
	unsigned long	flags;
	uint32_t	stat;
	uint16_t	mb[4];

	ha = dev_id;
	reg = &ha->iobase->isp82;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	do {
		if (RD_REG_DWORD(&reg->host_int)) {
			stat = RD_REG_DWORD(&reg->host_status);
			if ((stat & HSRX_RISC_INT) == 0)
				break;

			switch (stat & 0xff) {
			case 0x1:
			case 0x2:
			case 0x10:
			case 0x11:
				qla82xx_mbx_completion(ha, MSW(stat));
				status |= MBX_INTERRUPT;
				break;
			case 0x12:
				mb[0] = MSW(stat);
				mb[1] =
				    RD_REG_WORD(&reg->mailbox_out[1]);
				mb[2] =
				    RD_REG_WORD(&reg->mailbox_out[2]);
				mb[3] =
				    RD_REG_WORD(&reg->mailbox_out[3]);
				qla2x00_async_event(ha, mb);
				break;
			case 0x13:
				qla24xx_process_response_queue(ha);
				break;
			default:
				DEBUG2(printk(KERN_WARNING
				    "scsi(%ld): Unrecognized interrupt"
				    " type (%d).\n",
				    ha->host_no, stat & 0xff));
				break;
			}
		}
		WRT_REG_DWORD(&reg->host_int, 0);
	} while (0);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

#ifdef QL_DEBUG_LEVEL_17
	if (!irq && ha->flags.eeh_busy)
		qla_printk(KERN_WARNING, ha,
		    "isr: status %x, cmd_flags %lx, mbox_int %x, stat %x\n",
		    status, ha->mbx_cmd_flags, ha->flags.mbox_int, stat);
#endif

	if (test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags) &&
	    (status & MBX_INTERRUPT) && ha->flags.mbox_int) {
		set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
		complete(&ha->mbx_intr_comp);
	}
	return IRQ_HANDLED;
}

irqreturn_t
qla82xx_msix_rsp_q(int irq, void *dev_id, struct pt_regs *regs)
{
	scsi_qla_host_t *ha;
	struct device_reg_82xx __iomem *reg;
	unsigned long flags;

	ha = dev_id;
	reg = &ha->iobase->isp82;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	qla24xx_process_response_queue(ha);

	WRT_REG_DWORD(&reg->host_int, 0);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return IRQ_HANDLED;
}

void
qla82xx_poll(int irq, void *dev_id, struct pt_regs *regs)
{
	scsi_qla_host_t	*ha;
	struct device_reg_82xx __iomem *reg;
	int		status = 0;
	unsigned long	flags;
	uint32_t	stat;
	uint16_t	mb[4];

	ha = (scsi_qla_host_t *) dev_id;
	if (!ha) {
		printk(KERN_INFO
		    "%s(): NULL host pointer\n", __func__);
		return;
	}

	reg = &ha->iobase->isp82;
	spin_lock_irqsave(&ha->hardware_lock, flags);

	if (RD_REG_DWORD(&reg->host_int)) {
		stat = RD_REG_DWORD(&reg->host_status);
		switch (stat & 0xff) {
		case 0x1:
		case 0x2:
		case 0x10:
		case 0x11:
			qla82xx_mbx_completion(ha, MSW(stat));
			status |= MBX_INTERRUPT;
			break;
		case 0x12:
			mb[0] = MSW(stat);
			mb[1] = RD_REG_WORD(&reg->mailbox_out[1]);
			mb[2] = RD_REG_WORD(&reg->mailbox_out[2]);
			mb[3] = RD_REG_WORD(&reg->mailbox_out[3]);
			qla2x00_async_event(ha, mb);
			break;
		case 0x13:
			qla24xx_process_response_queue(ha);
			break;
		default:
			qla_printk(KERN_ERR, ha,
			    "scsi(%ld): Unrecognized completion type "
			    "(%d).\n", ha->host_no, stat & 0xff);
			break;
		}
	}
	WRT_REG_DWORD(&reg->host_int, 0);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
qla82xx_enable_intrs(struct scsi_qla_host *ha)
{
	qla82xx_mbx_intr_enable(ha);
	spin_lock_irq(&ha->hardware_lock);
	qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg,
	    0xfbff); /* BIT 10 - set */
	spin_unlock_irq(&ha->hardware_lock);
	ha->interrupts_on = 1;
}

void
qla82xx_disable_intrs(struct scsi_qla_host *ha)
{
	if (ha->interrupts_on)
		qla82xx_mbx_intr_disable(ha);
	spin_lock_irq(&ha->hardware_lock);
	qla82xx_wr_32(ha, ha->nx_legacy_intr.tgt_mask_reg,
	    0x0400); /* BIT 10 - set */
	spin_unlock_irq(&ha->hardware_lock);
	ha->interrupts_on = 0;
}

void qla82xx_init_flags(struct scsi_qla_host *ha)
{
	struct qla82xx_legacy_intr_set *nx_legacy_intr;

	/* ISP 8021 initializations */
	rwlock_init(&ha->hw_lock);
	ha->qdr_sn_window = -1;
	ha->ddr_mn_window = -1;
	ha->curr_window = 255;
	ha->portnum = PCI_FUNC(ha->pdev->devfn); 
	nx_legacy_intr = &legacy_intr[ha->portnum];
	ha->nx_legacy_intr.int_vec_bit = nx_legacy_intr->int_vec_bit;
	ha->nx_legacy_intr.tgt_status_reg = nx_legacy_intr->tgt_status_reg;
	ha->nx_legacy_intr.tgt_mask_reg = nx_legacy_intr->tgt_mask_reg;
	ha->nx_legacy_intr.pci_int_reg = nx_legacy_intr->pci_int_reg;
}

inline void
qla82xx_set_drv_active(scsi_qla_host_t *ha)
{
	uint32_t drv_active;

	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);

	qla_printk(KERN_INFO, ha,
	    "%s(%ld): drv_active = 0x%x\n", __func__,
	    ha->host_no, drv_active);

	/* reset value if all FF's */
	if (drv_active == 0xffffffff) {
		qla82xx_wr_32(ha, QLA82XX_CRB_DRV_ACTIVE, 0);
		drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	}

	drv_active |= (1 << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_ACTIVE, drv_active);
}

inline void
qla82xx_clear_drv_active(scsi_qla_host_t *ha)
{
	uint32_t drv_active;

	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	drv_active &= ~(1 << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_ACTIVE, drv_active);
}

static inline int
qla82xx_need_reset(scsi_qla_host_t *ha)
{
	uint32_t drv_state;
	int rval;

	if (ha->flags.isp82xx_reset_owner)
		return 1;
	else {
		drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
		rval = drv_state & (QLA82XX_DRVST_RST_RDY << (ha->portnum * 4));
		return rval;
	}
}

static inline void
qla82xx_set_rst_ready(scsi_qla_host_t *ha)
{
	uint32_t drv_state;

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);

	/* reset value if all FF's */
	if (drv_state == 0xffffffff) {
		qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, 0);
		drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	}

	drv_state |= (QLA82XX_DRVST_RST_RDY << (ha->portnum * 4));
	qla_printk(KERN_INFO, ha,
	    "%s(%ld):drv_state = 0x%08x\n", __func__, ha->host_no, drv_state);
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, drv_state);
}

static inline void
qla82xx_clear_rst_ready(scsi_qla_host_t *ha)
{
	uint32_t drv_state;

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_state &= ~(QLA82XX_DRVST_RST_RDY << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, drv_state);
}

static inline void
qla82xx_set_qsnt_ready(scsi_qla_host_t *ha)
{
	uint32_t qsnt_state;

	qsnt_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	qsnt_state |= (QLA82XX_DRVST_QSNT_RDY << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, qsnt_state);
}

void
qla82xx_clear_qsnt_ready(scsi_qla_host_t *ha)
{
	uint32_t qsnt_state;

	qsnt_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	qsnt_state &= ~(QLA82XX_DRVST_QSNT_RDY << (ha->portnum * 4));
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_STATE, qsnt_state);
}

int qla82xx_load_fw(scsi_qla_host_t *ha)
{

	int rst;

	/* Put both the PEG CMD and RCV PEG to default state
	 * of 0 before resetting the hardware
	 */
	qla82xx_wr_32(ha, CRB_CMDPEG_STATE, 0);
	qla82xx_wr_32(ha, CRB_RCVPEG_STATE, 0);

	if (qla82xx_pinit_from_rom(ha, 0) != QLA_SUCCESS) {
		qla_printk(KERN_ERR, ha,
			"%s: Error during CRB Initialization\n", __func__);
		return QLA_FUNCTION_FAILED;
	}
	udelay(500);

	/* Bring QM and CAMRAM out of reset */
	rst = qla82xx_rd_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET);
	rst &= ~((1 << 28) | (1 << 24));
	qla82xx_wr_32(ha, QLA82XX_ROMUSB_GLB_SW_RESET, rst);

	/*
	 * FW Load priority:
	 * 1) Operational firmware residing in flash.
	 */
	qla_printk(KERN_INFO, ha,
	    "Attempting to load firmware from flash\n");

	if (qla82xx_fw_load_from_flash(ha) == QLA_SUCCESS) {
		qla_printk(KERN_ERR, ha,
		    "Firmware loaded successfully from flash\n");
		return QLA_SUCCESS;
	} else {
		qla_printk(KERN_ERR, ha,
		    "Firmware load from flash failed\n");
	}

	return QLA_FUNCTION_FAILED;
}

int
qla82xx_start_firmware(scsi_qla_host_t *ha)
{
	int           pcie_cap;
	uint16_t      lnk;

	/* scrub dma mask expansion register */
	qla82xx_wr_32(ha, CRB_DMA_SHIFT, 0x55555555);

	/* Overwrite stale initialization register values */
	qla82xx_wr_32(ha, CRB_CMDPEG_STATE, 0);
	qla82xx_wr_32(ha, CRB_RCVPEG_STATE, 0);
	qla82xx_wr_32(ha, QLA82XX_PEG_HALT_STATUS1, 0);
	qla82xx_wr_32(ha, QLA82XX_PEG_HALT_STATUS2, 0);


	if (qla82xx_load_fw(ha) != QLA_SUCCESS) {
		qla_printk(KERN_ERR, ha,
		    "%s: Error trying to start fw!\n", __func__);
		return QLA_FUNCTION_FAILED;
	}

	/* Handshake with the card before we register the devices. */
	if (qla82xx_check_cmdpeg_state(ha) != QLA_SUCCESS) {
		qla_printk(KERN_ERR, ha,
		    "%s: Error during card handshake!\n", __func__);
		return QLA_FUNCTION_FAILED;
	}

	/* Negotiated Link width */
	pcie_cap = pci_find_capability(ha->pdev, PCI_CAP_ID_EXP);
	pci_read_config_word(ha->pdev, pcie_cap + PCI_EXP_LNKSTA, &lnk);
	ha->link_width = (lnk >> 4) & 0x3f;

	/* Synchronize with Receive peg */
	return qla82xx_check_rcvpeg_state(ha);
}

/*
 * qla82xx_device_bootstrap
 *    Initialize device, set DEV_READY, start fw
 *
 * Note:
 *      IDC lock must be held upon entry
 *
 * Return:
 *    Success : 0
 *    Failed  : 1
 */
static int
qla82xx_device_bootstrap(scsi_qla_host_t *ha)
{
	int rval = QLA_SUCCESS;
	int i, timeout;
	uint32_t old_count, count;
	int need_reset = 0, peg_stuck = 1;

	need_reset = qla82xx_need_reset(ha);


	old_count = qla82xx_rd_32(ha, QLA82XX_PEG_ALIVE_COUNTER);

	for (i = 0; i < 10; i++) {
		timeout = msleep_interruptible(200);
		if (timeout) {
			qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
			    QLA82XX_DEV_FAILED);
			rval = QLA_FUNCTION_FAILED;
			return rval;
		}

		count = qla82xx_rd_32(ha, QLA82XX_PEG_ALIVE_COUNTER);
		if (count != old_count)
			peg_stuck = 0;
	}

	if (need_reset) {
		/* Do rom lock recovery here */
		if (peg_stuck)
			qla82xx_rom_lock_recovery(ha);
		goto dev_initialize;
	} else {
		if (peg_stuck) {
			/* Either we are first or recovery is in progress */
			qla82xx_rom_lock_recovery(ha);
			goto dev_initialize;
		} else
			/* Firmware already running */
			goto dev_ready;
	}

dev_initialize:
	/* set to DEV_INITIALIZING */
	qla_printk(KERN_INFO, ha, "HW State: INITIALIZING\n");
	qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_INITIALIZING);

	/* Driver that sets device state to initializating sets IDC version */
	qla82xx_wr_32(ha, QLA82XX_CRB_DRV_IDC_VERSION, QLA82XX_IDC_VERSION);

	qla82xx_idc_unlock(ha);
	rval = qla82xx_start_firmware(ha);
	qla82xx_idc_lock(ha);

	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_INFO, ha, "HW State: FAILED\n");
		qla82xx_clear_drv_active(ha);
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_FAILED);
		return rval;
	}

dev_ready:
	qla_printk(KERN_INFO, ha, "HW State: READY\n");
	qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_READY);

	return QLA_SUCCESS;
}

static void
qla82xx_dev_failed_handler(scsi_qla_host_t *ha)
{

	/* Disable the board */
	qla_printk(KERN_INFO, ha,
	    "Disabling the board\n");
	/* Set DEV_FAILED flag to disable timer */
	ha->device_flags |= DFLG_DEV_FAILED;

	qla2x00_abort_all_cmds(ha, DID_NO_CONNECT << 16);

	qla2x00_mark_all_devices_lost(ha, 0);

	ha->flags.online = 0;
	ha->flags.init_done = 0;
}

/*
 * qla82xx_need_reset_handler
 *    Code to start reset sequence
 *
 * Note:
 *      IDC lock must be held upon entry
 *
 * Return:
 *    Success : 0
 *    Failed  : 1
 */
static void
qla82xx_need_reset_handler(scsi_qla_host_t *ha)
{
	uint32_t dev_state, drv_state, drv_active, active_mask;
	unsigned long reset_timeout;

	if (ha->flags.online) {
		qla82xx_idc_unlock(ha);
		qla2x00_abort_isp_cleanup(ha);
		ha->isp_ops->get_flash_version(ha, ha->request_ring);
		ha->isp_ops->nvram_config(ha);
		qla82xx_idc_lock(ha);
	}

	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	active_mask = ~(QLA82XX_DRV_ACTIVE << (ha->portnum * 4));
	if (!ha->flags.isp82xx_reset_owner) {
		printk("%s(%ld): reset_acknowledged by 0x%x\n",
		    __func__, ha->host_no, ha->portnum);
		qla82xx_set_rst_ready(ha);
	} else {
		drv_active &= active_mask;
		printk("%s(%ld): active_mask: 0x%08x\n",
		    __func__, ha->host_no, active_mask);
	}
	/* wait for 10 seconds for reset ack from all functions */
	reset_timeout = jiffies + (ha->nx_reset_timeout * HZ);

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);

	qla_printk(KERN_INFO, ha, "[0]: drv_state: 0x%08x, drv_active: 0x%08x, "
	    "dev_state: 0x%08x, active_mask: 0x%08x\n",
	    drv_state, drv_active, dev_state, active_mask);

	while (drv_state != drv_active &&
	    dev_state != QLA82XX_DEV_INITIALIZING) {

		if (time_after_eq(jiffies, reset_timeout)) {
			qla_printk(KERN_INFO, ha, "RESET TIMEOUT!\n");
			break;
		}

		qla82xx_idc_unlock(ha);
		msleep(1000);
		qla82xx_idc_lock(ha);

		drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
		drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
		if (ha->flags.isp82xx_reset_owner)
			drv_active &= active_mask;
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	}

	qla_printk(KERN_INFO, ha, "[1]: drv_state: 0x%08x, drv_active: 0x%08x, "
	    "dev_state: 0x%08x, active_mask: 0x%08x\n",
	    drv_state, drv_active, dev_state, active_mask);

	qla_printk(KERN_INFO, ha, "3:Device state is 0x%x = %s\n", dev_state,
	    dev_state < MAX_STATES ? qdev_state(dev_state) : "Unknown");

	/* Force to DEV_COLD unless someone else is starting a reset */
	if (dev_state != QLA82XX_DEV_INITIALIZING &&
	    dev_state != QLA82XX_DEV_COLD) {
		qla_printk(KERN_INFO, ha, "HW State: COLD/RE-INIT\n");
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_COLD);

		if (ha->flags.isp82xx_reset_owner)
			qla82xx_set_rst_ready(ha);
		if (ql2xmdenable) {
			if (qla82xx_md_collect(ha))
				qla_printk(KERN_WARNING, ha,
				    "%s(%ld): Minidump not collected.\n",
				    __func__, ha->host_no);
		} else {
			qla_printk(KERN_WARNING, ha,
			    "%s(%ld): Minidump disabled.\n",
			    __func__, ha->host_no);
		}
	}
}

/*
 * qla82xx_need_qsnt_handler
 *    Code to start quiescence sequence
 *
 * Note:
 *      IDC lock must be held upon entry
 *
 * Return: void
 */

static void
qla82xx_need_qsnt_handler(scsi_qla_host_t *ha)
{
	uint32_t dev_state, drv_state, drv_active;
	unsigned long reset_timeout;

	if (ha->flags.online) {
		/*Block any further I/O and wait for pending cmnds to complete*/
		qla82xx_quiescent_state_cleanup(ha);
	}

	/* Set the quiescence ready bit */
	qla82xx_set_qsnt_ready(ha);

	/*wait for 30 secs for other functions to ack */
	reset_timeout = jiffies + (30 * HZ);

	drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
	drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
	/* Its 2 that is written when qsnt is acked, moving one bit */
	drv_active = drv_active << 0x01;

	while (drv_state != drv_active) {

		if (time_after_eq(jiffies, reset_timeout)) {
			/* quiescence timeout, other functions didn't ack
			 * changing the state to DEV_READY
			 */
			qla_printk(KERN_INFO, ha,
				"%s: QUIESCENT TIMEOUT\n", QLA2XXX_DRIVER_NAME);
			qla_printk(KERN_INFO, ha,
				"DRV_ACTIVE:%d DRV_STATE:%d\n", drv_active,
							drv_state);
			qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
						QLA82XX_DEV_READY);
			qla_printk(KERN_INFO, ha,
					"HW State: DEV_READY\n");
			qla82xx_idc_unlock(ha);
			qla2x00_perform_loop_resync(ha);
			qla82xx_idc_lock(ha);

			qla82xx_clear_qsnt_ready(ha);
			return;
		}

		qla82xx_idc_unlock(ha);
		msleep(1000);
		qla82xx_idc_lock(ha);

		drv_state = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_STATE);
		drv_active = qla82xx_rd_32(ha, QLA82XX_CRB_DRV_ACTIVE);
		drv_active = drv_active << 0x01;
	}
	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	/* everyone acked so set the state to DEV_QUIESCENCE */
	if (dev_state == QLA82XX_DEV_NEED_QUIESCENT) {
		qla_printk(KERN_INFO, ha, "HW State: DEV_QUIESCENT\n");
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, QLA82XX_DEV_QUIESCENT);
	}
}

/*
 * qla82xx_wait_for_state_change
 *    Wait for device state to change from given current state
 *
 * Note:
 *     IDC lock must not be held upon entry
 *
 * Return:
 *    Changed device state.
 */
uint32_t
qla82xx_wait_for_state_change(scsi_qla_host_t *ha, uint32_t curr_state)
{
	uint32_t dev_state;

	do {
		msleep(1000);
		qla82xx_idc_lock(ha);
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
		qla82xx_idc_unlock(ha);
	} while (dev_state == curr_state);

	return dev_state;
}

int
qla82xx_check_fw_alive(scsi_qla_host_t *ha)
{
	uint32_t fw_heartbeat_counter;
	int status = 0;

	fw_heartbeat_counter = qla82xx_rd_32(ha, QLA82XX_PEG_ALIVE_COUNTER);
	/* all 0xff, assume AER/EEH in progress, ignore */
	if (fw_heartbeat_counter == 0xffffffff)
		return status;
	if (ha->fw_heartbeat_counter == fw_heartbeat_counter) {
		ha->seconds_since_last_heartbeat++;
		/* FW not alive after 2 seconds */
		if (ha->seconds_since_last_heartbeat == 2) {
			ha->seconds_since_last_heartbeat = 0;
			status = 1;
		}
	} else {
		ha->seconds_since_last_heartbeat = 0;
	}

	ha->fw_heartbeat_counter = fw_heartbeat_counter;
	return status;
}

int
qla82xx_check_md_needed(scsi_qla_host_t *ha)
{
	uint16_t fw_major_version, fw_minor_version, fw_subminor_version;
	int rval = QLA_SUCCESS;

	fw_major_version = ha->fw_major_version;
	fw_minor_version = ha->fw_minor_version;
	fw_subminor_version = ha->fw_subminor_version;

	rval = qla2x00_get_fw_version(ha, &ha->fw_major_version,
	    &ha->fw_minor_version, &ha->fw_subminor_version,
	    &ha->fw_attributes, &ha->fw_memory_size,
	    ha->mpi_version, &ha->mpi_capabilities,
	    ha->phy_version);


	if (rval != QLA_SUCCESS)
		return rval;

	if (ql2xmdenable) {
		if (!ha->fw_dumped) {
			if (fw_major_version != ha->fw_major_version ||
			    fw_minor_version != ha->fw_minor_version ||
			    fw_subminor_version != ha->fw_subminor_version) {
				qla_printk(KERN_INFO, ha,
				    "scsi(%ld): Firmware version differs "
				    "Previous version: %d:%d:%d - "
				    "New version: %d:%d:%d\n",
				    ha->host_no, fw_major_version,
				    fw_minor_version, fw_subminor_version,
				    ha->fw_major_version,
				    ha->fw_minor_version,
				    ha->fw_subminor_version);
				/* Release MiniDump resources */
				qla82xx_md_free(ha);
				/* Allocate MiniDump resources */
				qla82xx_md_prep(ha);
			}
		} else
			qla_printk(KERN_INFO, ha,
			    "scsi(%ld): Firmware dump available to "
			    "retrive\n", ha->host_no);
	}
	return rval;
}

/*
 * qla82xx_device_state_handler
 *	Main state handler
 *
 * Note:
 *      IDC lock must be held upon entry
 *
 * Return:
 *    Success : 0
 *    Failed  : 1
 */
int
qla82xx_device_state_handler(scsi_qla_host_t *ha)
{
	uint32_t dev_state;
	uint32_t old_dev_state;
	int rval = QLA_SUCCESS;
	unsigned long dev_init_timeout;
	int loopcount = 0;

	qla82xx_idc_lock(ha);

	if (!ha->flags.init_done)
		qla82xx_set_drv_active(ha);

	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	old_dev_state = dev_state;
	qla_printk(KERN_INFO, ha, "1:Device state is 0x%x = %s\n", dev_state,
		dev_state < MAX_STATES ? qdev_state(dev_state) : "Unknown");

	if (ql2xsetdevstate && dev_state != QLA82XX_DEV_NEED_RESET )
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE, ql2xsetdevstate);
		

	/* wait for 30 seconds for device to go ready */
	dev_init_timeout = jiffies + (ha->nx_dev_init_timeout * HZ);

	while (1) {

		if (time_after_eq(jiffies, dev_init_timeout)) {
			qla_printk(KERN_ERR, ha, "Device init failed!\n");
			rval = QLA_FUNCTION_FAILED;
			break;
		}
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
		if (old_dev_state != dev_state) {
			loopcount = 0;
			old_dev_state = dev_state;
		}
		if (loopcount < 5) {
			qla_printk(KERN_INFO, ha,
				"2:Device state is 0x%x = %s\n", dev_state,
				dev_state < MAX_STATES ? qdev_state(dev_state) :
				"Unknown");
		}

		switch (dev_state) {
		case QLA82XX_DEV_READY:
			ha->flags.isp82xx_reset_owner = 0;
			printk("%s(%ld): reset_owner reset by 0x%x\n",
			    __func__, ha->host_no, ha->portnum);
			goto exit;
		case QLA82XX_DEV_COLD:
			rval = qla82xx_device_bootstrap(ha);
			break;
		case QLA82XX_DEV_INITIALIZING:
			qla82xx_idc_unlock(ha);
			msleep(1000);
			qla82xx_idc_lock(ha);
			break;
		case QLA82XX_DEV_NEED_RESET:
			if (!ql2xdontresethba)
				qla82xx_need_reset_handler(ha);
			else {
				qla82xx_idc_unlock(ha);
				msleep(1000);
				qla82xx_idc_lock(ha);
			}
			dev_init_timeout = jiffies +
			    (ha->nx_dev_init_timeout * HZ);
			break;
		case QLA82XX_DEV_NEED_QUIESCENT:
			qla82xx_need_qsnt_handler(ha);
			/* Reset timeout value after quiescence handler */
			dev_init_timeout = jiffies + (ha->nx_dev_init_timeout\
							 * HZ);
			break;
		case QLA82XX_DEV_QUIESCENT:
			/* Owner will exit and other will wait for the state
			 * to get changed
			 */
			if (ha->flags.quiesce_owner)
				goto exit;

			qla82xx_idc_unlock(ha);
			msleep(1000);
			qla82xx_idc_lock(ha);

			/* Reset timeout value after quiescence handler */
			dev_init_timeout = jiffies + (ha->nx_dev_init_timeout\
							 * HZ);
			break;
		case QLA82XX_DEV_FAILED:
			qla82xx_dev_failed_handler(ha);
			rval = QLA_FUNCTION_FAILED;
			goto exit;
		default:
			qla82xx_dev_failed_handler(ha);
			rval = QLA_FUNCTION_FAILED;
			goto exit;
		}
	}
exit:
	qla82xx_idc_unlock(ha);
	return rval;
}

static int qla82xx_check_temp(scsi_qla_host_t *ha)
{
	uint32_t temp, temp_state, temp_val;

	temp = qla82xx_rd_32(ha, CRB_TEMP_STATE);
	temp_state = qla82xx_get_temp_state(temp);
	temp_val = qla82xx_get_temp_val(temp);

	if (temp_state == QLA82XX_TEMP_PANIC) {
		qla_printk(KERN_WARNING, ha,
		    "Device temperature %d degrees C exceeds maximum allowed. "
		    "Hardware has been shut down.\n", temp_val);
		return 1;
	} else if (temp_state == QLA82XX_TEMP_WARN) {
		qla_printk(KERN_WARNING, ha,
		    "Device temperature %d degress C exceeds operating range. "
		    "Immediate action needed.\n", temp_val);
	}
	return 0;
}

void qla82xx_clear_pending_mbx(scsi_qla_host_t *ha)
{
	if (ha->flags.mbox_busy) {
		ha->flags.mbox_int = 1;
		ha->flags.mbox_busy = 0;
		qla_printk(KERN_WARNING, ha,
		    "scsi(%ld): Doing premature completion of mbx command.\n",
		    ha->host_no);
		if(test_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags))
			complete(&ha->mbx_intr_comp);
	}
}

void qla82xx_watchdog(scsi_qla_host_t *ha)
{
	uint32_t dev_state, halt_status;

	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);

	/* don't poll if reset is going on */
	if (!ha->flags.isp82xx_reset_hdlr_active) {
		dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
		if (qla82xx_check_temp(ha)) {
			set_bit(ISP_UNRECOVERABLE, &ha->dpc_flags);
			ha->flags.isp82xx_fw_hung = 1;
			qla82xx_clear_pending_mbx(ha);
		} else if (dev_state == QLA82XX_DEV_NEED_RESET &&
			!test_bit(ISP_ABORT_NEEDED, &ha->dpc_flags)) {
			qla_printk(KERN_WARNING, ha,
			     "scsi(%ld) %s: Adapter reset needed!\n",
			      ha->host_no, __func__);
			set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
		} else if (dev_state == QLA82XX_DEV_NEED_QUIESCENT &&
			!test_bit(ISP_QUIESCE_NEEDED, &ha->dpc_flags)) {
			DEBUG(qla_printk(KERN_INFO, ha,
				"scsi(%ld) %s - detected quiescence needed\n",
				ha->host_no, __func__));
			set_bit(ISP_QUIESCE_NEEDED, &ha->dpc_flags);
		} else {
			if (qla82xx_check_fw_alive(ha)) {
				DEBUG2(qla_printk(KERN_WARNING, ha,
				    "Disabling pause transmit on port 0 & "
				    "1.\n"));
				qla82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x98,
				    CRB_NIU_XG_PAUSE_CTL_P0|CRB_NIU_XG_PAUSE_CTL_P1);
				halt_status = qla82xx_rd_32(ha,
					QLA82XX_PEG_HALT_STATUS1);
				qla_printk(KERN_INFO, ha,
				    "scsi(%ld): %s, Dumping hw/fw registers:\n"
				    "PEG_HALT_STATUS1: 0x%x, "
				    "PEG_HALT_STATUS2: 0x%x, "
				    "PEG_NET_0_PC: 0x%x, PEG_NET_1_PC: 0x%x,\n"
				    "PEG_NET_2_PC: 0x%x, PEG_NET_3_PC: 0x%x,\n"
				    "PEG_NET_4_PC: 0x%x\n",
				    ha->host_no, __func__, halt_status,
				    qla82xx_rd_32(ha, QLA82XX_PEG_HALT_STATUS2),
				    qla82xx_rd_32(ha,
					QLA82XX_CRB_PEG_NET_0 + 0x3c),
				    qla82xx_rd_32(ha,
					QLA82XX_CRB_PEG_NET_1 + 0x3c),
				    qla82xx_rd_32(ha,
					QLA82XX_CRB_PEG_NET_2 + 0x3c),
				    qla82xx_rd_32(ha,
					QLA82XX_CRB_PEG_NET_3 + 0x3c),
				    qla82xx_rd_32(ha,
					QLA82XX_CRB_PEG_NET_4 + 0x3c));
				if(((halt_status & 0x1fffff00) >> 8) == 0x67)
					qla_printk(KERN_ERR, ha,
					    "scsi(%ld): Firmware aborted with "
					    "error code 0x00006700. Device is "
					    "being reset.\n", ha->host_no);
				if (halt_status & HALT_STATUS_UNRECOVERABLE) {
					set_bit(ISP_UNRECOVERABLE,
						&ha->dpc_flags);
				} else {
					qla_printk(KERN_INFO, ha,
						"scsi(%ld): %s - detect abort "
						"needed\n", ha->host_no,
						__func__);
					set_bit(ISP_ABORT_NEEDED,
						&ha->dpc_flags);
				}
				ha->flags.isp82xx_fw_hung = 1;
				qla_printk(KERN_WARNING, ha,
				    "scsi(%ld): Firmware hung\n", ha->host_no);
				qla82xx_clear_pending_mbx(ha);
			}
		}
	}
}

int qla82xx_load_risc(scsi_qla_host_t *ha, uint32_t *srisc_addr)
{

	int rval;

	rval = qla82xx_device_state_handler(ha);

	return rval;
}

void
qla82xx_set_reset_owner(scsi_qla_host_t *ha)
{
	uint32_t dev_state;

	dev_state = qla82xx_rd_32(ha, QLA82XX_CRB_DEV_STATE);
	if (dev_state == QLA82XX_DEV_READY) {
		qla_printk(KERN_INFO, ha, "HW State: NEED RESET\n");
		qla82xx_wr_32(ha, QLA82XX_CRB_DEV_STATE,
			QLA82XX_DEV_NEED_RESET);
		ha->flags.isp82xx_reset_owner = 1;
		printk("%s(%ld): reset_owner is 0x%x\n",
		    __func__, ha->host_no, ha->portnum);
	} else
		qla_printk(KERN_INFO, ha, "HW State: %s\n",
			dev_state < MAX_STATES ?
			qdev_state(dev_state) : "Unknown");
}

/*
*  qla82xx_abort_isp
*      Resets ISP and aborts all outstanding commands.
*
* Input:
*      ha           = adapter block pointer.
*
* Returns:
*      0 = success
*/
int
qla82xx_abort_isp(scsi_qla_host_t *ha)
{
	int rval;

	if (ha->device_flags & DFLG_DEV_FAILED) {
		DEBUG(printk("%s(%ld): Device in failed state. "
			     "Exiting.\n", __func__, ha->host_no));
		return QLA_SUCCESS;
	}
	ha->flags.isp82xx_reset_hdlr_active = 1;

	qla82xx_idc_lock(ha);
	qla82xx_set_reset_owner(ha);
	qla82xx_idc_unlock(ha);

	rval = qla82xx_device_state_handler(ha);

	qla82xx_idc_lock(ha);
	qla82xx_clear_rst_ready(ha);
	qla82xx_idc_unlock(ha);

	if (rval == QLA_SUCCESS) {
		ha->flags.isp82xx_fw_hung = 0;
		ha->flags.isp82xx_reset_hdlr_active = 0;
		rval = qla82xx_restart_isp(ha);
	}

	if (rval) {
		ha->flags.online = 1;
		if (test_bit(ISP_ABORT_RETRY, &ha->dpc_flags)) {
			if (ha->isp_abort_cnt == 0) {
				qla_printk(KERN_WARNING, ha,
				    "ISP error recovery failed - "
				    "board disabled\n");
				/*
				 * The next call disables the board
				 * completely.
				 */
				ha->isp_ops->reset_adapter(ha);
				clear_bit(ISP_ABORT_RETRY,
				    &ha->dpc_flags);
				rval = QLA_SUCCESS;
			} else { /* schedule another ISP abort */
				ha->isp_abort_cnt--;
				DEBUG(qla_printk(KERN_INFO, ha,
				    "qla%ld: ISP abort - retry remaining %d\n",
				    ha->host_no, ha->isp_abort_cnt));
				rval = QLA_FUNCTION_FAILED;
			}
		} else {
			ha->isp_abort_cnt = MAX_RETRIES_OF_ISP_ABORT;
			DEBUG(qla_printk(KERN_INFO, ha,
			    "(%ld): ISP error recovery - retrying (%d) "
			    "more times\n", ha->host_no, ha->isp_abort_cnt));
			set_bit(ISP_ABORT_RETRY, &ha->dpc_flags);
			rval = QLA_FUNCTION_FAILED;
		}
	}

	return rval;
}

void
qla82xx_chip_reset_cleanup(scsi_qla_host_t *ha)
{
	int i;
	unsigned long flags;

	/* Check if 82XX firmware is alive or not
	 * We may have arrived here from NEED_RESET
	 * detection only
	 */
	if (!ha->flags.isp82xx_fw_hung) {
		for(i = 0; i < 2; i++) {
			msleep(1000);
			if (qla82xx_check_fw_alive(ha)) {
				ha->flags.isp82xx_fw_hung = 1;
				qla82xx_clear_pending_mbx(ha);
				break;
			}
		}
	}

	/* Abort all the commands gracefully if fw NOT hung */
	if (!ha->flags.isp82xx_fw_hung) {
		int cnt;
		srb_t *sp;

		spin_lock_irqsave(&ha->hardware_lock, flags);
		for (cnt = 1; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
			sp = ha->outstanding_cmds[cnt];
			if (sp) {
				if (!sp->ctx ||
				    (sp->flags & SRB_FCP_CMND_DMA_VALID)) {
					spin_unlock_irqrestore(
					  &ha->hardware_lock, flags);
					if (ha->isp_ops->abort_command(ha, sp)) {
						qla_printk(KERN_INFO, ha,
						  "scsi(%ld):mbx abort command "
						  "failed in %s\n", ha->host_no,
						  __func__);
					} else {
						qla_printk(KERN_INFO, ha,
						  "scsi(%ld):mbx abort command "
						  "success in %s\n",
						  ha->host_no, __func__);
					}
					spin_lock_irqsave(&ha->hardware_lock,
								flags);
				}
			}
		}
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/* Wait for pending cmds (physical and virtual) to complete */
		if (!qla2x00_eh_wait_for_pending_commands(ha) == QLA_SUCCESS) {
			DEBUG2(qla_printk(KERN_INFO, ha,
				"Done wait for pending commands\n"));
		}
	}
}

int
qla82xx_beacon_on(struct scsi_qla_host *ha)
{

	int rval;
	qla82xx_idc_lock(ha);
	rval = qla82xx_mbx_beacon_ctl(ha, 1);

	if (rval) {
		qla_printk(KERN_WARNING, ha,
		    "scsi(%ld): mbx set led config failed in %s\n",
		    ha->host_no, __func__);
		goto exit;
	}
	ha->beacon_blink_led = 1;
exit:
	qla82xx_idc_unlock(ha);
	return rval;
}

int
qla82xx_beacon_off(struct scsi_qla_host *ha)
{

	int rval;
	qla82xx_idc_lock(ha);
	rval = qla82xx_mbx_beacon_ctl(ha, 0);

	if (rval) {
		qla_printk(KERN_WARNING, ha,
		    "scsi(%ld): mbx set led config failed in %s\n",
		    ha->host_no, __func__);
		goto exit;
	}
	ha->beacon_blink_led = 0;
exit:
	qla82xx_idc_unlock(ha);
	return rval;
}

/* Minidump related functions */
static int
qla82xx_minidump_process_control(scsi_qla_host_t *ha,
	qla82xx_md_entry_hdr_t *entry_hdr, uint32_t **d_ptr)
{
	struct qla82xx_md_entry_crb *crb_entry;
	uint32_t read_value, opcode, poll_time;
	uint32_t addr, index, crb_addr;
	unsigned long wtime;
	uint32_t rval = QLA_SUCCESS;
	struct qla82xx_md_template_hdr *tmplt_hdr;
	int i;

	tmplt_hdr = (struct qla82xx_md_template_hdr *)ha->md_tmplt_hdr;
	crb_entry = (struct qla82xx_md_entry_crb *)entry_hdr;
	crb_addr = crb_entry->addr;

	for (i = 0; i < crb_entry->op_count; i++) {
		opcode = crb_entry->crb_ctrl.opcode;
		if (opcode & QLA82XX_DBG_OPCODE_WR) {
			qla82xx_md_rw_32(ha, crb_addr,
			    crb_entry->value_1, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_WR;
		}

		if (opcode & QLA82XX_DBG_OPCODE_RW) {
			read_value = qla82xx_md_rw_32(ha, crb_addr, 0, 0);
			qla82xx_md_rw_32(ha, crb_addr, read_value, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_RW;
		}

		if (opcode & QLA82XX_DBG_OPCODE_AND) {
			read_value = qla82xx_md_rw_32(ha, crb_addr, 0, 0);
			read_value &= crb_entry->value_2;
			opcode &= ~QLA82XX_DBG_OPCODE_AND;
			if (opcode & QLA82XX_DBG_OPCODE_OR) {
				read_value |= crb_entry->value_3;
				opcode &= ~QLA82XX_DBG_OPCODE_OR;
			}
			qla82xx_md_rw_32(ha, crb_addr, read_value, 1);
		}

		if (opcode & QLA82XX_DBG_OPCODE_OR) {
			read_value = qla82xx_md_rw_32(ha, crb_addr, 0, 0);
			read_value |= crb_entry->value_3;
			qla82xx_md_rw_32(ha, crb_addr, read_value, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_OR;
		}

		if (opcode & QLA82XX_DBG_OPCODE_POLL) {
			poll_time = crb_entry->crb_strd.poll_timeout;
			wtime = jiffies + poll_time;
			read_value = qla82xx_md_rw_32(ha, crb_addr, 0, 0);

			do {
				if ((read_value & crb_entry->value_2)
				    == crb_entry->value_1)
					break;
				else if (time_after_eq(jiffies, wtime)) {
					/* capturing dump failed */
					rval = QLA_FUNCTION_FAILED;
					break;
				} else
					read_value = qla82xx_md_rw_32(ha,
					    crb_addr, 0, 0);
			} while (1);
			opcode &= ~QLA82XX_DBG_OPCODE_POLL;
		}

		if (opcode & QLA82XX_DBG_OPCODE_RDSTATE) {
			if (crb_entry->crb_strd.state_index_a) {
				index = crb_entry->crb_strd.state_index_a;
				addr = tmplt_hdr->saved_state_array[index];
			} else
				addr = crb_addr;

			read_value = qla82xx_md_rw_32(ha, addr, 0, 0);
			index = crb_entry->crb_ctrl.state_index_v;
			tmplt_hdr->saved_state_array[index] = read_value;
			opcode &= ~QLA82XX_DBG_OPCODE_RDSTATE;
		}

		if (opcode & QLA82XX_DBG_OPCODE_WRSTATE) {
			if (crb_entry->crb_strd.state_index_a) {
				index = crb_entry->crb_strd.state_index_a;
				addr = tmplt_hdr->saved_state_array[index];
			} else
				addr = crb_addr;

			if (crb_entry->crb_ctrl.state_index_v) {
				index = crb_entry->crb_ctrl.state_index_v;
				read_value =
				    tmplt_hdr->saved_state_array[index];
			} else
				read_value = crb_entry->value_1;

			qla82xx_md_rw_32(ha, addr, read_value, 1);
			opcode &= ~QLA82XX_DBG_OPCODE_WRSTATE;
		}

		if (opcode & QLA82XX_DBG_OPCODE_MDSTATE) {
			index = crb_entry->crb_ctrl.state_index_v;
			read_value = tmplt_hdr->saved_state_array[index];
			read_value <<= crb_entry->crb_ctrl.shl;
			read_value >>= crb_entry->crb_ctrl.shr;
			if (crb_entry->value_2)
				read_value &= crb_entry->value_2;
			read_value |= crb_entry->value_3;
			read_value += crb_entry->value_1;
			tmplt_hdr->saved_state_array[index] = read_value;
			opcode &= ~QLA82XX_DBG_OPCODE_MDSTATE;
		}
		crb_addr += crb_entry->crb_strd.addr_stride;
	}
	return rval;
}

static void
qla82xx_minidump_process_rdocm(scsi_qla_host_t *ha,
	qla82xx_md_entry_hdr_t *entry_hdr, uint32_t **d_ptr)
{
	struct qla82xx_md_entry_rdocm *ocm_hdr;
	uint32_t r_addr, r_stride, loop_cnt, i, r_value;
	uint32_t *data_ptr = *d_ptr;

	ocm_hdr = (struct qla82xx_md_entry_rdocm *)entry_hdr;
	r_addr = ocm_hdr->read_addr;
	r_stride = ocm_hdr->read_addr_stride;
	loop_cnt = ocm_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		r_value = RD_REG_DWORD((void *)(r_addr + ha->nx_pcibase));
		*data_ptr++ = cpu_to_le32(r_value);
		r_addr += r_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla82xx_minidump_process_rdmux(scsi_qla_host_t *ha,
	qla82xx_md_entry_hdr_t *entry_hdr, uint32_t **d_ptr)
{
	uint32_t r_addr, s_stride, s_addr, s_value, loop_cnt, i, r_value;
	struct qla82xx_md_entry_mux *mux_hdr;
	uint32_t *data_ptr = *d_ptr;

	mux_hdr = (struct qla82xx_md_entry_mux *)entry_hdr;
	r_addr = mux_hdr->read_addr;
	s_addr = mux_hdr->select_addr;
	s_stride = mux_hdr->select_value_stride;
	s_value = mux_hdr->select_value;
	loop_cnt = mux_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		qla82xx_md_rw_32(ha, s_addr, s_value, 1);
		r_value = qla82xx_md_rw_32(ha, r_addr, 0, 0);
		*data_ptr++ = cpu_to_le32(s_value);
		*data_ptr++ = cpu_to_le32(r_value);
		s_value += s_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla82xx_minidump_process_rdcrb(scsi_qla_host_t *ha,
	qla82xx_md_entry_hdr_t *entry_hdr, uint32_t **d_ptr)
{
	uint32_t r_addr, r_stride, loop_cnt, i, r_value;
	struct qla82xx_md_entry_crb *crb_hdr;
	uint32_t *data_ptr = *d_ptr;

	crb_hdr = (struct qla82xx_md_entry_crb *)entry_hdr;
	r_addr = crb_hdr->addr;
	r_stride = crb_hdr->crb_strd.addr_stride;
	loop_cnt = crb_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		r_value = qla82xx_md_rw_32(ha, r_addr, 0, 0);
		*data_ptr++ = cpu_to_le32(r_addr);
		*data_ptr++ = cpu_to_le32(r_value);
		r_addr += r_stride;
	}
	*d_ptr = data_ptr;
}

static int
qla82xx_minidump_process_l2tag(scsi_qla_host_t *ha,
	qla82xx_md_entry_hdr_t *entry_hdr, uint32_t **d_ptr)
{
	uint32_t addr, r_addr, c_addr, t_r_addr;
	uint32_t i, k, loop_count, t_value, r_cnt, r_value;
	unsigned long p_wait, w_time, p_mask;
	uint32_t c_value_w, c_value_r;
	struct qla82xx_md_entry_cache *cache_hdr;
	int rval = QLA_FUNCTION_FAILED;
	uint32_t *data_ptr = *d_ptr;

	cache_hdr = (struct qla82xx_md_entry_cache *)entry_hdr;
	loop_count = cache_hdr->op_count;
	r_addr = cache_hdr->read_addr;
	c_addr = cache_hdr->control_addr;
	c_value_w = cache_hdr->cache_ctrl.write_value;

	t_r_addr = cache_hdr->tag_reg_addr;
	t_value = cache_hdr->addr_ctrl.init_tag_value;
	r_cnt = cache_hdr->read_ctrl.read_addr_cnt;
	p_wait = cache_hdr->cache_ctrl.poll_wait;
	p_mask = cache_hdr->cache_ctrl.poll_mask;

	for (i = 0; i < loop_count; i++) {
		qla82xx_md_rw_32(ha, t_r_addr, t_value, 1);
		if (c_value_w)
			qla82xx_md_rw_32(ha, c_addr, c_value_w, 1);

		if (p_mask) {
			w_time = jiffies + p_wait;
			do {
				c_value_r = qla82xx_md_rw_32(ha, c_addr, 0, 0);
				if ((c_value_r & p_mask) == 0)
					break;
				else if (time_after_eq(jiffies, w_time)) {
					/* capturing dump failed */
					DEBUG11(qla_printk(KERN_WARNING, ha,
					    "%s(%ld): c_value_r: 0x%x, poll_mask: 0x%lx, w_time: 0x%lx\n",
					    __func__, ha->host_no, c_value_r,
					    p_mask, w_time));
					return (rval);
				}
			} while (1);
		}

		addr = r_addr;
		for (k = 0; k < r_cnt; k++) {
			r_value = qla82xx_md_rw_32(ha, addr, 0, 0);
			*data_ptr++ = cpu_to_le32(r_value);
			addr += cache_hdr->read_ctrl.read_addr_stride;
		}
		t_value += cache_hdr->addr_ctrl.tag_value_stride;
	}
	*d_ptr = data_ptr;
	return QLA_SUCCESS;
}

static void
qla82xx_minidump_process_l1cache(scsi_qla_host_t *ha,
	qla82xx_md_entry_hdr_t *entry_hdr, uint32_t **d_ptr)
{
	uint32_t addr, r_addr, c_addr, t_r_addr;
	uint32_t i, k, loop_count, t_value, r_cnt, r_value;
	uint32_t c_value_w;
	struct qla82xx_md_entry_cache *cache_hdr;
	uint32_t *data_ptr = *d_ptr;

	cache_hdr = (struct qla82xx_md_entry_cache *)entry_hdr;
	loop_count = cache_hdr->op_count;
	r_addr = cache_hdr->read_addr;
	c_addr = cache_hdr->control_addr;
	c_value_w = cache_hdr->cache_ctrl.write_value;

	t_r_addr = cache_hdr->tag_reg_addr;
	t_value = cache_hdr->addr_ctrl.init_tag_value;
	r_cnt = cache_hdr->read_ctrl.read_addr_cnt;

	for (i = 0; i < loop_count; i++) {
		qla82xx_md_rw_32(ha, t_r_addr, t_value, 1);
		qla82xx_md_rw_32(ha, c_addr, c_value_w, 1);
		addr = r_addr;
		for (k = 0; k < r_cnt; k++) {
			r_value = qla82xx_md_rw_32(ha, addr, 0, 0);
			*data_ptr++ = cpu_to_le32(r_value);
			addr += cache_hdr->read_ctrl.read_addr_stride;
		}
		t_value += cache_hdr->addr_ctrl.tag_value_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla82xx_minidump_process_queue(scsi_qla_host_t *ha,
    qla82xx_md_entry_hdr_t *entry_hdr, uint32_t **d_ptr)
{
	uint32_t s_addr, r_addr;
	uint32_t r_stride, r_value, r_cnt, qid = 0;
	uint32_t i, k, loop_cnt;
	struct qla82xx_md_entry_queue *q_hdr;
	uint32_t *data_ptr = *d_ptr;

	q_hdr = (struct qla82xx_md_entry_queue *)entry_hdr;
	s_addr = q_hdr->select_addr;
	r_cnt = q_hdr->rd_strd.read_addr_cnt;
	r_stride = q_hdr->rd_strd.read_addr_stride;
	loop_cnt = q_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		qla82xx_md_rw_32(ha, s_addr, qid, 1);
		r_addr = q_hdr->read_addr;
		for (k = 0; k < r_cnt; k++) {
			r_value = qla82xx_md_rw_32(ha, r_addr, 0, 0);
			*data_ptr++ = cpu_to_le32(r_value);
			r_addr += r_stride;
		}
		qid += q_hdr->q_strd.queue_id_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla82xx_minidump_process_rdrom(scsi_qla_host_t *ha,
	qla82xx_md_entry_hdr_t *entry_hdr, uint32_t **d_ptr)
{
	uint32_t r_addr, r_value;
	uint32_t i, loop_cnt;
	struct qla82xx_md_entry_rdrom *rom_hdr;
	uint32_t *data_ptr = *d_ptr;

	rom_hdr = (struct qla82xx_md_entry_rdrom *)entry_hdr;
	r_addr = rom_hdr->read_addr;
	loop_cnt = rom_hdr->read_data_size/sizeof(uint32_t);

	for (i = 0; i < loop_cnt; i++) {
		qla82xx_md_rw_32(ha, MD_DIRECT_ROM_WINDOW,
		    (r_addr & 0xFFFF0000), 1);
		r_value = qla82xx_md_rw_32(ha,
		    MD_DIRECT_ROM_READ_BASE +
		    (r_addr & 0x0000FFFF), 0, 0);
		*data_ptr++ = cpu_to_le32(r_value);
		r_addr += sizeof(uint32_t);
	}
	*d_ptr = data_ptr;
}

static int
qla82xx_minidump_process_rdmem(scsi_qla_host_t *ha,
	qla82xx_md_entry_hdr_t *entry_hdr, uint32_t **d_ptr)
{
	uint32_t r_addr, r_value, r_data;
	uint32_t i, j, loop_cnt;
	struct qla82xx_md_entry_rdmem *m_hdr;
	unsigned long flags;
	int rval = QLA_FUNCTION_FAILED;
	uint32_t *data_ptr = *d_ptr;

	m_hdr = (struct qla82xx_md_entry_rdmem *)entry_hdr;
	r_addr = m_hdr->read_addr;
	loop_cnt = m_hdr->read_data_size/16;

	if (r_addr & 0xf) {
		qla_printk(KERN_WARNING, ha,
		    "%s(%ld): Read addr 0x%x not 16 bytes alligned\n",
		    __func__, ha->host_no, r_addr);
		return (rval);
	}

	if (m_hdr->read_data_size % 16) {
		qla_printk(KERN_WARNING, ha,
		    "%s(%ld): Read data[0x%x] not multiple of 16 bytes\n",
		    __func__, ha->host_no, m_hdr->read_data_size);
		return (rval);
	}

	DEBUG11(qla_printk(KERN_WARNING, ha,
	    "[%s]: rdmem_addr: 0x%x, read_data_size: 0x%x, loop_cnt: 0x%x\n",
	    __func__, r_addr, m_hdr->read_data_size, loop_cnt));

	write_lock_irqsave(&ha->hw_lock, flags);
	for (i = 0; i < loop_cnt; i++) {
		qla82xx_md_rw_32(ha, MD_MIU_TEST_AGT_ADDR_LO, r_addr, 1);
		r_value = 0;
		qla82xx_md_rw_32(ha, MD_MIU_TEST_AGT_ADDR_HI, r_value, 1);
		r_value = MIU_TA_CTL_ENABLE;
		qla82xx_md_rw_32(ha, MD_MIU_TEST_AGT_CTRL, r_value, 1);
		r_value = MIU_TA_CTL_START | MIU_TA_CTL_ENABLE;
		qla82xx_md_rw_32(ha, MD_MIU_TEST_AGT_CTRL, r_value, 1);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			r_value = qla82xx_md_rw_32(ha,
			    MD_MIU_TEST_AGT_CTRL, 0, 0);
			if ((r_value & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			if (printk_ratelimit())
				dev_err(&ha->pdev->dev,
				    "failed to read through agent\n");
			write_unlock_irqrestore(&ha->hw_lock, flags);
			return rval;
		}

		for (j = 0; j < 4; j++) {
			r_data = qla82xx_md_rw_32(ha,
			    MD_MIU_TEST_AGT_RDDATA[j], 0, 0);
			*data_ptr++ = cpu_to_le32(r_data);
		}
		r_addr += 16;
	}
	write_unlock_irqrestore(&ha->hw_lock, flags);
	*d_ptr = data_ptr;
	return QLA_SUCCESS;
}

static int
qla82xx_validate_template_chksum(scsi_qla_host_t *ha)
{
	uint64_t chksum = 0;
	uint32_t *d_ptr = (uint32_t *)ha->md_tmplt_hdr;
	int count = ha->md_template_size/sizeof(uint32_t);

	while (count-- > 0)
		chksum += *d_ptr++;
	while (chksum >> 32)
		chksum = (chksum & 0xFFFFFFFF) + (chksum >> 32);
	return ~chksum;
}

static void
qla82xx_mark_entry_skipped(scsi_qla_host_t *ha,
	qla82xx_md_entry_hdr_t *entry_hdr, int index)
{
	entry_hdr->d_ctrl.driver_flags |= QLA82XX_DBG_SKIPPED_FLAG;
	qla_printk(KERN_INFO, ha,
	    "scsi(%ld): Skipping entry[%d]: "
	    "ETYPE[0x%x]-ELEVEL[0x%x]\n",
	ha->host_no, index,
	entry_hdr->entry_type,
	entry_hdr->d_ctrl.entry_capture_mask);
}

int
qla82xx_md_collect(scsi_qla_host_t *ha)
{
	int no_entry_hdr = 0;
	qla82xx_md_entry_hdr_t *entry_hdr;
	struct qla82xx_md_template_hdr *tmplt_hdr;
	uint32_t *data_ptr;
	uint32_t total_data_size = 0, f_capture_mask, data_collected = 0;
	int i = 0, rval = QLA_FUNCTION_FAILED;

	tmplt_hdr = (struct qla82xx_md_template_hdr *)ha->md_tmplt_hdr;
	data_ptr = (uint32_t *)ha->md_dump;

	if (ha->fw_dumped) {
		qla_printk(KERN_INFO, ha,
		    "%s(%ld): Firmware has been previously dumped (%p) "
		    "-- ignoring request.\n", __func__, ha->host_no, ha->fw_dump);
		goto md_failed;
	}

	ha->fw_dumped = 0;

	if (!ha->md_tmplt_hdr || !ha->md_dump) {
		qla_printk(KERN_WARNING, ha,
		    "%s(%ld): Memory not allocated for minidump capture\n",
		    __func__, ha->host_no);
		goto md_failed;
	}

	if (ha->flags.isp82xx_no_md_cap) {
		qla_printk(KERN_WARNING, ha,
		    "%s(%ld): Forced reset from application, ignore minidump "
		    "capture.\n", __func__, ha->host_no);
		ha->flags.isp82xx_no_md_cap = 0;
		goto md_failed;
	}

	if (qla82xx_validate_template_chksum(ha)) {
		qla_printk(KERN_WARNING, ha,
		    "%s(%ld): Template checksum validation error\n",
		    __func__, ha->host_no);
		goto md_failed;
	}

	no_entry_hdr = tmplt_hdr->num_of_entries;
	qla_printk(KERN_WARNING, ha,
	    "%s(%ld): no of entry headers in Template: 0x%x\n",
	    __func__, ha->host_no, no_entry_hdr);

	DEBUG11(qla_printk(KERN_WARNING, ha,
	    "[%s]: Capture Mask obtained: 0x%x\n",
	    __func__, tmplt_hdr->capture_debug_level));

	f_capture_mask = tmplt_hdr->capture_debug_level & 0xFF;

	/* Validate whether required debug level is set */
	if ((f_capture_mask & 0x3) != 0x3) {
		qla_printk(KERN_WARNING, ha,
		    "%s(%ld): Minimum required capture mask[0x%x] level not set\n",
		    __func__, ha->host_no, f_capture_mask);
		goto md_failed;
	}
	tmplt_hdr->driver_capture_mask = ql2xmdcapmask;

	tmplt_hdr->driver_info[0] = ha->host_no;
	tmplt_hdr->driver_info[1] = (QLA_DRIVER_MAJOR_VER << 24) |
	    (QLA_DRIVER_MINOR_VER << 16) | (QLA_DRIVER_PATCH_VER << 8) |
	    QLA_DRIVER_BETA_VER;

	total_data_size = ha->md_dump_size;

	qla_printk(KERN_INFO, ha,
	    "%s(%ld): Total minidump data_size 0x%x to be captured\n",
		__func__, ha->host_no, total_data_size);

	/* Check whether template obtained is valid */
	if (tmplt_hdr->entry_type != QLA82XX_TLHDR) {
		qla_printk(KERN_WARNING, ha,
		    "%s(%ld): Bad template header entry type: 0x%x obtained\n",
		    __func__, ha->host_no, tmplt_hdr->entry_type);
		goto md_failed;
	}

	entry_hdr = (qla82xx_md_entry_hdr_t *) \
	    (((uint8_t *)ha->md_tmplt_hdr) + tmplt_hdr->first_entry_offset);

	/* Walk through the entry headers */
	for (i = 0; i < no_entry_hdr; i++) {

		if (data_collected > total_data_size) {
			qla_printk(KERN_WARNING, ha,
			    "%s(%ld): More MiniDump data collected: [0x%x]\n",
			    __func__, ha->host_no, data_collected);
			goto md_failed;
		}

		if (!(entry_hdr->d_ctrl.entry_capture_mask &
		    ql2xmdcapmask)) {
			entry_hdr->d_ctrl.driver_flags |=
			    QLA82XX_DBG_SKIPPED_FLAG;
			DEBUG11(qla_printk(KERN_WARNING, ha,
			    "%s:(%ld): Skipping entry[%d]: "
			    "ETYPE[0x%x]-ELEVEL[0x%x]\n",
			    __func__, ha->host_no, i,
			    entry_hdr->entry_type,
			    entry_hdr->d_ctrl.entry_capture_mask));
			goto skip_nxt_entry;
		}

		DEBUG11(qla_printk(KERN_WARNING, ha,
		    "[%s]: data ptr[%d]: %p, entry_hdr: %p\n"
		    "entry_type: 0x%x, captrue_mask: 0x%x\n",
		    __func__, i, data_ptr, entry_hdr,
		    entry_hdr->entry_type,
		    entry_hdr->d_ctrl.entry_capture_mask));

		DEBUG11(qla_printk(KERN_INFO, ha,
		    "Data collected: [0x%x], Dump size left:[0x%x]\n",
		    data_collected, (ha->md_dump_size - data_collected)));

		/* Decode the entry type and take
		 * required action to capture debug data */
		switch (entry_hdr->entry_type) {
		case QLA82XX_RDEND:
			qla82xx_mark_entry_skipped(ha, entry_hdr, i);
			break;
		case QLA82XX_CNTRL:
			rval = qla82xx_minidump_process_control(ha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS) {
				qla82xx_mark_entry_skipped(ha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_RDCRB:
			qla82xx_minidump_process_rdcrb(ha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDMEM:
			rval = qla82xx_minidump_process_rdmem(ha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS) {
				qla82xx_mark_entry_skipped(ha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_BOARD:
		case QLA82XX_RDROM:
			qla82xx_minidump_process_rdrom(ha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_L2DTG:
		case QLA82XX_L2ITG:
		case QLA82XX_L2DAT:
		case QLA82XX_L2INS:
			rval = qla82xx_minidump_process_l2tag(ha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS) {
				qla82xx_mark_entry_skipped(ha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_L1DAT:
		case QLA82XX_L1INS:
			qla82xx_minidump_process_l1cache(ha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDOCM:
			qla82xx_minidump_process_rdocm(ha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDMUX:
			qla82xx_minidump_process_rdmux(ha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_QUEUE:
			qla82xx_minidump_process_queue(ha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDNOP:
		default:
			qla82xx_mark_entry_skipped(ha, entry_hdr, i);
			break;
		}

		DEBUG11(qla_printk(KERN_WARNING, ha,
		    "[%s]: data ptr[%d]: %p\n", __func__, i, data_ptr));

		data_collected = (uint8_t *)data_ptr -
		    (uint8_t *)ha->md_dump;
skip_nxt_entry:
		entry_hdr = (qla82xx_md_entry_hdr_t *) \
		    (((uint8_t *)entry_hdr) + entry_hdr->entry_size);
	}

	if (data_collected != total_data_size) {
		qla_printk(KERN_WARNING, ha,
		    "MiniDump data mismatch: Data collected: [0x%x],"
		    "total_data_size:[0x%x]\n",
		    data_collected, total_data_size);
		goto md_failed;
	}

	qla_printk(KERN_INFO, ha,
	    "Firmware dump saved to temp buffer (%ld/%p %ld/%p).\n",
	    ha->host_no, ha->md_tmplt_hdr, ha->host_no, ha->md_dump);
	ha->fw_dumped = 1;
	qla2x00_post_uevent_work(ha, QLA_UEVENT_CODE_FW_DUMP);

md_failed:
	return rval;
}

int
qla82xx_md_alloc(scsi_qla_host_t *ha)
{
	int i, k;
	struct qla82xx_md_template_hdr *tmplt_hdr;

	tmplt_hdr = (struct qla82xx_md_template_hdr *)ha->md_tmplt_hdr;

	if (ql2xmdcapmask < 0x3 || ql2xmdcapmask > 0x7F) {
		ql2xmdcapmask = tmplt_hdr->capture_debug_level & 0xFF;
		qla_printk(KERN_INFO, ha,
		    "Forcing driver capture mask to firmware default capture mask: 0x%x.\n",
		    ql2xmdcapmask);
	}

	for (i = 0x2, k = 1; (i & QLA82XX_DEFAULT_CAP_MASK); i <<= 1, k++) {
		if (i & ql2xmdcapmask)
			ha->md_dump_size += tmplt_hdr->capture_size_array[k];
	}

	if (ha->md_dump) {
		qla_printk(KERN_WARNING, ha,
		    "Firmware dump previously allocated.\n");
		return 1;
	}

	ha->md_dump = vmalloc(ha->md_dump_size);
	if (ha->md_dump == NULL) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to allocate memory for Minidump size "
		    "(%d KB).\n", ha->md_dump_size / 1024);
		return 1;
	}
	return 0;
}

void
qla82xx_md_free(scsi_qla_host_t *ha)
{
	/* Release the template header allocated */
	if (ha->md_tmplt_hdr) {
		qla_printk(KERN_INFO, ha,
		    "scsi(%ld): Free MiniDump template: %p, size (%d KB)\n",
		    ha->host_no, ha->md_tmplt_hdr, ha->md_template_size / 1024);
                dma_free_coherent(&ha->pdev->dev, ha->md_template_size,
                    ha->md_tmplt_hdr, ha->md_tmplt_hdr_dma);
		ha->md_tmplt_hdr = 0;
	}

	/* Release the template data buffer allocated */
	if (ha->md_dump) {
		qla_printk(KERN_INFO, ha,
		    "scsi(%ld): Free MiniDump memory: %p, size (%d KB)\n",
		    ha->host_no, ha->md_dump, ha->md_dump_size / 1024);
		vfree(ha->md_dump);
		ha->md_dump_size = 0;
		ha->md_dump = 0;
	}
}

void
qla82xx_md_prep(scsi_qla_host_t *ha)
{
	int rval;

	/* Get Minidump template size */
	rval = qla82xx_md_get_template_size(ha);
	if (rval == QLA_SUCCESS) {
		qla_printk(KERN_INFO, ha,
		    "scsi(%ld): MiniDump Template size obtained (%d KB)\n",
		    ha->host_no, ha->md_template_size / 1024);

		/* Get Minidump template */
		rval = qla82xx_md_get_template(ha);
		if (rval == QLA_SUCCESS) {
			qla_printk(KERN_INFO, ha,
			    "scsi(%ld): MiniDump Template obtained\n",
			    ha->host_no);

			/* Allocate memory for minidump */
			rval = qla82xx_md_alloc(ha);
			if (rval == QLA_SUCCESS)
				qla_printk(KERN_INFO, ha,
				    "scsi(%ld): MiniDump memory allocated (%d KB)\n",
				    ha->host_no, ha->md_dump_size / 1024);
			else {
				qla_printk(KERN_INFO, ha,
				    "scsi(%ld): Free MiniDump template: %p, size: (%d KB)\n",
				    ha->host_no, ha->md_tmplt_hdr,
				    ha->md_template_size / 1024);
				dma_free_coherent(&ha->pdev->dev,
				    ha->md_template_size,
				    ha->md_tmplt_hdr, ha->md_tmplt_hdr_dma);
				ha->md_tmplt_hdr = 0;
			}

		}
	}
}

#ifndef ISP_QDEFS_H_
#define ISP_QDEFS_H_
struct qsio_scsiio;
void isp_complete_ctio(struct qsio_scsiio *);
void isp_common_dmateardownt(ispsoftc_t *isp, struct qsio_scsiio *csio);
void isp_handle_platform_atio7(ispsoftc_t *, at7_entry_t *);
void isp_handle_platform_ctio(ispsoftc_t *, void *);
void isp_handle_platform_target_tmf(ispsoftc_t *, isp_notify_t *);
void isp_target_start_ctio(ispsoftc_t *, struct qsio_scsiio *);
int isp_pci_dmasetupt(ispsoftc_t *isp, struct qsio_scsiio *csio, void *ff);
int tptr_alloc(ispsoftc_t *isp);
void tptr_free(ispsoftc_t *isp);
#endif

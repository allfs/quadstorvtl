#ifndef QUADSTOR_IETADM_H_
#define SCAHE_IETADM_H_
#include <apicommon.h>
void vdevice_construct_iqn(struct vdevice *vdevice, struct vdevice *parent);
int ietadm_default_settings(struct vdevice *vdevice, struct vdevice *parent);
int ietadm_mod_target(int tid, struct iscsiconf *iscsiconf, struct iscsiconf *oldconf);
int ietadm_add_target(struct vdevice *vdevice);
int ietadm_delete_target(int tid);
int ietadm_qload_done(void);
#endif

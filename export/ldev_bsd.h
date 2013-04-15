#ifndef LDEV_BSD_H_
#define LDEV_BSD_H_
#include "bsddefs.h"

struct ldev_bsd {
	spinlock_t cam_mtx;
	struct cam_sim *sim;
	struct cam_path *path;
	struct tdevice *device;
	struct qs_devq *devq;
	atomic_t pending_cmds;
	atomic_t disabled;
};

#define LDEV_HOST_ID	7
struct qsio_scsiio;
void ldev_proc_cmd(struct qsio_scsiio *ctio);
#endif

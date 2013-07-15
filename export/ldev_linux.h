#ifndef LDEV_LINUX_H_
#define LDEV_LINUX_H_

#include "linuxdefs.h"

struct ldev_priv {
	struct tdevice *device;
	struct qs_devq *devq;
	atomic_t pending_cmds;
	atomic_t disabled;
};

#define LDEV_NAME	"QUADStor vtldev"
#define LDEV_HOST_ID	15

struct qsio_scsiio;
void ldev_proc_cmd(struct qsio_scsiio *ctio);
#endif


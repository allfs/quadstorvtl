#ifndef QS_IOCTLDEFS_H_
#define QS_IOCTLDEFS_H_ 1

#include "commondefs.h"

/* List of IOCTLS */
#define TL_MAGIC			'k'

#define TLTARGIOCNEWBLKDEV		_IOWR(TL_MAGIC, 1, struct bdev_info)
#define TLTARGIOCDELBLKDEV		_IOWR(TL_MAGIC, 2, struct bdev_info)
#define TLTARGIOCGETBLKDEV		_IOWR(TL_MAGIC, 3, struct bdev_info)
#define TLTARGIOCDAEMONSETINFO		_IOWR(TL_MAGIC, 4, struct mdaemon_info)
#define TLTARGIOCCHECKDISKS		_IO(TL_MAGIC, 5)
#define TLTARGIOCLOADDONE		_IO(TL_MAGIC, 6) 
#define TLTARGIOCENABLEDEVICE		_IOWR(TL_MAGIC, 7, uint32_t)
#define TLTARGIOCDISABLEDEVICE		_IOWR(TL_MAGIC, 8, uint32_t)
#define TLTARGIOCBINTRESETSTATS		_IOWR(TL_MAGIC, 16, uint32_t)
#define TLTARGIOCUNLOAD			_IO(TL_MAGIC, 18) 
#define TLTARGIOCNEWBDEVSTUB		_IOWR(TL_MAGIC, 20, struct bdev_info) 
#define TLTARGIOCDELETEBDEVSTUB		_IOWR(TL_MAGIC, 21, struct bdev_info) 
#define TLTARGIOCADDGROUP		_IOWR(TL_MAGIC, 32, struct group_conf)
#define TLTARGIOCDELETEGROUP		_IOWR(TL_MAGIC, 33, struct group_conf)
#define TLTARGIOCADDFCRULE		_IOWR(TL_MAGIC, 37, struct fc_rule_config)
#define TLTARGIOCREMOVEFCRULE		_IOWR(TL_MAGIC, 38, struct fc_rule_config)
#define TLTARGIOCUNMAPCONFIG		_IOWR(TL_MAGIC, 40, struct bdev_info)
#define TLTARGIOCRENAMEGROUP		_IOWR(TL_MAGIC, 41, struct group_conf)
#define TLTARGIOCWCCONFIG		_IOWR(TL_MAGIC, 43, struct bdev_info)
#define TLTARGIOCNEWVCARTRIDGE		_IOWR(TL_MAGIC, 44, struct vcartridge)
#define TLTARGIOCLOADVCARTRIDGE		_IOWR(TL_MAGIC, 45, struct vcartridge)
#define TLTARGIOCDELETEVCARTRIDGE	_IOWR(TL_MAGIC, 46, struct vcartridge)
#define TLTARGIOCGETVCARTRIDGEINFO	_IOWR(TL_MAGIC, 47, struct vcartridge)
#define TLTARGIOCNEWDEVICE		_IOWR(TL_MAGIC, 48, struct vdeviceinfo)
#define TLTARGIOCDELETEDEVICE		_IOWR(TL_MAGIC, 49, struct vdeviceinfo)
#define TLTARGIOCMODDEVICE		_IOWR(TL_MAGIC, 50, struct vdeviceinfo)
#define TLTARGIOCGETDEVICEINFO		_IOWR(TL_MAGIC, 51, struct vdeviceinfo)
#define TLTARGIOCLOADDRIVE		_IOWR(TL_MAGIC, 52, struct vdeviceinfo)

#endif

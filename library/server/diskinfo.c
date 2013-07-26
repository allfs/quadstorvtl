#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <apicommon.h>
#include "diskinfo.h"

#ifdef FREEBSD
int disk_getsize(char *device, uint64_t *size)
{
	int fd;
	int err;
	off_t dsize;

	*size = 0;

	fd = open(device, O_RDONLY);

	if (fd < 0)
	{
		DEBUG_ERR("Unable to open device %s\n", device);
		return -1;
	}

	err = ioctl(fd, DIOCGMEDIASIZE, &dsize);
	if (err)
	{
		DEBUG_ERR("Disk size ioctl failed with errno %d and msg %s\n", errno, strerror(errno));
		return err;
	}
	close(fd);

	*size = dsize;
	return 0;
}
#else
static int
sectorsize(int fd, int *sector_size) {
	int err;

	err = ioctl(fd, BLKSSZGET, sector_size);
	if (err)
	{
		DEBUG_ERR("Failed to get sector size errno %d msg %s\n", errno, strerror(errno));
		*sector_size = 512;
	}
	return 0;
}

static int
disksize(int fd, uint64_t *sectors)
{
	int err;
	long sz = 0L;
	long long b = 0LL;

	err = ioctl(fd, BLKGETSIZE, &sz);
	if (err)
	{
		DEBUG_ERR("Disk size ioctl failed with errno %d and msg %s\n", errno, strerror(errno));
		return err;
	}
	err = ioctl(fd, BLKGETSIZE64, &b);
	if (err || b == 0 || b == sz)
	{
		*sectors = sz;
	}
	else
	{
		*sectors = (b >> 9);
	}
	return 0;
}

int disk_getsize(char *device, uint64_t *size)
{
	int fd;
	int err;
	uint64_t nsectors;
	int sector_size;

	*size = 0;

	fd = open(device, O_RDONLY);

	if (fd < 0)
	{
		DEBUG_ERR("Unable to open device %s\n", device);
		return -1;
	}

	err = disksize(fd, &nsectors);
	if (err != 0)
	{
		DEBUG_ERR("Getting disksize for disk %s failed\n", device);
		close(fd);
		return -1;
	}

	sectorsize(fd, &sector_size);
	close(fd);
	*size = (nsectors << 9);
	return 0;
}
#endif

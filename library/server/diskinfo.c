#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <apicommon.h>
#include "diskinfo.h"

dev_t 
get_device_id(char *devname)
{
	struct stat stbuf;

	if (stat(devname, &stbuf) < 0)
		return -1;
	else
		return stbuf.st_rdev;
}

#ifdef FREEBSD
int
is_swap_disk(char *devname)
{
	char buf[512];
	char swapdev[256];
	FILE *fp;

	fp = popen("/usr/sbin/swapinfo", "r");

	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		sscanf(buf, "%s", swapdev);
		if (strcmp(swapdev, devname) == 0) {
			pclose(fp);
			return 1;
		}
	}
	pclose(fp);
	return 0;
}

int
check_blkdev_valid(char *devname)
{
	struct statfs *mntinfo;
	struct statfs *ent;
	int count;

	if (is_swap_disk(devname))
		return -1;

	count = getmntinfo(&mntinfo, MNT_WAIT);

	if (!count)
		return 0;

	for (ent = mntinfo; count--; ent++) {
		if (strcmp(ent->f_mntfromname, devname) == 0)
			return -1;

	}
	return 0;
}

int
is_mount_exists(char *dirname)
{
	struct statfs *mntinfo;
	struct statfs *ent;
	int count;
	int retval = 0;

	count = getmntinfo(&mntinfo, MNT_WAIT);

	if (!count)
	{
		return -1;
	}

	for (ent = mntinfo; count--; ent++)
	{
		/* Check through each mnt ent */
		if (strcmp(ent->f_mntonname, dirname) == 0)
		{
			retval = 1;
		}
	} 
	return retval;
}
#else

int
__check_blkdev_valid(char *devname, char *path)
{
	FILE *fp;
	struct mntent *ent;
	int retval;

	fp = setmntent(path, "r");
	if (!fp)
		return -1;

	retval = 0;
	while ((ent = getmntent(fp))) {
		/* Check through each mnt ent */
		if (strcmp(ent->mnt_fsname, devname) == 0) {
			retval = -1;
			break;
		}
	}
	endmntent(fp);
	return retval;
}

int
is_swap_disk(char *devname)
{
	char buf[512];
	char swapdev[256];
	FILE *fp;

	fp = fopen("/proc/swaps", "r");

	if (!fp)
		return 0;

	while (fgets(buf, sizeof(buf), fp) != NULL) {
		sscanf(buf, "%s", swapdev);
		if (strcmp(swapdev, devname) == 0) {
			fclose(fp);
			return 1;
		}
	}
	fclose(fp);
	return 0;
}

int
check_blkdev_valid(char *devname)
{
	int retval;

	if (is_swap_disk(devname))
		return -1;

	retval = __check_blkdev_valid(devname, _PATH_MNTTAB);
	if (retval != 0)
		return retval;

	retval = __check_blkdev_valid(devname, _PATH_MOUNTED);
	return retval;
}

int
is_mount_exists(char *dirname)
{
	FILE *fp;
	struct mntent *ent;

	fp = setmntent(MOUNTED, "r");
	if (!fp)
	{
		return 0;
	}

	while ((ent = getmntent(fp)))
	{
		/* Check through each mnt ent */
		if (strcmp(ent->mnt_dir, dirname) == 0)
		{
			endmntent(fp);
			return 1;
		}
	}
	endmntent(fp);
	return 0;
}
#endif

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
		DEBUG_ERR("Getting disksize for disk failed\n");
		close(fd);
		return -1;
	}

	sectorsize(fd, &sector_size);
	close(fd);
	*size = (nsectors << 9);
	return 0;
}
#endif

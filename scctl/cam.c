/* Borrowed scan logic from camcontrol.c */
/*
 * Copyright (c) 1997-2007 Kenneth D. Merry
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#include <sys/cdefs.h>

#include <sys/ioctl.h>
#include <sys/stdint.h>
#include <sys/types.h>
#include <sys/endian.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <err.h>
#include <libutil.h>
#include <inttypes.h>

#include <cam/cam.h>
#include <cam/cam_debug.h>
#include <cam/cam_ccb.h>
#include <cam/scsi/scsi_all.h>
#include <cam/scsi/scsi_da.h>
#include <cam/scsi/scsi_pass.h>
#include <cam/scsi/scsi_message.h>
#include <cam/ata/ata_all.h>
#include <camlib.h>
#include <sys/ata.h>

#if 0
static void
param_print(char *device, struct ata_params *param)
{
	fprintf(stdout, "/dev/%s %u:%u:%u:%"PRIu64" %x %.8s %.16s %.4s\n", device, 0, 0, 0, 0, param->config, "ATA", param->model, param->revision);
}

static int 
info_print(int fd, int channel)
{
        struct ata_ioc_devices devices;
	struct ata_params *parm;

        devices.channel = channel;
	if (ioctl(fd, IOCATADEVICES, &devices) < 0) {
		fprintf(stderr, "failed with error %d\n", errno);
		return 0;
	}

	if (*devices.name[0]) {
		param_print(devices.name[0], &devices.params[0]);
	}

	if (*devices.name[1]) {
		param_print(devices.name[1], &devices.params[1]);
	}
	return 0;
}

static int
getatalist(void)
{
	int maxchannel;
	int channel;
	int fd;
	int retval;

	if ((fd = open("/dev/ata", O_RDWR)) < 0) {
                fprintf(stderr, "failed to open control device\n");
		return -1;
	}

	if (ioctl(fd, IOCATAGMAXCHANNEL, &maxchannel) < 0) {
		fprintf(stderr, "failed to get channel\n");
		return -1;
	}
	for (channel = 0; channel < maxchannel; channel++) {
		retval = info_print(fd, channel);
		if (retval != 0) {
			fprintf(stderr, "failed to channel info %d\n", channel);
			return -1;
		}
	}
	close(fd);
	return 0;
}
#endif

void fix_string(uint8_t *str, int size)
{
	int i;

	for (i = 0; i < size; i++)
		if (str[i] == 0)
			str[i] = ' ';
}

void fix_vendor(uint8_t *str, int size)
{
	int i;

	for (i = 0; i < size; i++)
		if (str[i] != ' ')
			return;
	memcpy(str, "ATA", strlen("ATA"));
}

static int
getdevtree(void)
{
	union ccb ccb;
	int bufsize, fd;
	unsigned int i;
	int need_close = 0;
	int error = 0;
	int skip_device = 0;
	uint8_t vendor[16], product[48], revision[16];
	int type = 0;
	path_id_t path_id;
	target_id_t target_id;
	lun_id_t lun_id;

	if ((fd = open(XPT_DEVICE, O_RDWR)) == -1) {
		warn("couldn't open %s", XPT_DEVICE);
		return(1);
	}

	bzero(&ccb, sizeof(union ccb));

	ccb.ccb_h.path_id = CAM_XPT_PATH_ID;
	ccb.ccb_h.target_id = CAM_TARGET_WILDCARD;
	ccb.ccb_h.target_lun = CAM_LUN_WILDCARD;

	ccb.ccb_h.func_code = XPT_DEV_MATCH;
	bufsize = sizeof(struct dev_match_result) * 100;
	ccb.cdm.match_buf_len = bufsize;
	ccb.cdm.matches = (struct dev_match_result *)malloc(bufsize);
	if (ccb.cdm.matches == NULL) {
		warnx("can't malloc memory for matches");
		close(fd);
		return(1);
	}
	ccb.cdm.num_matches = 0;

	/*
	 * We fetch all nodes, since we display most of them in the default
	 * case, and all in the verbose case.
	 */
	ccb.cdm.num_patterns = 0;
	ccb.cdm.pattern_buf_len = 0;

	/*
	 * We do the ioctl multiple times if necessary, in case there are
	 * more than 100 nodes in the EDT.
	 */
	do {
		struct device_match_result *dev_result;

		if (ioctl(fd, CAMIOCOMMAND, &ccb) == -1) {
			warn("error sending CAMIOCOMMAND ioctl");
			error = 1;
			break;
		}

		if ((ccb.ccb_h.status != CAM_REQ_CMP)
		 || ((ccb.cdm.status != CAM_DEV_MATCH_LAST)
		    && (ccb.cdm.status != CAM_DEV_MATCH_MORE))) {
			warnx("got CAM error %#x, CDM error %d\n",
			      ccb.ccb_h.status, ccb.cdm.status);
			error = 1;
			break;
		}

		for (i = 0; i < ccb.cdm.num_matches; i++) {
			switch (ccb.cdm.matches[i].type) {
			case DEV_MATCH_BUS:
				if (need_close) {
					need_close = 0;
				}

				break;
			case DEV_MATCH_DEVICE: {
				dev_result =
				     &ccb.cdm.matches[i].result.device_result;

				if (dev_result->flags & DEV_RESULT_UNCONFIGURED) {
					skip_device = 1;
					break;
				}
				skip_device = 0;

				memset(vendor, ' ', sizeof(vendor));
				memset(product, ' ', sizeof(product));
				memset(revision, ' ', sizeof(revision));
				if (dev_result->protocol == PROTO_SCSI) {
				    type = dev_result->inq_data.device & 0x1f;
				    cam_strvis(vendor, (uint8_t *)dev_result->inq_data.vendor,
					   sizeof(dev_result->inq_data.vendor),
					   sizeof(vendor));
				    cam_strvis(product,
					   (uint8_t *)dev_result->inq_data.product,
					   sizeof(dev_result->inq_data.product),
					   sizeof(product));
				    cam_strvis(revision,
					   (uint8_t *)dev_result->inq_data.revision,
					  sizeof(dev_result->inq_data.revision),
					   sizeof(revision));
				} else if (dev_result->protocol == PROTO_ATA ||
				    dev_result->protocol == PROTO_SATAPM) {
				    cam_strvis(product,
					   dev_result->ident_data.model,
					   sizeof(dev_result->ident_data.model),
					   sizeof(product));
				    cam_strvis(revision,
					   dev_result->ident_data.revision,
					  sizeof(dev_result->ident_data.revision),
					   sizeof(revision));
				}
				if (need_close) {
					need_close = 0;
				}

				path_id = dev_result->path_id;
				target_id = dev_result->target_id;
				lun_id = dev_result->target_lun;

				need_close = 1;

				break;
			}
			case DEV_MATCH_PERIPH: {
				struct periph_match_result *periph_result;

				periph_result =
				      &ccb.cdm.matches[i].result.periph_result;

				if (skip_device != 0)
					break;

				fix_string(vendor, 8);
				fix_vendor(vendor, 8);
				fix_string(product, 16);
				fix_string(vendor, 4);

				if (strncmp(periph_result->periph_name, "pass", 4) == 0 && memcmp(product, "FCSCBRIDGE", strlen("FCSCBRIDGE")))
					break;
				if (type != T_DIRECT)
					break;

				fprintf(stdout, "/dev/%s%d %x %.8s %.16s %.4s\n", periph_result->periph_name, periph_result->unit_number, type, vendor, product, revision);
				type = 0;
				need_close++;
				break;
			}
			default:
				fprintf(stdout, "unknown match type\n");
				break;
			}
		}

	} while ((ccb.ccb_h.status == CAM_REQ_CMP)
		&& (ccb.cdm.status == CAM_DEV_MATCH_MORE));

	close(fd);

	return(error);
}

int main()
{
	getdevtree();
#if 0
	getatalist();
#endif
	return 0;
}


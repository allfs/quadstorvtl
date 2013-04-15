/* 
 * Copyright (C) Shivaram Upadhyayula <shivaram.u@quadstor.com>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * Version 2 as published by the Free Software Foundation
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 * Boston, MA  02110-1301, USA.
 */

#include <html-lib.h>
#include "cgimain.h"
#include <tlclntapi.h>
#include <vdevice.h>

int main()
{
	llist entries;
	char *label;
	int nvolumes;
	int voltype;
	char *tmp;
	char cmd[512];
	char reply[512];
	int retval;
	int tl_id;
	int vtltype;
	int worm = 0;
	uint32_t group_id;

	read_cgi_input(&entries);

	tmp = cgi_val(entries, "group_id");
	if (tmp)
		group_id = strtoull(tmp, NULL, 10);
	else
		group_id = 0;

	label = cgi_val(entries, "barcode");
	if (!label)
		cgi_print_header_error_page("No volume label specified in add volume");

	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid cgi parameters passed. No library id passed");
	tl_id = atoi(tmp);

	tmp = cgi_val(entries, "vtltype");

	if (!tmp || !(vtltype = atoi(tmp)))
		cgi_print_header_error_page("Invalid cgi parameters passed. Invalid virtual device type");

	tmp = cgi_val(entries, "voltype");
	if (!tmp)
		cgi_print_header_error_page("No volume type specified in add volume");

	voltype = atoi(tmp);

	tmp = cgi_val(entries, "nvolumes");
	if (!tmp)
		cgi_print_header_error_page("Number of volumes not specified in add volume");

	nvolumes = atoi(tmp);

	tmp = cgi_val(entries, "worm");
	if (tmp && (strcasecmp(tmp, "on") == 0))
		worm = 1;

	reply[0] = 0;
	retval = tl_client_add_vol_conf(group_id, label, tl_id, voltype, nvolumes, worm, reply);
	if (retval != 0) {
		char errmsg[1024];

		sprintf(errmsg, "Unable to add a new VCartridge.<br>Message from server is:<br>\"%s\"\n", reply);
		cgi_print_header_error_page(errmsg);
	}

	if (vtltype == T_CHANGER)
		sprintf(cmd, "indvvtl.cgi?tl_id=%d", tl_id);
	else
		sprintf(cmd, "indvdrive.cgi?tl_id=%d", tl_id);
	cgi_redirect(cmd);
	return 0;
}

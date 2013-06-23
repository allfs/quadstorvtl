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

int main()
{
	llist entries;
	char *tmp;
	char *name;
	int drivetype;
	int ret_tl_id;
	int retval;
	char reply[512];
	char cmd[256];

	read_cgi_input(&entries);

	name = cgi_val(entries, "name");
	if (!name)
		cgi_print_header_error_page("No name specified for VDrive");

	tmp = cgi_val(entries, "drivetype");
	if (!tmp)
		cgi_print_header_error_page("VDrive type not specified");

	drivetype = atoi(tmp);

	retval = tl_client_add_drive_conf(name, drivetype, &ret_tl_id, reply);

	if (retval != 0) {
		char errmsg[1024];

		sprintf (errmsg, "Unable to add new Virtual Library/Drive.<br>Message from server is:<br>\"%s\"\n", reply);
		cgi_print_header_error_page(errmsg);
	}

	if (ret_tl_id < 0)
		cgi_print_header_error_page("Internal processing error");

	sprintf(cmd, "indvvdrive.cgi?tl_id=%d", ret_tl_id);
	cgi_redirect(cmd);
	return 0;
}

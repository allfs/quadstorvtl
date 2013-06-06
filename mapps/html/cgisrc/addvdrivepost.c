/*
 * Copyright (C) Shviaram Narasimha Murthy
 * All Rights Reserved
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

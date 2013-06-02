/*
 * Physical Libraries view
 * Copyright (C) Shivaram Narasimha Murthy
 * All Rights Reserved
 */
#include <html-lib.h>
#include "cgimain.h"
#include <tlclntapi.h>
#include <physlib.h>

int main()
{
	llist entries;
	int tl_id, tid, msg_id;
	int retval;
	char reply[256];
	char *tmp;
	char cmd[100];


	read_cgi_input(&entries);
	tmp = cgi_val(entries, "tid");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters\n");

	tid = atoi(tmp);
	if (!tid)
		cgi_print_header_error_page("Invalid CGI parameters\n");

	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters\n");

	tl_id = atoi(tmp);

	tmp = cgi_val(entries, "msg_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters\n");

	msg_id = atoi(tmp);
	if (msg_id != MSG_ID_LOAD_DRIVE && msg_id != MSG_ID_UNLOAD_DRIVE)
		cgi_print_header_error_page("Invalid CGI parameters\n");

	retval = tl_client_load_drive(msg_id, tl_id, tid, reply);

	if (retval != 0)
		cgi_print_header_error_page("Load drive operation failed\n");

	sprintf(cmd, "indvvdrive.cgi?tl_id=%d", tl_id);
	cgi_redirect(cmd);
	return 0;
}

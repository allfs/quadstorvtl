/*
 * Copyright (C) Shivaram Narasimha Murthy
 * All Rights Reserved
 */

#include <html-lib.h>
#include "cgimain.h"
#include <tlclntapi.h>
#include <vdevice.h>

int main()
{
	llist entries;
	uint32_t tape_id;
	int tl_id;
	char *tmp;
	char cmd[256];
	int retval;

	read_cgi_input(&entries);
	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters");

	tl_id = atoi(tmp);

	tmp = cgi_val(entries, "tape_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters");

	tape_id = atoi(tmp);

	retval = tl_client_reload_export(tl_id, tape_id);
	if (retval != 0)
		cgi_print_header_error_page("Reloading exported tape failed");

	sprintf(cmd, "indvvtl.cgi?tl_id=%d", tl_id);
	cgi_redirect(cmd);
	return 0;
}

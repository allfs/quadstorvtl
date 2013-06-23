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

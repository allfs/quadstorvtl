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

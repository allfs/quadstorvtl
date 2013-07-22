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

#include "cgimain.h"

int main()
{
	llist entries;
	char *tmp;
	uint32_t group_id;
	char reply[512], errmsg[512];
	int retval;

	read_cgi_input(&entries);

	tmp = cgi_val(entries, "group_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters passed\n");

	group_id = strtoul(tmp, NULL, 10);
	if (!group_id)
		cgi_print_header_error_page("Invalid CGI parameters passed\n");

	retval = tl_client_delete_group(group_id, reply);
	if (retval != 0) {
		snprintf(errmsg, sizeof(errmsg), "Unable to delete pool<br/>Message from server is:<br/>\"%s\"\n", reply);
		cgi_print_header_error_page(errmsg);
	}

	cgi_redirect("vtliststoragepool.cgi");
	return 0;
}

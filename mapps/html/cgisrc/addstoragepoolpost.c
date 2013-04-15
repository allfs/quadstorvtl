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
	char reply[512];
	int retval;
	int worm = 0;
	char *groupname;
	char *tmp;

	read_cgi_input(&entries);

	groupname = cgi_val(entries, "groupname");
	if (!groupname)
		cgi_print_header_error_page("Invalid CGI parameters passed\n");

	tmp = cgi_val(entries, "worm");
	if (tmp && (strcasecmp(tmp, "on") == 0))
		worm = 1;

	reply[0] = 0;

	retval = tl_client_add_group(groupname, worm, reply);
	if (retval != 0) {
		char errmsg[1024];

		sprintf(errmsg, "Unable to add pool. Message from server is: \"%s\"\n", reply);
		cgi_print_header_error_page(errmsg);
	}

	cgi_redirect("liststoragepool.cgi");
	return 0;
}

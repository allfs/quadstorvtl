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
	char *dev;
	int retval;
	char reply[256];
	int op;
	uint32_t group_id;
	char *tmp;

	read_cgi_input(&entries);

	tmp = cgi_val(entries, "op");
	if (!tmp || !(op = atoi(tmp)))
	{
		cgi_print_header_error_page("Add/Delete Storage: Invalid operation");
	}

	tmp = cgi_val(entries, "group_id");
	if (tmp)
		group_id = strtoull(tmp, NULL, 10);
	else
		group_id = 0;

	dev = cgi_val(entries, "dev");
	if (!dev)
	{
		cgi_print_header_error_page("Insufficient CGI parameters\n");
	}

	if (op == 1)
	{
		retval = tl_client_add_disk(dev, group_id, reply);
	}
	else
	{
		retval = tl_client_delete_disk(dev, reply);
	}

	if (retval != 0)
	{
		char errmsg[512];

		sprintf(errmsg, "Reply from server is \"%s\"", reply);
		cgi_print_header_error_page(errmsg);
	}

	cgi_redirect("adddisk.cgi");
	return 0;
}

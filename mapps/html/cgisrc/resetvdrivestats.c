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
	char cmd[64];
	int retval;
	uint32_t tl_id;
	uint32_t target_id;

	read_cgi_input(&entries);

	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters passed\n");

	tl_id = strtoul(tmp, NULL, 10);

	tmp = cgi_val(entries, "target_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters passed\n");

	target_id = strtoul(tmp, NULL, 10);

	retval = tl_client_reset_vdrive_stats(tl_id, target_id);
	if (retval != 0)
		cgi_print_header_error_page("Resetting vdrive stats failed\n");

	sprintf(cmd, "vdrivestats.cgi?tl_id=%d&target_id=%d", tl_id, target_id);
	cgi_redirect(cmd);
	return 0;
}

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
	char *tmp;
	int tl_id;
	int tape_id;
	int retval;
	char cmd[512];
	int vtltype;

	read_cgi_input(&entries);

	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
		cgi_print_header_error_page("No VTL specified for volume deletion");
	tl_id = atoi(tmp);

	tmp = cgi_val(entries, "tape_id");
	if (!tmp)
		cgi_print_header_error_page("No virtual volume specified for deletion");
	tape_id = atoi(tmp);

	tmp = cgi_val(entries, "vtltype");
	if (!tmp || !(vtltype = atoi(tmp)))
		cgi_print_header_error_page("No virtual device type specified");

	retval = tl_client_delete_vol_conf(tl_id, tape_id);
	if (retval != 0)
		cgi_print_header_error_page("Unable to delete the specified volume");

	if (vtltype == T_CHANGER)
		sprintf(cmd, "indvvtl.cgi?tl_id=%d", tl_id);
	else
		sprintf(cmd, "indvdrive.cgi?tl_id=%d", tl_id);
	cgi_redirect(cmd);
	return 0;
}

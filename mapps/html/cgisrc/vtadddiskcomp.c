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
	char *op;
	struct group_list group_list;
	struct group_info *group_info;
	int retval;

	read_cgi_input(&entries);

	op = cgi_val(entries, "op");
	if (!op) {
		cgi_print_header_error_page("Insufficient CGI parameters\n");
		return 0;
	}

	dev = cgi_val(entries, "dev");
	if (!dev)
		cgi_print_header_error_page("Insufficient CGI parameters\n");

	retval = tl_client_list_groups(&group_list, MSG_ID_LIST_GROUP);
	if (retval != 0)
		cgi_print_header_error_page("Getting pool list failed\n");

	cgi_print_header("Select Storage Pool", NULL, 0);

	cgi_print_form_start("vtadddiskcomp", "vtadddiskpost.cgi", "post", 0);
	printf("<input type=\"hidden\" name=\"op\" value=\"%s\">\n", op);
	printf("<input type=\"hidden\" name=\"dev\" value=\"%s\">\n", dev);
	
	cgi_print_thdr("Select Disk Properties");

	cgi_print_div_start("center");
	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<td>Storage Pool:</td>\n");
	printf("<td><select name=\"group_id\">\n");
	TAILQ_FOREACH(group_info, &group_list, q_entry) {
		printf("<option value=\"%u\">%s</option>\n", group_info->group_id, group_info->name);
	}
	printf("</select></td>\n");
	printf("</tr>\n");
	group_list_free(&group_list);

	printf("</table>\n");
	cgi_print_div_end();

	cgi_print_submit_button("submit", "Submit");

	cgi_print_form_end();

	cgi_print_div_trailer();

	cgi_print_body_trailer();
	return 0;
}

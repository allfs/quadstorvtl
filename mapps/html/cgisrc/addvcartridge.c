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

extern struct voltype voltypes[];

int main()
{
	struct group_list group_list;
	struct group_info *group_info;
	llist entries;
	char *vtlname;
	char *tmp;
	int nvoltypes;
	int vtltype;
	int i, tl_id, retval;

	retval = tl_client_list_groups(&group_list, MSG_ID_LIST_GROUP_CONFIGURED);

	if (retval != 0)
		cgi_print_header_error_page("Getting pool list failed\n");

	if (TAILQ_EMPTY(&group_list))
		cgi_print_header_error_page("Could not find any storage pool with configured disk storage");

	read_cgi_input(&entries);
	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters\n");

	tl_id = atoi(tmp);

	vtlname = cgi_val(entries, "vtlname");
	if (!vtlname)
		cgi_print_header_error_page("Invalid CGI parameters\n");

	tmp = cgi_val(entries, "nvoltypes");
	if (!tmp || !(nvoltypes = atoi(tmp)))
		cgi_print_header_error_page("Invalid CGI parameters\n");

	tmp = cgi_val(entries, "vtltype");
	if (!tmp || !(vtltype = atoi(tmp)))
		cgi_print_header_error_page("Invalid CGI parameters\n");

	cgi_print_header("Add VCartridge", "addvcartridge.js", 0);
	cgi_print_thdr("Add VCartridge");

	cgi_print_form_start("addvcartridge", "addvcartridgepost.cgi", "post", 1);
	printf("<input type=\"hidden\" name=\"tl_id\" value=\"%d\">\n", tl_id);
	printf("<input type=\"hidden\" name=\"vtltype\" value=\"%d\">\n", vtltype);
	printf("<input type=\"hidden\" name=\"vtlname\" value=\"%s\">\n", vtlname);

	cgi_print_div_start("center");
	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<td>VTL Name</td>\n");
	printf("<td>%s</td>\n", vtlname);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>VCartridge Type</td>\n");
	if (nvoltypes > 1) {
		printf("<td><SELECT name=\"voltype\">\n");
		for (i =0; i< nvoltypes; i++) {
			char vspec[20];
			int vtype;

			sprintf(vspec, "vtype%d", i);
			tmp = cgi_val(entries, vspec);
			if (!tmp || !(vtype = atoi(tmp)))
				cgi_print_header_error_page("Invalid CGI Parameters\n");

			printf("<OPTION value=\"%d\">%s</OPTION>\n", voltypes[vtype - 1].type, voltypes[vtype - 1].name );
		}
		printf("</SELECT></td>\n");
	}
	else {
		int vtype;
		tmp = cgi_val(entries, "vtype0");
		if (!tmp || !(vtype = atoi(tmp)))
			cgi_print_header_error_page("Invalid CGI Parameters\n");

		printf("<input type=\"hidden\" name=\"voltype\" value=\"%d\">\n", vtype);
		printf("<td>%s</td>\n", voltypes[vtype - 1].name);
	}

	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Storage Pool:</td>\n");
	printf("<td><select class=\"inputt\" name=\"group_id\">\n");
	TAILQ_FOREACH(group_info, &group_list, q_entry) {
		printf("<option value=\"%u\">%s</option>\n", group_info->group_id, group_info->name);
	}
	printf("</select></td>\n");
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Number of VCartridges</td>\n");

	if (vtltype == T_CHANGER)
		cgi_print_text_input_td("nvolumes", 4, "1", 4);
	else {
		printf("<input type=\"hidden\" name=\"nvolumes\" value=\"1\">\n");
		printf("<td>1</td>\n");
	}

	printf("<tr>\n");
	printf("<td>WORM: </td>\n");
	printf("<td>");
	cgi_print_checkbox_input("worm", 0);
	printf("</td>\n");
	printf("</tr>\n");

	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>VCartridge Label/Prefix</td>\n");
	cgi_print_text_input_td("barcode", 10, "", 32);
	printf("</tr>\n");

	printf("</table>\n");

	cgi_print_submit_button("submit", "Submit");

	cgi_print_div_end();

	cgi_print_form_end();

	cgi_print_div_trailer();

	cgi_print_body_trailer();

	return 0;
}


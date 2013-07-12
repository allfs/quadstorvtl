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
	int tl_id;
	uint32_t target_id;
	int vtltype;
	char *tmp;
	int retval;
	struct iscsiconf iscsiconf;

	read_cgi_input(&entries);
	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI Parameters\n");

	tl_id = atoi(tmp);

	tmp = cgi_val(entries, "target_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI Parameters\n");

	target_id = atoi(tmp);

	tmp = cgi_val(entries, "vtltype");
	if (!tmp || !(vtltype = atoi(tmp)))
		cgi_print_header_error_page("Invalid CGI Parameters\n");

	retval = tl_client_get_iscsiconf(tl_id, target_id, &iscsiconf);
	if (retval != 0)
		cgi_print_header_error_page("Unable to get iSCSI configuration\n");
	cgi_print_header("iSCSI Configuration", "vtiscsiconf.js", 0);

	cgi_print_form_start_check("iscsiconf", "iscsiconfpost.cgi", "post", "checkform");
	printf("<INPUT type=hidden name=tl_id value=%d>\n", tl_id);
	printf("<INPUT type=hidden name=target_id value=%d>\n", target_id);
	printf("<INPUT type=hidden name=vtltype value=%d>\n", vtltype);

	cgi_print_thdr("iSCSI Configuration");

	cgi_print_div_start("center");
	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<td>IQN:</td>\n");
	cgi_print_text_input_td("iqn", 40, iscsiconf.iqn, 255);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Incoming User:</td>\n");
	cgi_print_text_input_td("IncomingUser", 20, iscsiconf.IncomingUser, 35);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Incoming Passwd:</td>\n");
	cgi_print_text_input_td("IncomingPasswd", 20, iscsiconf.IncomingPasswd, 35);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Outgoing User:</td>\n");
	cgi_print_text_input_td("OutgoingUser", 20, iscsiconf.OutgoingUser, 35);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Outgoing Passwd:</td>\n");
	cgi_print_text_input_td("OutgoingPasswd", 20, iscsiconf.OutgoingPasswd, 35);
	printf("</tr>\n");

	printf("</table>\n");

	cgi_print_submit_button("updateiscsi", "Submit");

	cgi_print_div_end();

	cgi_print_form_end();

	cgi_print_div_trailer();
	cgi_print_body_trailer();

	return 0;
}

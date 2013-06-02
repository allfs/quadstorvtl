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
#include <vdevice.h>

int main()
{
	int i;

	__cgi_print_header("Add VTL", "addvtl.js", 0, NULL, 0, "fillinit()");

	cgi_print_thdr("Add VTL");

	cgi_print_form_start("addvtl", "addvtlseldrive.cgi", "post", 1);
	printf("<input type=\"hidden\" name=\"vtype\" value=\"%d\">\n", T_CHANGER);
	printf("<INPUT type=\"hidden\" name=\"ndrivetypes\" value=\"1\">\n");
	printf("<INPUT type=\"hidden\" name=\"ndrivetype0\" value=\"0\">\n");

	cgi_print_div_start("center");
	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<td>VTL Name</td>\n");
	cgi_print_text_input_td("lname", 15, "", TL_NAME_LEN);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Library Type</td>\n");
	printf("<td><select name=\"vselect\" onchange=\"fillvtldrivetypes();\"></select></td>\n");
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>VDrive Type</td>\n");
	printf("<td><select name=\"drivetype0\"></select></td>\n");
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Number of VDrives</td>\n");
	printf("<td>\n");
	printf("<select name=\"ndrives\">\n");
	for (i = 1; i <= TL_MAX_DRIVES; i++) {
		printf("<option value=\"%d\">%d</option>\n", i, i);
	}
	printf("</select>\n");
	printf("</td>\n");
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Number of VSlots</td>\n");
	printf("<td>\n");
	printf("<select name=\"slots\">\n");
	printf("<option value=\"20\">20</option>\n");
	printf("<option value=\"50\">50</option>\n");
	printf("<option value=\"100\">100</option>\n");
	printf("<option value=\"200\">200</option>\n");
	printf("</select>\n");
	printf("</td>\n");
	printf("</tr>\n");

	printf("</table>\n");

	cgi_print_submit_button("submit", "Submit");

	cgi_print_div_end();

	cgi_print_form_end();

	cgi_print_div_trailer();

	cgi_print_body_trailer();

	return 0;
}

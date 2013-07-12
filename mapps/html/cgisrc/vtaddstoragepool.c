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
	cgi_print_header("Add Storage Pool", "vtaddstoragepool.js", 0);

	cgi_print_thdr("Add Storage Pool");

	cgi_print_form_start("vtaddstoragepool", "vtaddstoragepoolpost.cgi", "post", 1);
	cgi_print_div_start("center");
	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<td>Pool Name</td>\n");
	printf("<td>");
	cgi_print_text_input("groupname", 20, "", TDISK_NAME_LEN);
	printf("</td>\n");
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>WORM: </td>\n");
	printf("<td>");
	cgi_print_checkbox_input("worm", 0);
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

/*
 * Copyleft (C) Shivaram Narsimha Murthy
 * All Rights Reserved
 */

#include <html-lib.h>
#include "cgimain.h"
#include <tlclntapi.h>
#include <physlib.h>
#include <vdevice.h>

int main()
{
	__cgi_print_header("Add VDrive", "vtaddvdrive.js", 0, NULL, 0, "filldrive()");

	cgi_print_thdr("Add VDrive");

	cgi_print_form_start("addvdrive", "addvdrivepost.cgi", "post", 1);

	cgi_print_div_start("center");
	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<td>VDrive Name</td>\n");
	cgi_print_text_input_td("name", 15, "", TL_NAME_LEN);
	printf("</tr>\n");

	printf("<input type=hidden name=vtype value=%d>\n", T_SEQUENTIAL);

	printf("<tr>\n");
	printf("<td>VDrive Type</td>\n");
	printf("<td><select name=\"drivetype\"></SELECT></td>\n");
	printf("</tr>\n");

	printf("</table>\n");

	cgi_print_submit_button("submit", "Submit");

	cgi_print_div_end();

	cgi_print_form_end();

	cgi_print_div_trailer();

	cgi_print_body_trailer();

	return 0;
}

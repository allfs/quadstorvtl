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

extern struct vtltype vtltypes[];
extern struct drivetype drivetypes[];
extern struct voltype voltypes[];

void
build_drivespec(llist entries, int drivetype, int ndrivetype, int start)
{
	int i;

	for (i = 0; i < ndrivetype; i++)
	{

		printf("<tr>\n");
		printf("<td>%d</td>\n", (start+i+1));
		printf("<td>drive%d</td>\n", (start+i+1));
		printf("<td>%s</td>\n", drivetypes[drivetype-1].name);
		printf("</tr>\n");
	}

	return;
}

int main()
{
	llist entries;
	char *tmp;
	char *name;
	int vtltype = 0;
	int slots;
	int ndrivetypes = 0;
	int i;
	int start;

	read_cgi_input(&entries);

	name = cgi_val(entries, "lname");

	if (!name || (strlen(name) == 0))
		cgi_print_header_error_page("No name specified for Virtual Library/Drive\n");

	tmp = cgi_val(entries, "vselect");
	if (!tmp || !(vtltype = atoi(tmp)))
		cgi_print_header_error_page("No virtual library/drive type specified");

	tmp = cgi_val(entries, "ndrivetypes");
	if ((!tmp || !(ndrivetypes = atoi(tmp))))
		cgi_print_header_error_page("Insufficient CGI parameters passed. No ndrivetypes passed");

	tmp = cgi_val(entries, "slots");
	if ((!tmp || !(slots = atoi(tmp))))
		cgi_print_header_error_page("Insufficient CGI parameters passed. VSlots not specified");

	cgi_print_header("Add VTL", "addvtlseldrive.js", 0);

	cgi_print_form_start("addvtl", "addvtlpost.cgi", "post", 0);
	cgi_print_thdr("VTL Configuration");

	printf("<input type=hidden name=lname value=\"%s\" />\n", name);
	printf("<input type=hidden name=vtype value=%d />\n", T_CHANGER);
	printf("<input type=hidden name=slots value=%d />\n", slots);
	printf("<input type=hidden name=ndrivetypes value=%d />\n", ndrivetypes);
	printf("<input type=hidden name=vselect value=%d />\n", vtltype);

	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<td>VTL Name</td>\n");
	printf("<td>%s</td>\n", name);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Library Type</td>\n");
	printf("<td>%s</td>\n", vtltypes[vtltype-1].name);
	printf("</tr>\n");

	printf("</table>\n");

	cgi_print_thdr("VDrive Configuration");

	cgi_print_div_start("center");
	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<th width=\"30px\">&nbsp</th>\n");
	printf("<th width=\"50px\">Name</th>\n");
	printf("<th>Type</th>\n");
	printf("</tr>\n");

	start = 0;
	for (i = 0; i < ndrivetypes && start < TL_MAX_DRIVES; i++)
	{
		char drivespec[20];
		int drivetype;
		int ndrivetype;

		sprintf(drivespec, "drivetype%d", i);
		tmp = cgi_val(entries, drivespec);
		if (!tmp || !(drivetype = atoi(tmp)))
		{
			cgi_print_error_page("Insufficent cgi parameters passed. Not able to get drivetype");
		}

		sprintf(drivespec, "ndrivetype%d", i);
		tmp = cgi_val(entries, drivespec);
		if (!tmp || !(ndrivetype = atoi(tmp)))
		{
			cgi_print_error_page("Insufficent cgi parameters passed. Not able to get parameter ndrivetype");
		}
		if ((start + ndrivetype) > TL_MAX_DRIVES)
			ndrivetype = (TL_MAX_DRIVES - ndrivetype);

		sprintf(drivespec, "drivetype%d", i);
		printf("<input type=hidden name=\"%s\" value=\"%d\" />\n", drivespec, drivetype);
		sprintf(drivespec, "ndrivetype%d", i);
		printf("<input type=hidden name=\"%s\" value=\"%d\" />\n", drivespec, ndrivetype);
		build_drivespec(entries, drivetype, ndrivetype, start);
		start += ndrivetype;
	}
	printf("</table>\n");

	if (start < TL_MAX_DRIVES) {
		printf("<input type=hidden name=\"max_drives\" value=\"%d\" />\n", (TL_MAX_DRIVES - start));
		printf("<div style=\"text-align: center;margin-top: 1px;margin-bottom: 15px;\"><input type=\"submit\" class=\"yui3-button\" name=\"submit\" value=\"Add VDrive(s)\" onclick=\"addvdrivetarget()\" /></div>\n");
	}

	printf("<div style=\"text-align: center;margin-top: 3px;margin-bottom: 3px;\">\n");
	printf("<input type=\"submit\" class=\"yui3-button\" name=\"submit\" value=\"Add VTL\" />\n");
	printf("<input type=\"button\" class=\"yui3-button\" name=\"Cancel\" value=\"Cancel\" onClick=\"document.location.href='addvtl.cgi';\"/>\n");
	printf("</div>\n");

	cgi_print_form_end();

	cgi_print_div_end();

	cgi_print_div_trailer();

	cgi_print_body_trailer();
	return 0;
}

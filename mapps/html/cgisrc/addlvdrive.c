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
	char spec[30];
	char *tmp;

	for (i = 0; i < ndrivetype; i++)
	{
		int worm = 0;

		sprintf(spec, "drivetype%d_%d_worm", drivetype, i);

		tmp = cgi_val(entries, spec);
		if (tmp)
		{
			worm = atoi(spec);
		}

		printf ("<input type=hidden name=%s value=%d>\n", spec, worm);
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
	int max_drives;

	read_cgi_input(&entries);

	name = cgi_val(entries, "lname");

	if (!name || (strlen(name) == 0))
		cgi_print_header_error_page("No name specified for Virtual Library/Drive\n");

	tmp = cgi_val(entries, "slots");
	if (!tmp || !(slots = atoi(tmp)))
		cgi_print_header_error_page("No value specified for VSlots");

	tmp = cgi_val(entries, "vselect");
	if (!tmp || !(vtltype = atoi(tmp)))
		cgi_print_header_error_page("No virtual library/drive type specified");

	tmp = cgi_val(entries, "ndrivetypes");
	if ((!tmp || !(ndrivetypes = atoi(tmp))))
		cgi_print_header_error_page("Insufficient CGI parameters passed. No ndrivetypes passed");

	tmp = cgi_val(entries, "max_drives");
	if (!tmp || !(max_drives = atoi(tmp)))
		cgi_print_header_error_page("Insufficient CGI parameters passed. Max drives not passed");
	
	__cgi_print_header("Additional VDrives", "vtaddlvdrive.js", 1, NULL, 0, "fillvtldrivetypes();");

	cgi_print_form_start("addlvdrive", "addvtlseldrive.cgi", "post", 1);
	printf ("<input type=hidden name=lname value=\"%s\">\n", name);
	printf ("<input type=hidden name=vtype value=%d>\n", T_CHANGER);
	printf ("<input type=hidden name=slots value=%d>\n", slots);
	printf ("<input type=hidden name=ndrivetypes value=%d>\n", ndrivetypes);
	printf ("<input type=hidden name=vselect value=%d>\n", vtltype);

	start = 0;
	for (i =0; i < ndrivetypes; i++) {
		char drivespec[20];
		int drivetype;
		int ndrivetype;

		sprintf(drivespec, "drivetype%d", i);
		tmp = cgi_val(entries, drivespec);

		if (!tmp || !(drivetype = atoi(tmp)))
			cgi_print_error_page("Insufficent cgi parameters passed. Not able to get drivetype");

		printf("<input type=hidden name=\"%s\" value=\"%s\">\n", drivespec, tmp);

		sprintf(drivespec, "ndrivetype%d", i);
		tmp = cgi_val(entries, drivespec);
		if (!tmp || !(ndrivetype = atoi(tmp)))
			cgi_print_error_page("Insufficent cgi parameters passed. Not able to get parameter ndrivetype");

		printf("<input type=hidden name=\"%s\" value=\"%s\">\n", drivespec, tmp);
#if 0
		build_drivespec(entries, drivetype, ndrivetype, start);
#endif
		start += ndrivetype;
	}
	printf ("<input type=hidden name=drivetype%d value=0>\n", ndrivetypes);
	printf ("<input type=hidden name=ndrivetype%d value=0>\n", ndrivetypes);

	cgi_print_thdr("Additional VDrives");
	printf("<table class=\"ctable\">\n");

	printf ("<tr>\n");
	printf ("<td>VDrive Type</td>\n");
	printf ("<td><SELECT name=\"driveselect\"></SELECT></td>\n");
	printf ("</tr>\n");

	printf("<tr>\n");
	printf("<td>Number of VDrives</td>\n");
	printf("<td>\n");
	printf("<select name=\"ndrives\">\n");
	for (i = 1; i <= max_drives; i++) {
		printf("<option value=\"%d\">%d</option>\n", i, i);
	}
	printf("</select>\n");
	printf("</td>\n");
	printf("</tr>\n");
	printf("</table>\n");

	printf("<div style=\"text-align: center;margin-top: 3px;margin-bottom: 3px;\">\n");
	printf("<input type=\"submit\" class=\"yui3-button\" name=\"submit\" value=\"Submit\" />\n");
	printf("<input type=\"button\" class=\"yui3-button\" name=\"Cancel\" value=\"Cancel\" onClick=\"history.go(-1)\" />\n");
	printf("</div>\n");
	cgi_print_form_end();

	cgi_print_div_trailer();

	cgi_print_body_trailer();

	return 0;
}

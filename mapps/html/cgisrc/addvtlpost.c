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
build_drivespec(llist entries, FILE *fp, int drivetype, int ndrivetype, int start)
{
	int i;
#if 0
	char spec[30];
	char *tmp;
#endif

	for (i = 0; i < ndrivetype; i++)
	{

		fprintf(fp, "<drive>\n"); 
		fprintf(fp, "name: drive%d\n", start+i);
		fprintf(fp, "type: %d\n", drivetype);
		fprintf(fp, "</drive>\n");
	}

	return;
}

int main()
{
	llist entries;
	char *tmp;
	char *name;
	int vtltype = 0;
	int slots = 0;
	int ndrivetypes = 0;
	int i;
	int start;
	FILE *fp;
	int fd;
	char tempfile[50];
	char reply[512];
	char cmd[256];
	int tl_id;
	int retval;

	read_cgi_input(&entries);

	name = cgi_val(entries, "lname");

	if (!name || (strlen(name) == 0))
	{
		cgi_print_header_error_page("No name specified for Virtual Library/Drive\n");
	}

	tmp = cgi_val(entries, "vselect");
	if (!tmp || !(vtltype = atoi(tmp)))
	{
		DEBUG_INFO("tmp is %s\n", tmp);
		cgi_print_header_error_page("No virtual library/drive type specified");
	}

	tmp = cgi_val(entries, "ndrivetypes");
	if ((!tmp || !(ndrivetypes = atoi(tmp))))
	{
		cgi_print_header_error_page("Insufficient CGI parameters passed. No ndrivetypes passed");
	}

	tmp = cgi_val(entries, "slots");
	if ((!tmp || !(slots = atoi(tmp))))
	{
		cgi_print_header_error_page("Insufficent CGI parameters. VSlots not specified");
	}

	strcpy(tempfile, MSKTEMP_PREFIX);
	fd = mkstemp(tempfile);
	if (fd == -1)
	{
		cgi_print_header_error_page("Internal processing error\n");
	}
	close(fd);

	fp = fopen(tempfile, "w");
	if (!fp)
	{
		remove(tempfile);
		cgi_print_header_error_page("Internal processing error\n");
	}

	fprintf(fp, "<vtlconf>\n");
	fprintf(fp, "name: %s\n", name);
	fprintf(fp, "slots: %d\n", slots);
	fprintf(fp, "type: %d\n", vtltype);

	start = 0;
	for (i =0; i < ndrivetypes; i++)
	{
		char drivespec[20];
		int drivetype = 0;
		int ndrivetype = 0;

		sprintf(drivespec, "drivetype%d", i);
		tmp = cgi_val(entries, drivespec);
		if (!tmp || !(drivetype = atoi(tmp)))
		{
			remove(tempfile);
			cgi_print_header_error_page("Insufficent cgi parameters passed. Not able to get drivetype");
		}

		sprintf(drivespec, "ndrivetype%d", i);
		tmp = cgi_val(entries, drivespec);
		if (!tmp || !(ndrivetype = atoi(tmp)))
		{
			remove(tempfile);
			cgi_print_header_error_page("Insufficent cgi parameters passed. Not able to get parameter ndrivetype");
		}
		build_drivespec(entries, fp, drivetype, ndrivetype, start);
		start += ndrivetype;
	}

	fclose(fp);
	retval = tl_client_add_vtl_conf(tempfile, &tl_id, reply);
	remove(tempfile);

	if (retval != 0)
	{
		char errmsg[1024];
		sprintf(errmsg, "Unable to add new Virtual Library/Drive.<br>Message from server is:<br>\"%s\"\n", reply);
		cgi_print_header_error_page(errmsg);
	}

	DEBUG_INFO("Ret val got tl_id %d\n", tl_id);
	if (tl_id < 0)
	{
		cgi_print_header_error_page("Internal processing error");
	}

	sprintf(cmd, "indvvtl.cgi?tl_id=%d", tl_id);
	cgi_redirect(cmd);
	return 0;
}

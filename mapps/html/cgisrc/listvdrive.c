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

int main()
{
	FILE *fp;
	char tempfile[100];
	char buf[256];
	int fd;
	int retval;
	struct tdriveconf *driveconf;
	char *cols[] = {"{ key: 'Name', sortable: true}", "{ key: 'Type', sortable: true}", "{ key: 'View', allowHTML: true }", NULL};

	strcpy(tempfile, MSKTEMP_PREFIX);
	fd = mkstemp(tempfile);
	if (fd == -1)
		cgi_print_header_error_page("Internal processing error\n");

	close(fd);

	retval = tl_client_list_vtls(tempfile);
	if (retval != 0) {
		remove(tempfile);
		cgi_print_header_error_page("Getting VTL list failed\n");
	}

	fp = fopen(tempfile, "r");
	if (!fp) {
		remove(tempfile);
		cgi_print_header_error_page("Internal processing error\n");
	}

	cgi_print_header("Standalone Virtual Drives", NULL, 1);

	cgi_print_thdr("Configured Virtual Drives");

	cgi_print_table_div("vtls-table");

	cgi_print_div_start("center");
	cgi_print_form_start("addvdrive", "addvdrive.cgi", "post", 0);
	cgi_print_submit_button("submit", "Add VDrive");
	cgi_print_form_end();
	cgi_print_div_end();

	cgi_print_div_trailer();

	cgi_print_table_start("vtls-table", cols, 1);

	while (fgets(buf, sizeof(buf), fp) != 0) {
		struct vdevice *vdevice;
		if (strcmp(buf, "<vdevice>\n"))
			goto err;

		vdevice = parse_vdevice(fp);
		if (!vdevice)
			continue;

		if (vdevice->type != T_SEQUENTIAL)
			continue;

		cgi_print_row_start();

		driveconf = (struct tdriveconf *)(vdevice);

		cgi_print_column("Name", vdevice->name);
		cgi_print_comma();

		cgi_print_column("Type", drivetypes[driveconf->type - 1].name);
		cgi_print_comma();

		cgi_print_column_format("View", "<a href=indvvdrive.cgi?tl_id=%d>View</a>", vdevice->tl_id);

		cgi_print_row_end();
		free(vdevice);
	}

err:
	fclose(fp);
	remove(tempfile);
	cgi_print_table_end("vtls-table");

	cgi_print_body_trailer();

	return 0;
}


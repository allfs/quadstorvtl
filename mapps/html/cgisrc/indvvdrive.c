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

int vtypes_arr[16];
int ntypes;

void
add_drivetype(int drivetype)
{
	int voltype;
	int i;

	voltype = get_voltype(drivetype);

	for (i = 0; i < ntypes; i++)
	{
		if (vtypes_arr[i] == voltype)
		{
			return;
		}
	}

	vtypes_arr[ntypes] = voltype;
	ntypes++;
}

int main()
{
	llist entries;
	char *tmp;
	int tl_id;
	int retval;
	struct vdevice *vdevice;
	struct tdriveconf *driveconf;
	struct vcartridge *vcartridge;
	char buf[256];
	char tempfile[100];
	int fd, i;
	FILE *fp;
	char *cols1[] = {"Pool", "Label", "VType", "WORM", "Size", "Used", "Status", "{ key: 'LoadUnload', label: 'Load/Unload', allowHTML: true }", "{ key: 'Delete', label: 'Delete', allowHTML: true }", NULL};

	read_cgi_input(&entries);

	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid cgi input parameters passed\n");

	tl_id = atoi(tmp);

	strcpy(tempfile, "/tmp/.quadstorindvvtl.XXXXXX");
	fd = mkstemp(tempfile);
	if (fd == -1)
		cgi_print_header_error_page("Internal processing error\n");

	close(fd);

	retval = tl_client_vtl_info(tempfile, tl_id, MSG_ID_GET_VTL_CONF);
	if (retval) {
		remove(tempfile);
		cgi_print_header_error_page("Unable to get VDrive Configuration\n");
	}

	fp = fopen(tempfile, "r");
	if (!fp) {
		remove(tempfile);
		cgi_print_header_error_page("Internal processing error\n");
	}

	fgets(buf, sizeof(buf), fp);
	vdevice = parse_vdevice(fp);
	fclose(fp);
	remove(tempfile);
	if (!vdevice)
		cgi_print_header_error_page("Internal processing error\n");

	driveconf = (struct tdriveconf *)vdevice;
	add_drivetype(driveconf->type);

	cgi_print_header("VDrive Information", NULL, 0);

	cgi_print_thdr("VDrive Information");

	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<td>VDrive Type</td>\n");
	printf("<td>%s</td>\n", drivetypes[driveconf->type - 1].name);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>VDrive Name</td>\n");
	printf("<td>%s</td>\n", vdevice->name);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Serial Number</td>\n");
	printf("<td>%s</td>\n", vdevice->serialnumber);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>VCartridge</td>\n");
	printf("<td>%s</td>\n", driveconf->tape_label);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>iSCSI</td>\n");
	printf("<td><a href=\"iscsiconf.cgi?tl_id=%d&target_id=0&vtltype=%d\">View</a></td>\n", tl_id, T_SEQUENTIAL);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Statistics</td>\n");
	printf("<td><a href=\"vdrivestats.cgi?tl_id=%d&target_id=0\">View</a></td>\n", tl_id);
	printf("</tr>\n");

	printf("</table>\n");

	printf("<form action=\"deletevtl.cgi\" method=\"post\" onSubmit=\"return confirm('Delete VDrive %s ?');\">\n", vdevice->name);
	printf("<input type=\"hidden\" name=\"tl_id\" value=\"%d\">\n", tl_id);
	printf("<input type=\"hidden\" name=\"vtltype\" value=\"%d\">\n", T_SEQUENTIAL);
	cgi_print_submit_button("submit", "Delete VDrive");
	cgi_print_form_end();

	cgi_print_thdr("VCartridge Information");
	if (TAILQ_EMPTY(&vdevice->vol_list)) {
		cgi_print_div_start("center");
		cgi_print_paragraph("None");
		cgi_print_div_end();
	}
	else {
		cgi_print_table_div("vcartridges-table");
	}

	cgi_print_div_start("center");
	cgi_print_form_start("addvcartridge", "addvcartridge.cgi", "post", 0);
	printf("<input type=\"hidden\" name=\"tl_id\" value=\"%u\">\n", tl_id);
	printf("<input type=\"hidden\" name=\"vtlname\" value=\"%s\">\n", vdevice->name);
	printf("<input type=\"hidden\" name=\"vtltype\" value=\"%d\">\n", T_SEQUENTIAL);
	printf("<input type=\"hidden\" name=\"nvoltypes\" value=\"%d\">\n", ntypes);
	for (i = 0; i < ntypes; i++) {
		printf("<input type=\"hidden\" name=\"vtype%d\" value=\"%d\">\n", i, vtypes_arr[i]);
	}
	cgi_print_submit_button("submit", "Add VCartridge");
	cgi_print_form_end();
	cgi_print_div_end();

	cgi_print_div_trailer();

	if (TAILQ_EMPTY(&vdevice->vol_list))
		goto skip_vcartridge;

	cgi_print_table_start("vcartridges-table", cols1, 1);
	TAILQ_FOREACH(vcartridge, &vdevice->vol_list, q_entry) {
		cgi_print_row_start();
		cgi_print_column_format("Pool", "%s", vcartridge->group_name);
		cgi_print_comma();
		cgi_print_column_format("Label", "%s", vcartridge->label);
		cgi_print_comma();
		cgi_print_column_format("VType", "%s", voltypes[vcartridge->type - 1].name);
		cgi_print_comma();
		if (vcartridge->worm)
			cgi_print_column("WORM", "Yes");
		else
			cgi_print_column("WORM", "No");

		cgi_print_comma();
		cgi_print_column_format("Size", "%llu", (unsigned long long)(vcartridge->size/(1024 * 1024 * 1024)));
		cgi_print_comma();
		cgi_print_column_format("Used", "%d%%", (int)usage_percentage(vcartridge->size, vcartridge->used));
		cgi_print_comma();

		if (vcartridge->loaderror)
			cgi_print_column("Status", "Load Error");
		else if (vcartridge->vstatus & MEDIA_STATUS_EXPORTED)
			cgi_print_column("Status", "Exported");
		else
			cgi_print_column("Status", "Active");
		cgi_print_comma();

		if (strcmp(vcartridge->label, driveconf->tape_label))
			cgi_print_column_format("LoadUnload", "<a href=\"loadvdrive.cgi?tid=%d&tl_id=%d&msg_id=%d\">Load</a>", vcartridge->tape_id, tl_id, MSG_ID_LOAD_DRIVE);
		else
			cgi_print_column_format("LoadUnload", "<a href=\"loadvdrive.cgi?tid=%d&tl_id=%d&msg_id=%d\">Unload</a>", vcartridge->tape_id, tl_id, MSG_ID_UNLOAD_DRIVE);
		cgi_print_comma();

		cgi_print_column_format("Delete", "<a href=\"deletevcartridge.cgi?tl_id=%u&tape_id=%u&vtltype=%d\"  onclick=\\'return confirm(\\\"Delete VCartrdige %s?\\\");\\'><img src=\"/quadstor/delete.png\" width=16px height=16px border=0></a>", tl_id, vcartridge->tape_id, T_SEQUENTIAL, vcartridge->label);

		cgi_print_row_end();
	}
	cgi_print_table_end("vcartridges-table");

skip_vcartridge:
	cgi_print_body_trailer();

	free_vdevice(vdevice);

	return 0;
}

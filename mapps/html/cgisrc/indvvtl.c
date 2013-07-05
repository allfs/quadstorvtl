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
	struct vtlconf *vtlconf;
	struct tdriveconf *dconf;
	struct vcartridge *vcartridge;
	char buf[256];
	char tempfile[100];
	int fd, i;
	FILE *fp;
	char *cols[] = {"Name", "{ key: 'DType', label: 'Drive Type'}", "Name", "{ key: 'Serial', label: 'Serial Number'}", "VCartridge", "{ key: 'iSCSI', label: 'iSCSI', allowHTML: true }", "{ key: 'Statistics', label: 'Statistics', allowHTML: true }", NULL};
	char *cols1[] = {"Pool", "Label", "{key: 'Element', sortable: true }", "Address", "{ key: 'VType', label: 'VCart Type', sortable: true }", "WORM", "Size", "Used", "Status", "{key: 'Reload', label: 'Reload', allowHTML: true}", "{ key: 'Delete', label: 'Delete', allowHTML: true }", NULL};

	read_cgi_input(&entries);

	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
	{
		DEBUG_INFO("cgi_val not able get tl_id");
		cgi_print_header_error_page("Invalid cgi input parameters passed\n");
	}

	tl_id = atoi(tmp);

	strcpy(tempfile, "/tmp/.quadstorindvvtl.XXXXXX");
	fd = mkstemp(tempfile);
	if (fd == -1)
	{
		cgi_print_header_error_page("Internal processing error\n");
	}
	close(fd);

	retval = tl_client_vtl_info(tempfile, tl_id, MSG_ID_VTL_INFO);
	if (retval)
	{
		remove(tempfile);
		cgi_print_header_error_page("Unable to get VTL Configuration\n");
	}

	fp = fopen(tempfile, "r");
	if (!fp)
	{
		remove(tempfile);
		cgi_print_header_error_page("Internal processing error\n");
	}

	fgets(buf, sizeof(buf), fp);
	vdevice = parse_vdevice(fp);
	fclose(fp);
	remove(tempfile);
	if (!vdevice)
	{
		cgi_print_header_error_page("Internal processing error\n");
	}

	vtlconf = (struct vtlconf *)vdevice;

	cgi_print_header("VTL Information", NULL, 0);

	cgi_print_thdr("VTL Information");

	printf("<table class=\"ctable\">\n");

	printf("<tr>\n");
	printf("<td>VTL Type</td>\n");
	printf("<td>%s</td>\n", vtltypes[vtlconf->type - 1].name);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>VTL Name</td>\n");
	printf("<td>%s</td>\n", vdevice->name);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Serial Number</td>\n");
	printf("<td>%s</td>\n", vdevice->serialnumber);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>Slots</td>\n");
	printf("<td>%d</td>\n", vtlconf->slots);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>I/E Ports</td>\n");
	printf("<td>%d</td>\n", vtlconf->ieports);
	printf("</tr>\n");

	printf("<tr>\n");
	printf("<td>iSCSI</td>\n");
	printf("<td><a href=\"iscsiconf.cgi?tl_id=%d&target_id=0&vtltype=%d\">View</a></td>\n", tl_id, T_CHANGER);
	printf("</tr>\n");

	printf("</table>\n");

	printf("<form action=\"deletevtl.cgi\" method=\"post\" onSubmit=\"return confirm('Delete VTL %s ?');\">\n", vdevice->name);
	printf("<input type=\"hidden\" name=\"tl_id\" value=\"%d\">\n", tl_id);
	printf("<input type=\"hidden\" name=\"vtltype\" value=\"%d\">\n", T_CHANGER);
	cgi_print_submit_button("submit", "Delete VTL");
	cgi_print_form_end();

	TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) {
		add_drivetype(dconf->type);
	}

	cgi_print_thdr("VDrive Information");
	if (TAILQ_EMPTY(&vtlconf->drive_list)) {
		cgi_print_div_start("center");
		cgi_print_paragraph("None");
		cgi_print_div_end();
	}
	else {
		cgi_print_table_div("vtl-drives-table");
	}

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
	printf("<input type=\"hidden\" name=\"vtltype\" value=\"%d\">\n", T_CHANGER);
	printf("<input type=\"hidden\" name=\"nvoltypes\" value=\"%d\">\n", ntypes);
	for (i = 0; i < ntypes; i++) {
		printf("<input type=\"hidden\" name=\"vtype%d\" value=\"%d\">\n", i, vtypes_arr[i]);
	}
	cgi_print_submit_button("submit", "Add VCartridge");
	cgi_print_form_end();
	cgi_print_div_end();

	cgi_print_div_trailer();

	if (TAILQ_EMPTY(&vtlconf->drive_list))
		goto skip_drives;

	cgi_print_table_start("vtl-drives-table", cols, 1);
	TAILQ_FOREACH(dconf, &vtlconf->drive_list, q_entry) {
		cgi_print_row_start();
		cgi_print_column_format("Name", "%s", dconf->vdevice.name);
		cgi_print_comma();
		cgi_print_column_format("DType", "%s", drivetypes[dconf->type - 1].name);
		cgi_print_comma();
		cgi_print_column_format("Name", "%s", dconf->vdevice.name);
		cgi_print_comma();
		cgi_print_column_format("Serial", "%s", dconf->vdevice.serialnumber);
		cgi_print_comma();
		cgi_print_column_format("VCartridge", "%s", dconf->tape_label);
		cgi_print_comma();

		cgi_print_column_format("iSCSI", "<a href=\"iscsiconf.cgi?tl_id=%d&target_id=%u&vtltype=%d\">View</a>", vdevice->tl_id, dconf->vdevice.target_id, T_CHANGER);
		cgi_print_comma();
		cgi_print_column_format("Statistics", "<a href=\"vdrivestats.cgi?tl_id=%d&target_id=%u\">View</a>", vdevice->tl_id, dconf->vdevice.target_id);

		cgi_print_row_end();
	}
	cgi_print_table_end("vtl-drives-table");

skip_drives:
	if (TAILQ_EMPTY(&vdevice->vol_list))
		goto skip_vcartridge;

	cgi_print_table_start("vcartridges-table", cols1, 1);
	TAILQ_FOREACH(vcartridge, &vdevice->vol_list, q_entry) {
		cgi_print_row_start();
		cgi_print_column_format("Pool", "%s", vcartridge->group_name);
		cgi_print_comma();
		cgi_print_column_format("Label", "%s", vcartridge->label);
		cgi_print_comma();
		cgi_print_column("Element", get_element_type_str(vcartridge->elem_type));
		cgi_print_comma();
		cgi_print_column_format("Address", "%d", vcartridge->elem_address);
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

		if (vcartridge->vstatus & MEDIA_STATUS_EXPORTED)
			cgi_print_column_format("Reload", "<a href=\"reloadexp.cgi?tape_id=%u&tl_id=%d\">Reload</a>", vcartridge->tape_id, tl_id);
		else
			cgi_print_column("Reload", "N/A");
		cgi_print_comma();

		cgi_print_column_format("Delete", "<a href=\"deletevcartridge.cgi?tl_id=%u&tape_id=%u&vtltype=%d\"  onclick=\\'return confirm(\\\"Delete VCartrdige %s?\\\");\\'><img src=\"/quadstor/delete.png\" width=16px height=16px border=0></a>", tl_id, vcartridge->tape_id, T_CHANGER, vcartridge->label);
		cgi_print_row_end();
	}
	cgi_print_table_end("vcartridges-table");

skip_vcartridge:
	cgi_print_body_trailer();

	free_vdevice(vdevice);

	return 0;
}

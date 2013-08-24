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
	llist entries;
	char tempfile[40];
	char alias[512];
	int fd;
	int retval;
	FILE *fp;
	struct d_list configured_dlist;
	struct physdisk *disk;
	char databuf[64];
	char *name;
	uint32_t group_id;
	char *tmp;
	char *cols[] = {"ID", "Vendor", "Model", "{ key: 'Serial', label: 'Serial Number'}", "Name", "Size", "Used", "Status", NULL};

	read_cgi_input(&entries);

	tmp = cgi_val(entries, "group_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters passed\n");

	name = cgi_val(entries, "name");
	if (!name)
		cgi_print_header_error_page("Invalid CGI parameters passed\n");

	group_id = atoi(tmp);

	strcpy(tempfile, MSKTEMP_PREFIX);
	fd = mkstemp(tempfile);
	if (fd == -1)
		cgi_print_header_error_page("Internal processing error\n");

	retval = tl_client_list_target_generic(group_id, tempfile, MSG_ID_GET_POOL_CONFIGURED_DISKS);
	if (retval != 0) {
		remove(tempfile);
		cgi_print_header_error_page("Getting configured storage list failed\n");
	}

	fp = fopen(tempfile, "r");
	if (!fp) {
		remove(tempfile);
		cgi_print_header_error_page("Internal processing error\n");
	}

	TAILQ_INIT(&configured_dlist);
	retval = tl_common_parse_physdisk(fp, &configured_dlist);
	fclose(fp);
	if (retval != 0) {
		remove(tempfile);
		cgi_print_header_error_page("Unable to get configured disk list\n");
	}

	close(fd);
	remove(tempfile);

	__cgi_print_header("Storage Pools", "vtmodifystoragepool.js", 1, NULL, 0, NULL);

	if (!group_id)
		goto skip_rename;

	cgi_print_form_start("vtmodifystoragepool", "vtmodifystoragepool.cgi", "post", 1);
	printf("<input type=\"hidden\" name=\"group_id\" value=\"%u\">\n", group_id);
	cgi_print_thdr("Rename Pool");
	cgi_print_div_start("center");
	printf("<table class=\"ctable\">\n");
	printf("<tr>\n");
	printf("<td>Pool Name:</td>\n");
	printf("<td>");
	cgi_print_text_input("groupname", 15, name, TDISK_NAME_LEN);
	printf("</td>\n");
	printf("</tr>\n");
	printf("</table>\n");
	cgi_print_submit_button("submit", "Submit");
	cgi_print_div_end();
	cgi_print_form_end();

skip_rename:
	cgi_print_thdr("Configured Disks");
	if (TAILQ_EMPTY(&configured_dlist)) {
		cgi_print_div_start("center");
		cgi_print_paragraph("None");
		cgi_print_div_end();
	}
	else {
		cgi_print_table_div("pool-disks-table");
	}

	cgi_print_div_trailer();

	if (TAILQ_EMPTY(&configured_dlist))
		goto skip;

	cgi_print_table_start("pool-disks-table", cols, 1);

	TAILQ_FOREACH(disk, &configured_dlist, q_entry) {
		cgi_print_row_start();
		cgi_print_column_format("ID", "%u", disk->bid); 
		cgi_print_comma();

		cgi_print_column_format("Vendor", "%.8s", disk->info.vendor);
		cgi_print_comma();

		cgi_print_column_format("Model", "%.16s", disk->info.product);
		cgi_print_comma();

		cgi_print_column_format("Serial", "%.32s", disk->info.serialnumber);
		cgi_print_comma();

		if (disk->info.multipath)
			device_get_alias(disk->info.mdevname, alias);
		else
			device_get_alias(disk->info.devname, alias);

		cgi_print_column_format("Name", "%.32s", alias);
		cgi_print_comma();

		if (disk->info.online) {
			get_data_str(disk->size, databuf);
			cgi_print_column("Size", databuf);
			cgi_print_comma();

			get_data_str(disk->used, databuf);
			cgi_print_column("Used", databuf);
			cgi_print_comma();

			cgi_print_column("Status", "online");
			cgi_print_comma();
		}
		else {
			cgi_print_column("Used", "N/A");
			cgi_print_comma();

			cgi_print_column("Status", "offline");
			cgi_print_comma();
		}


		cgi_print_row_end();
	}
	cgi_print_table_end("pool-disks-table");

skip:
	cgi_print_body_trailer();

	disk_free_all(&configured_dlist);
	return 0;
}	


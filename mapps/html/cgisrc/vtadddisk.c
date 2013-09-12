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
	int retval;
	struct d_list dlist;
	struct d_list configured_dlist;
	struct physdisk *disk;
	char alias[512];
	char databuf[64];
	char *cols[] = {"ID", "Vendor", "Model", "{ key: 'Serial', label: 'Serial Number'}", "Name", "{key: 'Pool', label: 'Pool', sortable: true}", "Size", "Used", "{ key: 'Add', label: ' ', allowHTML: true }", NULL};


	TAILQ_INIT(&dlist);
	TAILQ_INIT(&configured_dlist);
	retval = tl_client_list_disks(&configured_dlist, MSG_ID_GET_CONFIGURED_DISKS);
	if (retval != 0)
		cgi_print_header_error_page("Unable to get configured disk list\n");

	retval = tl_client_list_disks(&dlist, MSG_ID_LIST_DISKS);
	if (retval != 0)
		cgi_print_header_error_page("Unable to get disk list\n");

	__cgi_print_header("Physical Storage", NULL, 1, NULL, 0, NULL);

	cgi_print_thdr("Physical Storage");
	if (TAILQ_EMPTY(&dlist) && TAILQ_EMPTY(&configured_dlist)) {
		cgi_print_div_start("center");
		cgi_print_paragraph("None");
		cgi_print_div_end();
	}
	else {
		cgi_print_table_div("disks-table");
	}

	cgi_print_div_start("center");
	cgi_print_form_start("vtrescandisk", "vtrescandisk.cgi", "post", 0);
	cgi_print_submit_button("submit", "Rescan");
	cgi_print_form_end();
	cgi_print_div_end();

	cgi_print_div_trailer();

	if (TAILQ_EMPTY(&dlist) && TAILQ_EMPTY(&configured_dlist))
		goto skip;

	cgi_print_table_start("disks-table", cols, 1);

	TAILQ_FOREACH(disk, &dlist, q_entry) {
		struct physdisk *config;

		config = disk_configured(disk, &configured_dlist);

		cgi_print_row_start();

		if (!config)
			cgi_print_column_format("ID", "N/A");
		else
			cgi_print_column_format("ID", "%u", config->bid);
		cgi_print_comma();

		cgi_print_column_format("Vendor", "%.8s", disk->info.vendor);
		cgi_print_comma();

		cgi_print_column_format("Model", "%.16s", disk->info.product);
		cgi_print_comma();

		sprintf(databuf, "Serial: '%%.%ds'", disk->info.serial_len);
		printf(databuf, disk->info.serialnumber);
		cgi_print_comma();

		if (disk->info.multipath)
			device_get_alias(disk->info.mdevname, alias);
		else
			device_get_alias(disk->info.devname, alias);

		cgi_print_column_format("Name", "%.32s", alias);
		cgi_print_comma();

		if (!config)
			cgi_print_column("Pool", "N/A");
		else
			cgi_print_column("Pool", config->group_name);
		cgi_print_comma();

		get_data_str(disk->size, databuf);
		cgi_print_column("Size", databuf);
		cgi_print_comma();

		if (!config) {
			cgi_print_column("Used", "N/A");
			cgi_print_comma();

			cgi_print_column("Modify", "N/A");
			cgi_print_comma();

			cgi_print_column_format("Add", "<a href=\"vtadddiskcomp.cgi?dev=%s&op=1\">Add</a>", disk->info.devname);
		}
		else
		{
			get_data_str(config->used, databuf);
			cgi_print_column("Used", databuf);
			cgi_print_comma();

			cgi_print_column_format("Modify", "<a href=\"modifydisk.cgi?bid=%u\">Modify</a>", config->bid);
			cgi_print_comma();

			cgi_print_column_format("Add", "<a href=\"vtadddiskpost.cgi?dev=%s&op=2\">Remove</a>", disk->info.devname); 
		}
		cgi_print_row_end();
	}

	TAILQ_FOREACH(disk, &configured_dlist, q_entry) {
		if (disk->info.online)
			continue;

		cgi_print_row_start();
		cgi_print_column_format("ID", "%u", disk->bid); 
		cgi_print_comma();

		cgi_print_column_format("Vendor", "%.8s", disk->info.vendor);
		cgi_print_comma();

		cgi_print_column_format("Model", "%.16s", disk->info.product);
		cgi_print_comma();

		cgi_print_column_format("Serial", "%.32s", disk->info.serialnumber);
		cgi_print_comma();

		cgi_print_column("Name", "N/A");
		cgi_print_comma();

		cgi_print_column("Pool", disk->group_name);
		cgi_print_comma();

		cgi_print_column("Size", "N/A");
		cgi_print_comma();

		cgi_print_column("Used", "N/A");
		cgi_print_comma();

		cgi_print_column("Add", "&nbsp;");

		cgi_print_row_end();
	}
	cgi_print_table_end("disks-table");

skip:
	cgi_print_body_trailer();

	disk_free_all(&configured_dlist);
	disk_free_all(&dlist);
	return 0;
}	


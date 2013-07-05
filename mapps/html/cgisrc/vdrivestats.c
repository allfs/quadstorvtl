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
	int retval;
	uint32_t tl_id;
	uint32_t target_id;
	struct tdrive_stats stats;
	char *tmp;
	char databuf[64];
	uint64_t transfer_rate;
	char *cols[] = {"name", "value", NULL};

	read_cgi_input(&entries);

	tmp = cgi_val(entries, "tl_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters passed\n");

	tl_id = strtoul(tmp, NULL, 10);

	tmp = cgi_val(entries, "target_id");
	if (!tmp)
		cgi_print_header_error_page("Invalid CGI parameters passed\n");

	target_id = strtoul(tmp, NULL, 10);

	retval = tl_client_get_vdrive_stats(tl_id, target_id, &stats);
	if (retval != 0)
		cgi_print_header_error_page("Getting vdrive stats failed\n");

	cgi_print_header("VDrive Statistics", NULL, 1);

	cgi_print_thdr("VDrive Statisics");

	cgi_print_table_div("vstats-table");

	cgi_print_form_start("resetstats", "resetvdrivestats.cgi", "post", 1);
	printf ("<input type=\"hidden\" name=\"tl_id\" value=\"%u\">\n", tl_id);
	printf ("<input type=\"hidden\" name=\"target_id\" value=\"%u\">\n", target_id);
	cgi_print_submit_button("submit", "Reset");
	cgi_print_form_end();

	cgi_print_div_trailer();

	cgi_print_table_start("vstats-table", cols, 0);

	cgi_print_row_start();
	cgi_print_column("name", "Compression:");
	cgi_print_comma();
	if (stats.compression_enabled)
		cgi_print_column("value", "Enabled");
	else
		cgi_print_column("value", "Disabled");
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Load count:");
	cgi_print_comma();
	cgi_print_column_format("value", "%u", stats.load_count);
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Write errors:");
	cgi_print_comma();
	cgi_print_column_format("value", "%u", stats.write_errors);
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Read errors:");
	cgi_print_comma();
	cgi_print_column_format("value", "%u", stats.read_errors);
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Write bytes processed:");
	cgi_print_comma();
	get_data_str(stats.write_bytes_processed, databuf);
	cgi_print_column("value", databuf);
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Bytes written to tape:");
	cgi_print_comma();
	get_data_str(stats.bytes_written_to_tape, databuf);
	cgi_print_column("value", databuf);
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Compressed bytes written:");
	cgi_print_comma();
	get_data_str(stats.compressed_bytes_written, databuf);
	cgi_print_column("value", databuf);
	cgi_print_row_end();

	DEBUG_INFO_NEW("write bytes processed %lu write ticks %lu\n", stats.write_bytes_processed, stats.write_ticks);
	if (stats.write_ticks)
		transfer_rate = ((stats.write_bytes_processed * 1000) / stats.write_ticks);
	else
		transfer_rate = 0;
	get_data_str(transfer_rate, databuf);
	cgi_print_row_start();
	cgi_print_column("name", "Write Transfer Rate:");
	cgi_print_comma();
	cgi_print_column_format("value", "%s/s", databuf);
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Read bytes processed:");
	cgi_print_comma();
	get_data_str(stats.read_bytes_processed, databuf);
	cgi_print_column("value", databuf);
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Bytes read from tape:");
	cgi_print_comma();
	get_data_str(stats.bytes_read_from_tape, databuf);
	cgi_print_column("value", databuf);
	cgi_print_row_end();

	cgi_print_row_start();
	cgi_print_column("name", "Compressed bytes read:");
	cgi_print_comma();
	get_data_str(stats.compressed_bytes_read, databuf);
	cgi_print_column("value", databuf);
	cgi_print_row_end();

	if (stats.read_ticks)
		transfer_rate = ((stats.read_bytes_processed * 1000) / stats.read_ticks);
	else
		transfer_rate = 0;
	get_data_str(transfer_rate, databuf);
	cgi_print_row_start();
	cgi_print_column("name", "Read Transfer Rate:");
	cgi_print_comma();
	cgi_print_column_format("value", "%s/s", databuf);
	cgi_print_row_end();

	cgi_print_table_end("vstats-table");

	cgi_print_body_trailer();
	return 0;
}

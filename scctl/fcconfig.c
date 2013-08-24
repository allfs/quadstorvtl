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

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <tlclntapi.h>

static void
print_usage(void)
{
	fprintf(stdout, "fcconfig usage: \n");
	fprintf(stdout, "fconfig -l lists configured rules\n");
	fprintf(stdout, "fcconfig -a -v <vtl>  -r <allow|disallow> -w <wwpn>\n");
	fprintf(stdout, "fcconfig -x -v <vtl>  -w <wwpn>\n");
	fprintf(stdout, "-a will add a rule -x will delete a rule\n");
	exit(0);
}

static void
remove_fc_rule(struct fc_rule_spec *fc_rule_spec)
{
	int retval;
	char reply[512];

	retval = tl_client_fc_rule_op(fc_rule_spec, reply, MSG_ID_REMOVE_FC_RULE);
	if (retval != 0) {
		fprintf(stderr, "Removing fc rule failed\n");
		fprintf(stderr, "Message from server is - %s\n", reply);
		exit(1);
	}
	fprintf(stdout, "FC rule successfully removed\n");
	exit(0);
}

static void
add_fc_rule(struct fc_rule_spec *fc_rule_spec)
{
	int retval;
	char reply[512];

	retval = tl_client_fc_rule_op(fc_rule_spec, reply, MSG_ID_ADD_FC_RULE);
	if (retval != 0) {
		fprintf(stderr, "Adding fc rule failed\n");
		fprintf(stderr, "Message from server is - %s\n", reply);
		exit(1);
	}
	fprintf(stdout, "FC rule successfully added\n");
	exit(0);
}

static void
dump_fc_rule_list(void)
{
	char tempfile[32];
	char buf[512];
	FILE *fp;
	int fd;
	int retval;
	int rule;
	char vtl[40];
	char wwpn[64];

	strcpy(tempfile, MSKTEMP_PREFIX);
	fd = mkstemp(tempfile);
	if (fd == -1) {
		fprintf(stderr, "Internal system error\n");
		exit(1);
	}

	retval = tl_client_list_generic(tempfile, MSG_ID_LIST_FC_RULES);
	if (retval != 0) {
		remove(tempfile);
		fprintf(stderr, "Cannot get fcconfig list\n");
		exit(1);
	}

	fp = fopen(tempfile, "r");
	if (!fp) {
		remove(tempfile);
		fprintf(stderr, "Internal system error\n");
		exit(1);
	}

	fprintf(stdout, "%-42s %-9s %-24s\n", "WWPN", "Rule", "VTL");
	while ((fgets(buf, sizeof(buf), fp) != NULL)) {
		retval = sscanf(buf, "wwpn: %s vtl: %s rule: %d\n", wwpn, vtl, &rule);
		if (retval != 3) {
			fprintf(stderr, "Invalid buf %s\n", buf);
			exit(1);
			break;
		}

		if (rule == FC_RULE_ALLOW)
			fprintf(stdout, "%-42s %-9s %-24s\n", wwpn, "Allow", vtl);
		else
			fprintf(stdout, "%-42s %-9s %-24s\n", wwpn, "Disallow", vtl);
	}

	fclose(fp);
	close(fd);
	remove(tempfile);
	exit(0);
}

void
convert_guid_to_wwpn(char *wwpn, char *guid)
{
	int i = 0;

	while (1) {
		wwpn[0] = guid[0];
		wwpn[1] = guid[1];
		wwpn[2] = ':';
		wwpn[3] = guid[2];
		wwpn[4] = guid[3];
		i++;
		if (i == 4)
			break;

		wwpn[5] = ':';
		wwpn += 6;
		guid += 5;
	}
}

void
fill_guid(char *spec_wwpn, char *spec_wwpn1, char *guid)
{
	convert_guid_to_wwpn(spec_wwpn, guid);
	convert_guid_to_wwpn(spec_wwpn1, guid+20);
}

int main(int argc, char *argv[])
{
	int c;
	int list = 0;
	char wwpn[64];
	int wwpn_len;
	char vtl[TDISK_MAX_NAME_LEN];
	char rule[16];
	int add = 0, delete = 0;
	struct fc_rule_spec fc_rule_spec;

	if (geteuid() != 0) {
		fprintf(stderr, "This program can only be run as root\n");
		exit(1);
	}

	memset(rule, 0, sizeof(rule));
	memset(vtl, 0, sizeof(vtl));
	memset(wwpn, 0, sizeof(wwpn));
	memset(&fc_rule_spec, 0, sizeof(fc_rule_spec));

	while ((c = getopt(argc, argv, "w:r:v:lax")) != -1) {
		switch (c) {
		case 'w':
			strncpy(wwpn, optarg, sizeof(wwpn) - 1);
			break;
		case 'r':
			strncpy(rule, optarg, sizeof(rule) - 1);
			break;
		case 'v':
			strncpy(vtl, optarg, sizeof(vtl) - 1);
			break;
		case 'a':
			add = 1;
			break;
		case 'x':
			delete = 1;
			break;
		case 'l':
			list = 1;
			break;
		default:
			print_usage();
			break;
		}
	}

	if (list) {
		dump_fc_rule_list();
	}
	else {
		if (!add && !delete) {
			fprintf(stderr, "-a or -x needs to be specified\n");
			print_usage();
		}
		wwpn_len = strlen(wwpn);
		if (wwpn_len && wwpn_len != 23 && wwpn_len != 39) {
			fprintf(stderr, "Invalid wwpn %s\n", wwpn);
			print_usage();
		}
		if (wwpn_len == 23)
			strcpy(fc_rule_spec.wwpn, wwpn);
		else if (wwpn_len == 39) {
			fill_guid(fc_rule_spec.wwpn, fc_rule_spec.wwpn1, wwpn);
		}
		strcpy(fc_rule_spec.vtl, vtl);
		if (add) {
			if (strcasecmp(rule, "Allow") == 0)
				fc_rule_spec.rule = FC_RULE_ALLOW;
			else if (strcasecmp(rule, "Disallow") == 0)
				fc_rule_spec.rule = FC_RULE_DISALLOW;
			else {
				fprintf(stderr, "Invalid rule %s\n", rule);
				print_usage();
			}
		}

		if (add)
			add_fc_rule(&fc_rule_spec);
		else
			remove_fc_rule(&fc_rule_spec);
	}
	return 0;
}


/*-
 * Copyright (c) 2005 Roman Bogorodskiy
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: pidof.c,v 1.1.1.1 2011/11/21 23:22:24 quadstor Exp $
 */

#include <stdio.h>
#include <string.h>
#include <err.h>
#include <kvm.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/user.h>
#include <sys/param.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sysexits.h>

static int	get_pid_of_process(char *process_name);
static void	usage(void);

static int
get_pid_of_process(char *process_name)
{
	static kvm_t *kd = NULL;
	struct kinfo_proc *p;
	int i, n_processes, processes_found;

	processes_found = 0;
	
	if ((kd = kvm_open("/dev/null", "/dev/null", "/dev/null", O_RDONLY, "kvm_open")) == NULL) 
			 (void)errx(1, "%s", kvm_geterr(kd));
	else {
		p = kvm_getprocs(kd, KERN_PROC_PROC, 0, &n_processes);
		for (i = 0; i<n_processes; i++)
			if (strncmp(process_name, p[i].ki_comm, COMMLEN+1) == 0) {
				(void)printf("%d ", (int)p[i].ki_pid);
				processes_found++;
			}

		kvm_close(kd);
	}	

	return processes_found;
}

static void
usage() 
{

	(void)fprintf(stderr, "usage: pidof name1 name2 ...\n");
	exit(EX_USAGE);
}

int
main(int argc, char **argv)
{
	int i, procs_found;

	procs_found = 0;

	if (argc <= 1)
		usage();

	for (i = 1; i<argc; procs_found += get_pid_of_process(argv[i++]));

	(void)printf("\n");
	
	return (procs_found > 0) ? 0 : 1;
}

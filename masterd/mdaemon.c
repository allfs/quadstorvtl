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

#include <apicommon.h>
#include <tlsrvapi.h>
#include <sys/resource.h>
#include <signal.h>
#include <pthread.h>

static void
term_handler(int signo)
{
	tl_server_unload();
	exit(0);
}

static void daemonize() {

	pid_t pid;
	int fd;

	if ((pid = fork()) < 0) {
		perror("Unable to fork the daemon child process");
		exit(1);
	}
	else if (pid > 0) {
		exit(0); /* parent exit */
	}

	if (setsid() == -1) { /* session leader */
		perror("Unable to become the session leader");
		exit(1);
	}

	openlog(MDAEMON_NAME, LOG_ODELAY, LOG_DAEMON);

	if (chdir("/") != 0) {
		perror("chdir to root directory failed");
		exit(1);
	}

	umask(0);
#if 0
	close(0);
#endif
#ifdef ENABLE_STDERR
	fd = open("/tmp/mdout.txt", O_CREAT|O_WRONLY|O_APPEND);
#else
	fd = open("/dev/null", O_WRONLY);
#endif
	if (fd >= 0)
		dup2(fd, 1);
#ifdef ENABLE_STDERR
	fd = open("/tmp/mderrors.txt", O_CREAT|O_WRONLY|O_APPEND);
#else
	fd = open("/dev/null", O_WRONLY);
#endif
	if (fd >= 0)
		dup2(fd, 2);
	signal(SIGPIPE, SIG_IGN);

	return;
}

#ifdef FREEBSD
static void
gvinum_init()
{
	system("/sbin/gvinum list > /dev/null 2>&1");
	sleep(2);
}
#endif

/* 
 * Initialize the server
 */

extern struct lstate_info lstate_info;
int main(int argc, char *argv[])
{
	int retval;
	struct rlimit rlimit;
	pthread_t mainloop_id;

	daemonize();

	getrlimit(RLIMIT_AS, &rlimit);
	if (rlimit.rlim_cur != rlimit.rlim_max) {
		rlimit.rlim_cur = rlimit.rlim_max;
		setrlimit(RLIMIT_AS, &rlimit);
	}

	/* Dump cores */
	rlimit.rlim_cur = RLIM_INFINITY;
	rlimit.rlim_max = RLIM_INFINITY;
	setrlimit(RLIMIT_CORE, &rlimit);

#ifdef FREEBSD
	gvinum_init();
#endif

	signal(SIGTERM, term_handler);
	retval = main_server_start(&mainloop_id);
	if (retval != 0) {
		DEBUG_ERR("Unable to start mainloop\n");
		exit(1);
	}

	(void) pthread_join(mainloop_id, NULL);
	DEBUG_INFO("main: exiting cleanly\n");
	return 0;
}

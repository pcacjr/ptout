// SPDX-License-Identifier: GPL-3.0
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>

#define die(s) ({ perror(s); exit(EXIT_FAILURE); })

static void usage(char *argv[])
{
	printf("Extract output from a program running under a fake PTY.\n\n"
	       "Usage: %s prog [args]\n\n", argv[0]);
	exit(EXIT_FAILURE);
}

static void flush_pts(int fd)
{
	if (tcflush(fd, TCIOFLUSH) == -1)
		die("tcflush()");
}

static void set_raw_mode(int fd)
{
	struct termios tio;

	if (tcgetattr(fd, &tio) == -1)
		die("tcgetattr()");
	cfmakeraw(&tio);
	if (tcsetattr(fd, TCSANOW, &tio) == -1)
		die("tcsetattr()");
}

static void print_buf(int sfd)
{
	ssize_t c;
	static char buf[4096];

	for (;;) {
		c = read(sfd, buf, sizeof(buf));
		if (c == -1) {
			if (errno != EINTR)
				die("read()");
			continue;
		}
		if (c == 0)
			break;
		write(STDOUT_FILENO, buf, c);
		buf[0] = '\0';
	}
}

static void do_poll(int sfd)
{
	int efd;
	struct epoll_event ev = {0};
	struct epoll_event events[10] = {0};

	efd = epoll_create1(0);
	if (efd == -1)
		die("epoll_create1()");

	ev.events = EPOLLIN;
	ev.data.fd = sfd;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &ev) == -1)
		die("epoll_ctl()");

	for (;;) {
		int nfds, i;
		int nevents = sizeof(events) / sizeof(events[0]);

		nfds = epoll_wait(efd, events, nevents, -1);
		if (nfds == -1)
			die("epoll_wait()");
		for (i = 0; i < nfds; i++) {
			if ((events[i].events & EPOLLIN) &&
			    events[i].data.fd == sfd)
				print_buf(sfd);
		}
	}
}

static int do_exec(int argc, char *argv[])
{
	char **args;
	int nargs = argc + 1;

	args = alloca(sizeof(char *) * nargs);
	memset(args, 0, sizeof(char *) * nargs);
	args[0] = args[1] = argv[1];
	if (argc > 2)
		memcpy(&args[2], &argv[2], sizeof(char *) * (nargs - 2));

	if (execvp(args[0], &args[1]) == -1)
		die("execvp()");
	return 0;
}

int main(int argc, char *argv[])
{
	int mfd, sfd;
	pid_t pid;
	const char *prog;

	if (argc < 2)
		usage(argv);

	mfd = posix_openpt(O_RDWR);
	if (mfd == -1)
		die("posix_openpt()");

	if (grantpt(mfd))
		die("grantpt()");

	if (unlockpt(mfd))
		die("unlockpt()");

	sfd = open(ptsname(mfd), O_RDWR);
	if (sfd == -1)
		die("open()");

	pid = fork();
	if (pid == -1)
		die("fork()");

	if (pid == 0) {
		close(sfd);
		flush_pts(mfd);

		if (dup2(mfd, STDIN_FILENO) == -1)
			die("dup2()");
		if (dup2(mfd, STDOUT_FILENO) == -1)
			die("dup2()");
		if (dup2(mfd, STDERR_FILENO) == -1)
			die("dup2()");

		do_exec(argc, argv);
	}

	close(mfd);
	set_raw_mode(sfd);
	close(STDERR_FILENO);
	close(STDIN_FILENO);

	do_poll(sfd);

	if (waitpid(pid, NULL, 0) == -1)
		die("waitpid()");

	return 0;
}

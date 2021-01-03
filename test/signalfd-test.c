#define _GNU_SOURCE

#include <atf-c.h>

#include <sys/types.h>

#include <sys/param.h>
#include <sys/select.h>
#include <sys/time.h>

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <err.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include <sys/signalfd.h>

#include "atf-c-leakcheck.h"

ATF_TC_WITHOUT_HEAD(signalfd__simple_signalfd);
ATF_TC_BODY_FD_LEAKCHECK(signalfd__simple_signalfd, tcptr)
{
	sigset_t mask;
	int sfd;
	struct signalfd_siginfo fdsi;
	ssize_t s;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	ATF_REQUIRE(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);

	sfd = signalfd(-1, &mask, 0);
	ATF_REQUIRE(sfd >= 0);

	kill(getpid(), SIGINT);

	{
		struct pollfd pfd = {.fd = sfd, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, -1) == 1);
		ATF_REQUIRE(pfd.revents == POLLIN);
	}

	s = read(sfd, &fdsi, sizeof(struct signalfd_siginfo));
	ATF_REQUIRE(s == sizeof(struct signalfd_siginfo));

	ATF_REQUIRE(fdsi.ssi_signo == SIGINT);

	ATF_REQUIRE(sigprocmask(SIG_UNBLOCK, &mask, NULL) == 0);

	{
		struct pollfd pfd = {.fd = sfd, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, 0) == 0);
	}

	ATF_REQUIRE(close(sfd) == 0);
}

static void *
sleep_then_kill(void *arg)
{
	(void)arg;
	usleep(300000);
	kill(getpid(), SIGINT);
	return NULL;
}

ATF_TC_WITHOUT_HEAD(signalfd__blocking_read);
ATF_TC_BODY_FD_LEAKCHECK(signalfd__blocking_read, tcptr)
{
	sigset_t mask;
	int sfd;
	struct signalfd_siginfo fdsi;
	ssize_t s;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	ATF_REQUIRE(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);

	sfd = signalfd(-1, &mask, 0);
	ATF_REQUIRE(sfd >= 0);

	pthread_t writer_thread;
	ATF_REQUIRE(
	    pthread_create(&writer_thread, NULL, sleep_then_kill, NULL) == 0);

	s = read(sfd, &fdsi, sizeof(struct signalfd_siginfo));
	ATF_REQUIRE(s == sizeof(struct signalfd_siginfo));

	ATF_REQUIRE(fdsi.ssi_signo == SIGINT);

	ATF_REQUIRE(pthread_join(writer_thread, NULL) == 0);

	ATF_REQUIRE(close(sfd) == 0);
}

ATF_TC_WITHOUT_HEAD(signalfd__nonblocking_read);
ATF_TC_BODY_FD_LEAKCHECK(signalfd__nonblocking_read, tcptr)
{
	sigset_t mask;
	int sfd;
	struct signalfd_siginfo fdsi;
	ssize_t s;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	ATF_REQUIRE(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);

	sfd = signalfd(-1, &mask, SFD_NONBLOCK);
	ATF_REQUIRE(sfd >= 0);

	ATF_REQUIRE_ERRNO(EAGAIN,
	    read(sfd, &fdsi, sizeof(struct signalfd_siginfo)) < 0);

	pthread_t writer_thread;
	ATF_REQUIRE(
	    pthread_create(&writer_thread, NULL, sleep_then_kill, NULL) == 0);

	int read_counter = 0;
	do {
		++read_counter;
		s = read(sfd, &fdsi, sizeof(struct signalfd_siginfo));
	} while (s < 0 && errno == EAGAIN);

	ATF_REQUIRE(s == sizeof(struct signalfd_siginfo));
	ATF_REQUIRE_MSG(read_counter > 10, "%d", read_counter);

	ATF_REQUIRE(fdsi.ssi_signo == SIGINT);

	ATF_REQUIRE(pthread_join(writer_thread, NULL) == 0);

	ATF_REQUIRE(close(sfd) == 0);
}

ATF_TC_WITHOUT_HEAD(signalfd__multiple_signals);
ATF_TC_BODY_FD_LEAKCHECK(signalfd__multiple_signals, tcptr)
{
	sigset_t mask;
	int sfd;
	struct signalfd_siginfo fdsi[16];
	ssize_t s;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);

	ATF_REQUIRE(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);

	sfd = signalfd(-1, &mask, 0);
	ATF_REQUIRE(sfd >= 0);

	kill(getpid(), SIGINT);
	kill(getpid(), SIGUSR1);
	kill(getpid(), SIGUSR2);

	s = read(sfd, &fdsi, sizeof(fdsi));
	ATF_REQUIRE(s == 3 * sizeof(struct signalfd_siginfo));

	printf("%d %d %d\n", /**/
	    fdsi[0].ssi_signo, fdsi[1].ssi_signo, fdsi[2].ssi_signo);

	ATF_REQUIRE(			   /**/
	    fdsi[0].ssi_signo == SIGINT || /**/
	    fdsi[1].ssi_signo == SIGINT || /**/
	    fdsi[2].ssi_signo == SIGINT);
	ATF_REQUIRE(			    /**/
	    fdsi[0].ssi_signo == SIGUSR1 || /**/
	    fdsi[1].ssi_signo == SIGUSR1 || /**/
	    fdsi[2].ssi_signo == SIGUSR1);
	ATF_REQUIRE(			    /**/
	    fdsi[0].ssi_signo == SIGUSR2 || /**/
	    fdsi[1].ssi_signo == SIGUSR2 || /**/
	    fdsi[2].ssi_signo == SIGUSR2);

	ATF_REQUIRE(close(sfd) == 0);
}

ATF_TC_WITHOUT_HEAD(signalfd__modify_signalmask);
ATF_TC_BODY_FD_LEAKCHECK(signalfd__modify_signalmask, tcptr)
{
	sigset_t mask;
	int sfd;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	sfd = signalfd(-1, &mask, 0);
	ATF_REQUIRE(sfd >= 0);

	sigaddset(&mask, SIGUSR1);

#ifndef __linux__
	atf_tc_expect_fail("modifying an existing signalfd descriptor is "
			   "not currently supported");
#endif

	ATF_REQUIRE(sfd == signalfd(sfd, &mask, 0));

	ATF_REQUIRE(close(sfd) == 0);
}

ATF_TC_WITHOUT_HEAD(signalfd__argument_checks);
ATF_TC_BODY_FD_LEAKCHECK(signalfd__argument_checks, tcptr)
{
	sigset_t mask;
	int sfd;

	int const invalid_fd = 0xbeef;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	sfd = signalfd(-1, &mask, 0);
	ATF_REQUIRE(sfd >= 0);
	ATF_REQUIRE(close(sfd) == 0);

	int fds[2];
	ATF_REQUIRE(pipe2(fds, O_CLOEXEC) == 0);

	ATF_REQUIRE_ERRNO(EBADF, signalfd(invalid_fd, &mask, 0));
	ATF_REQUIRE(signalfd(invalid_fd, NULL, 0));
	/* Linux 5.10 returns EFAULT. */
	ATF_REQUIRE(errno == EINVAL || errno == EFAULT);
	ATF_REQUIRE(signalfd(-1, NULL, 0));
	/* Linux 5.10 returns EFAULT. */
	ATF_REQUIRE(errno == EINVAL || errno == EFAULT);

	ATF_REQUIRE_ERRNO(EINVAL, signalfd(fds[0], &mask, 0));
	ATF_REQUIRE(signalfd(fds[0], NULL, 0));
	/* Linux 5.10 returns EFAULT. */
	ATF_REQUIRE(errno == EINVAL || errno == EFAULT);

	ATF_REQUIRE_ERRNO(EINVAL, signalfd(invalid_fd, &mask, 42));

	ATF_REQUIRE_ERRNO(EBADF, signalfd(-2, &mask, 0));

	sfd = signalfd(-1, &mask, 0);
	ATF_REQUIRE(sfd >= 0);

	int dummy;
	ssize_t s = read(sfd, &dummy, sizeof(int));
	ATF_REQUIRE_ERRNO(EINVAL, s < 0);

	ATF_REQUIRE(close(sfd) == 0);

	struct signalfd_siginfo siginfo;
	s = read(sfd, &siginfo, sizeof(struct signalfd_siginfo));
	ATF_REQUIRE_ERRNO(EBADF, s < 0);

	ATF_REQUIRE(close(fds[0]) == 0);
	ATF_REQUIRE(close(fds[1]) == 0);
}

static sig_atomic_t volatile got_sigint = 0;
static void
sigint_handler(int signo)
{
	got_sigint = 1;
}

ATF_TC_WITHOUT_HEAD(signalfd__signal_disposition);
ATF_TC_BODY_FD_LEAKCHECK(signalfd__signal_disposition, tcptr)
{
	/*
	 * Check that signalfd's don't work when signals are handled by a
	 * different mechanism.
	 */

	sigset_t mask;
	int sfd;
	struct signalfd_siginfo fdsi;
	ssize_t s;

	ATF_REQUIRE(signal(SIGINT, sigint_handler) != SIG_ERR);

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);

	sfd = signalfd(-1, &mask, SFD_NONBLOCK);
	ATF_REQUIRE(sfd >= 0);

	ATF_REQUIRE_ERRNO(EAGAIN, /**/
	    read(sfd, &fdsi, sizeof(struct signalfd_siginfo)) < 0);

	ATF_REQUIRE(kill(getpid(), SIGINT) == 0);
	ATF_REQUIRE(got_sigint == 1);

	{
		struct pollfd pfd = {.fd = sfd, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, 0) == 0);
	}

	ATF_REQUIRE_ERRNO(EAGAIN, /**/
	    read(sfd, &fdsi, sizeof(struct signalfd_siginfo)) < 0);

	{
		struct pollfd pfd = {.fd = sfd, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, 0) == 0);
	}

	ATF_REQUIRE(close(sfd) == 0);
}

ATF_TC_WITHOUT_HEAD(signalfd__sigwaitinfo);
ATF_TC_BODY_FD_LEAKCHECK(signalfd__sigwaitinfo, tcptr)
{
	sigset_t mask;

	sigemptyset(&mask);

	int sfd2 = signalfd(-1, &mask, SFD_NONBLOCK);
	ATF_REQUIRE(sfd2 >= 0);

	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);
	sigaddset(&mask, SIGINT);

	ATF_REQUIRE(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);

	kill(getpid(), SIGINT);
	kill(getpid(), SIGUSR1);
	kill(getpid(), SIGUSR2);

	int sfd = signalfd(-1, &mask, 0);
	ATF_REQUIRE(sfd >= 0);

	int sfd3 = signalfd(-1, &mask, 0);
	ATF_REQUIRE(sfd3 >= 0);

	{
		struct pollfd pfd = {.fd = sfd, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, -1) == 1);
		ATF_REQUIRE(pfd.revents == POLLIN);
	}
	{
		struct pollfd pfd = {.fd = sfd3, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, -1) == 1);
		ATF_REQUIRE(pfd.revents == POLLIN);
	}

	{
		siginfo_t siginfo;
		int s = sigwaitinfo(&mask, &siginfo);
		ATF_REQUIRE(s == SIGINT);
		ATF_REQUIRE(siginfo.si_signo == SIGINT);
	}

	{
		struct pollfd pfd = {.fd = sfd, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, -1) == 1);
		ATF_REQUIRE(pfd.revents == POLLIN);
	}
	{
		struct pollfd pfd = {.fd = sfd3, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, -1) == 1);
		ATF_REQUIRE(pfd.revents == POLLIN);
	}

	{
		siginfo_t siginfo;
		int s = sigtimedwait(&mask, &siginfo, /**/
		    &(struct timespec){0, 0});
		ATF_REQUIRE(s == SIGUSR1);
		ATF_REQUIRE(siginfo.si_signo == SIGUSR1);
	}

	{
		struct pollfd pfd = {.fd = sfd, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, -1) == 1);
		ATF_REQUIRE(pfd.revents == POLLIN);
	}
	{
		struct pollfd pfd = {.fd = sfd3, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, -1) == 1);
		ATF_REQUIRE(pfd.revents == POLLIN);
	}

	{
		struct signalfd_siginfo fdsi;
		ATF_REQUIRE(
		    read(sfd, &fdsi, sizeof(struct signalfd_siginfo)) >= 0);
		ATF_REQUIRE(fdsi.ssi_signo == SIGUSR2);
	}

	ATF_REQUIRE(sigprocmask(SIG_UNBLOCK, &mask, NULL) == 0);

	{
		struct pollfd pfd = {.fd = sfd, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, 0) == 0);
	}
	{
		struct pollfd pfd = {.fd = sfd3, .events = POLLIN};
		ATF_REQUIRE(poll(&pfd, 1, 0) == 0);
	}

	ATF_REQUIRE(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
	kill(getpid(), SIGUSR1);
	struct signalfd_siginfo fdsi;
	ATF_REQUIRE_ERRNO(EAGAIN, /**/
	    read(sfd2, &fdsi, sizeof(struct signalfd_siginfo)) < 0);
	ATF_REQUIRE(read(sfd, &fdsi, sizeof(struct signalfd_siginfo)) >= 0);
	ATF_REQUIRE(sigprocmask(SIG_UNBLOCK, &mask, NULL) == 0);

	ATF_REQUIRE(close(sfd) == 0);
	ATF_REQUIRE(close(sfd2) == 0);
	ATF_REQUIRE(close(sfd3) == 0);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, signalfd__simple_signalfd);
	ATF_TP_ADD_TC(tp, signalfd__blocking_read);
	ATF_TP_ADD_TC(tp, signalfd__nonblocking_read);
	ATF_TP_ADD_TC(tp, signalfd__multiple_signals);
	ATF_TP_ADD_TC(tp, signalfd__modify_signalmask);
	ATF_TP_ADD_TC(tp, signalfd__argument_checks);
	ATF_TP_ADD_TC(tp, signalfd__signal_disposition);
	ATF_TP_ADD_TC(tp, signalfd__sigwaitinfo);

	return atf_no_error();
}

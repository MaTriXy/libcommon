/*
 * Copyright (C) 2012-2016 Max Kellermann <max.kellermann@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "FileDescriptor.hxx"

#include <sys/stat.h>
#include <fcntl.h>

#ifdef __BIONIC__
#include <sys/syscall.h>
#endif

#ifndef _WIN32
#include <poll.h>
#endif

#if defined(HAVE_EVENTFD) && !defined(__BIONIC__)
#include <sys/eventfd.h>
#endif

#if defined(HAVE_SIGNALFD) && !defined(__BIONIC__)
#include <sys/signalfd.h>
#endif

#ifdef HAVE_INOTIFY
#include <sys/inotify.h>
#endif

#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif

bool
FileDescriptor::IsValid() const noexcept
{
	return IsDefined() && fcntl(fd, F_GETFL) >= 0;
}

bool
FileDescriptor::IsPipe() const noexcept
{
	struct stat st;
	return IsDefined() && fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode);
}

bool
FileDescriptor::IsSocket() const noexcept
{
	struct stat st;
	return IsDefined() && fstat(fd, &st) == 0 && S_ISSOCK(st.st_mode);
}

bool
FileDescriptor::Open(const char *pathname, int flags, mode_t mode) noexcept
{
	fd = ::open(pathname, flags | O_NOCTTY | O_CLOEXEC, mode);
	return IsDefined();
}

bool
FileDescriptor::OpenReadOnly(const char *pathname) noexcept
{
	return Open(pathname, O_RDONLY);
}

#ifndef _WIN32

bool
FileDescriptor::OpenNonBlocking(const char *pathname) noexcept
{
	return Open(pathname, O_RDWR | O_NONBLOCK);
}

#ifdef __linux__
bool
FileDescriptor::CreatePipe(FileDescriptor &r, FileDescriptor &w, int flags) noexcept
{
	int fds[2];
#ifdef __BIONIC__
	/* Bionic provides the pipe2() function only since Android 2.3,
	   therefore we must roll our own system call here */
	const int result = syscall(__NR_pipe2, fds, flags);
#else
	const int result = pipe2(fds, flags);
#endif
	if (result < 0)
		return false;

	r = FileDescriptor(fds[0]);
	w = FileDescriptor(fds[1]);
	return true;
}
#endif

bool
FileDescriptor::CreatePipe(FileDescriptor &r, FileDescriptor &w) noexcept
{
#ifdef __linux__
	return CreatePipe(r, w, O_CLOEXEC);
#else
	int fds[2];
	const int result = pipe(fds);
	if (result < 0)
		return false;

	r = FileDescriptor(fds[0]);
	w = FileDescriptor(fds[1]);
	return true;
#endif
}

bool
FileDescriptor::CreatePipeNonBlock(FileDescriptor &r, FileDescriptor &w) noexcept
{
#ifdef __linux__
	return CreatePipe(r, w, O_CLOEXEC|O_NONBLOCK);
#else
	if (!CreatePipe(r, w))
		return false;

	r.SetNonBlocking();
	w.SetNonBlocking();
	return true;
#endif
}

void
FileDescriptor::SetNonBlocking() noexcept
{
	assert(IsDefined());

	int flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void
FileDescriptor::SetBlocking() noexcept
{
	assert(IsDefined());

	int flags = fcntl(fd, F_GETFL);
	fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
}

void
FileDescriptor::EnableCloseOnExec() noexcept
{
	assert(IsDefined());

	const int old_flags = fcntl(fd, F_GETFD, 0);
	fcntl(fd, F_SETFD, old_flags | FD_CLOEXEC);
}

void
FileDescriptor::DisableCloseOnExec() noexcept
{
	assert(IsDefined());

	const int old_flags = fcntl(fd, F_GETFD, 0);
	fcntl(fd, F_SETFD, old_flags & ~FD_CLOEXEC);
}

bool
FileDescriptor::CheckDuplicate(FileDescriptor new_fd) noexcept
{
	if (*this == new_fd) {
		DisableCloseOnExec();
		return true;
	} else
		return Duplicate(new_fd);
}

#endif

#ifdef HAVE_EVENTFD

bool
FileDescriptor::CreateEventFD(unsigned initval) noexcept
{
#ifdef __BIONIC__
	/* Bionic provides the eventfd() function only since Android 2.3,
	   therefore we must roll our own system call here */
	fd = syscall(__NR_eventfd2, initval, O_NONBLOCK|O_CLOEXEC);
#else
	fd = ::eventfd(initval, EFD_NONBLOCK|EFD_CLOEXEC);
#endif
	return fd >= 0;
}

#endif

#ifdef HAVE_SIGNALFD

bool
FileDescriptor::CreateSignalFD(const sigset_t *mask, bool nonblock) noexcept
{
#ifdef __BIONIC__
	int flags = O_CLOEXEC;
	if (nonblock)
		flags |= O_NONBLOCK;
	int new_fd = syscall(__NR_signalfd4, fd, mask, sizeof(*mask),
			     flags);
#else
	int flags = SFD_CLOEXEC;
	if (nonblock)
		flags |= SFD_NONBLOCK;
	int new_fd = ::signalfd(fd, mask, flags);
#endif
	if (new_fd < 0)
		return false;

	fd = new_fd;
	return true;
}

#endif

#ifdef HAVE_INOTIFY

bool
FileDescriptor::CreateInotify() noexcept
{
#ifdef __BIONIC__
	/* Bionic doesn't have inotify_init1() */
	int new_fd = inotify_init();
#else
	int new_fd = inotify_init1(IN_CLOEXEC|IN_NONBLOCK);
#endif
	if (new_fd < 0)
		return false;

#ifdef __BIONIC__
	SetNonBlocking();
#endif

	fd = new_fd;
	return true;
}

#endif

bool
FileDescriptor::Rewind() noexcept
{
	assert(IsDefined());

	return lseek(fd, 0, SEEK_SET) == 0;
}

off_t
FileDescriptor::GetSize() const noexcept
{
	struct stat st;
	return ::fstat(fd, &st) >= 0
		? (long)st.st_size
		: -1;
}

#ifndef _WIN32

int
FileDescriptor::Poll(short events, int timeout) const noexcept
{
	assert(IsDefined());

	struct pollfd pfd;
	pfd.fd = fd;
	pfd.events = events;
	int result = poll(&pfd, 1, timeout);
	return result > 0
		? pfd.revents
		: result;
}

int
FileDescriptor::WaitReadable(int timeout) const noexcept
{
	return Poll(POLLIN, timeout);
}

int
FileDescriptor::WaitWritable(int timeout) const noexcept
{
	return Poll(POLLOUT, timeout);
}

bool
FileDescriptor::IsReadyForWriting() const noexcept
{
	return WaitWritable(0) > 0;
}

#endif

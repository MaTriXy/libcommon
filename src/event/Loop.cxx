// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Loop.hxx"
#include "DeferEvent.hxx"
#include "SocketEvent.hxx"
#include "util/ScopeExit.hxx"

#ifdef HAVE_THREADED_EVENT_LOOP
#include "InjectEvent.hxx"
#endif

#ifdef HAVE_URING
#include "uring/Manager.hxx"
#endif

#include <array>

EventLoop::EventLoop(
#ifdef HAVE_THREADED_EVENT_LOOP
		     ThreadId _thread
#endif
		     )
#ifdef HAVE_THREADED_EVENT_LOOP
	:thread(_thread),
	 /* if this instance is hosted by an EventThread (no ThreadId
	    known yet) then we're not yet alive until the thread is
	    started; for the main EventLoop instance, we assume it's
	    already alive, because nobody but EventThread will call
	    SetAlive() */
	 alive(!_thread.IsNull())
#endif
{
}

EventLoop::~EventLoop() noexcept
{
#if defined(HAVE_URING) && !defined(NDEBUG)
	/* if Run() was never called (maybe because startup failed and
	   an exception is pending), we need to destruct the
	   Uring::Manager here or else the assertions below fail */
	uring.reset();
#endif

	assert(defer.empty());
	assert(idle.empty());
	assert(next.empty());
#ifdef HAVE_THREADED_EVENT_LOOP
	assert(inject.empty());
#endif
	assert(sockets.empty());
	assert(ready_sockets.empty());
}

void
EventLoop::SetVolatile() noexcept
{
#ifdef HAVE_URING
	if (uring)
		uring->SetVolatile();
#endif
}

#ifdef HAVE_URING

void
EventLoop::EnableUring(unsigned entries, unsigned flags)
{
	assert(!uring);

	uring = std::make_unique<Uring::Manager>(*this, entries, flags);
}

void
EventLoop::EnableUring(unsigned entries, struct io_uring_params &params)
{
	assert(!uring);

	uring = std::make_unique<Uring::Manager>(*this, entries, params);
}

void
EventLoop::DisableUring() noexcept
{
	uring.reset();
}

Uring::Queue *
EventLoop::GetUring() noexcept
{
	return uring.get();
}

#endif // HAVE_URING

bool
EventLoop::AddFD(int fd, unsigned events, SocketEvent &event) noexcept
{
#ifdef HAVE_THREADED_EVENT_LOOP
	assert(!IsAlive() || IsInside());
#endif
	assert(events != 0);

	if (!poll_backend.Add(fd, events, &event))
		return false;

	sockets.push_back(event);
	return true;
}

bool
EventLoop::ModifyFD(int fd, unsigned events, SocketEvent &event) noexcept
{
#ifdef HAVE_THREADED_EVENT_LOOP
	assert(!IsAlive() || IsInside());
#endif
	assert(events != 0);

	return poll_backend.Modify(fd, events, &event);
}

bool
EventLoop::RemoveFD(int fd, SocketEvent &event) noexcept
{
#ifdef HAVE_THREADED_EVENT_LOOP
	assert(!IsAlive() || IsInside());
#endif

	event.unlink();
	return poll_backend.Remove(fd);
}

void
EventLoop::AbandonFD(SocketEvent &event) noexcept
{
#ifdef HAVE_THREADED_EVENT_LOOP
	assert(!IsAlive() || IsInside());
#endif
	assert(event.IsDefined());

	event.unlink();
}

void
EventLoop::Insert(CoarseTimerEvent &t) noexcept
{
	assert(IsInside());

	coarse_timers.Insert(t, SteadyNow());
	again = true;
}

#ifndef NO_FINE_TIMER_EVENT

void
EventLoop::Insert(FineTimerEvent &t) noexcept
{
	assert(IsInside());

	timers.Insert(t);
	again = true;
}

#endif // NO_FINE_TIMER_EVENT

/**
 * Determines which timeout will happen earlier; either one may be
 * negative to specify "no timeout at all".
 */
static constexpr Event::Duration
GetEarlierTimeout(Event::Duration a, Event::Duration b) noexcept
{
	return b.count() < 0 || (a.count() >= 0 && a < b)
		? a
		: b;
}

inline Event::Duration
EventLoop::HandleTimers() noexcept
{
	const auto now = SteadyNow();

#ifndef NO_FINE_TIMER_EVENT
	auto fine_timeout = timers.Run(now);
#else
	const Event::Duration fine_timeout{-1};
#endif // NO_FINE_TIMER_EVENT
	auto coarse_timeout = coarse_timers.Run(now);

	return GetEarlierTimeout(coarse_timeout, fine_timeout);
}

void
EventLoop::AddDefer(DeferEvent &e) noexcept
{
#ifdef HAVE_THREADED_EVENT_LOOP
	assert(!IsAlive() || IsInside());
#endif

	defer.push_back(e);

#ifdef HAVE_THREADED_EVENT_LOOP
	/* setting this flag here is only relevant if we've been
	   called by a DeferEvent */
	again = true;
#endif
}

void
EventLoop::AddIdle(DeferEvent &e) noexcept
{
	assert(IsInside());

	idle.push_back(e);

#ifdef HAVE_THREADED_EVENT_LOOP
	/* setting this flag here is only relevant if we've been
	   called by a DeferEvent */
	again = true;
#endif
}

void
EventLoop::AddNext(DeferEvent &e) noexcept
{
	assert(IsInside());

	next.push_back(e);
}

void
EventLoop::RunDeferred() noexcept
{
	while (!defer.empty() && !quit) {
		defer.pop_front_and_dispose([](DeferEvent *e){
			e->Run();
		});
	}
}

bool
EventLoop::RunOneIdle() noexcept
{
	if (idle.empty())
		return false;

	idle.pop_front_and_dispose([](DeferEvent *e){
		e->Run();
	});

	return true;
}

/**
 * Convert the given timeout specification to a milliseconds integer,
 * to be used by functions like poll() and epoll_wait().  Any negative
 * value (= never times out) is translated to the magic value -1.
 */
static constexpr int
ExportTimeoutMS(Event::Duration timeout) noexcept
{
	return timeout >= timeout.zero()
		? static_cast<int>(std::chrono::ceil<std::chrono::milliseconds>(timeout).count())
		: -1;
}

inline bool
EventLoop::Wait(Event::Duration timeout) noexcept
{
	std::array<struct epoll_event, 256> received_events;
	int ret = poll_backend.Wait(received_events.data(),
				    received_events.size(),
				    ExportTimeoutMS(timeout));
	for (int i = 0; i < ret; ++i) {
		const auto &e = received_events[i];
		auto &socket_event = *(SocketEvent *)e.data.ptr;
		socket_event.SetReadyFlags(e.events);

		/* move from "sockets" to "ready_sockets" */
		socket_event.unlink();
		ready_sockets.push_back(socket_event);
	}

	return ret > 0;
}

void
EventLoop::Run() noexcept
{
#ifdef HAVE_THREADED_EVENT_LOOP
	assert(!thread.IsNull());
#endif

	assert(IsInside());
#ifdef HAVE_THREADED_EVENT_LOOP
	assert(alive || quit_injected);
	assert(busy);

	wake_event.Schedule(SocketEvent::READ);
#endif

#ifdef HAVE_THREADED_EVENT_LOOP
	AtScopeExit(this) {
		wake_event.Cancel();
	};
#endif

	FlushClockCaches();

	quit = false;

	do {
		again = false;

		/* invoke timers */

		Event::Duration timeout = HandleTimers();
		if (quit)
			break;

		RunDeferred();
		if (quit)
			break;

		if (RunOneIdle())
			/* check for other new events after each
			   "idle" invocation to ensure that the other
			   "idle" events are really invoked at the
			   very end */
			continue;

#ifdef HAVE_THREADED_EVENT_LOOP
		/* try to handle DeferEvents without WakeFD
		   overhead */
		{
			const std::scoped_lock lock{mutex};
			HandleInject();
#endif

			if (again)
				/* re-evaluate timers because one of
				   the DeferEvents may have added a
				   new timeout */
				continue;

#ifdef HAVE_THREADED_EVENT_LOOP
			busy = false;
		}
#endif

		/* wait for new event */

		if (IsEmpty())
			return;

		if (ready_sockets.empty()) {
			if (!next.empty())
				timeout = Event::Duration{0};

			Wait(timeout);

			idle.splice(std::next(idle.begin()), next);

			FlushClockCaches();
		}

#ifdef HAVE_THREADED_EVENT_LOOP
		{
			const std::scoped_lock lock{mutex};
			busy = true;
		}
#endif

		/* invoke sockets */
		while (!ready_sockets.empty() && !quit) {
			auto &socket_event = ready_sockets.front();

			/* move from "ready_sockets" back to "sockets" */
			socket_event.unlink();
			sockets.push_back(socket_event);

			socket_event.Dispatch();
		}

		RunPost();
	} while (!quit);

#ifdef HAVE_THREADED_EVENT_LOOP
#ifndef NDEBUG
	assert(thread.IsInside());
#endif
#endif
}

#ifdef HAVE_THREADED_EVENT_LOOP

void
EventLoop::AddInject(InjectEvent &d) noexcept
{
	bool must_wake;

	{
		const std::scoped_lock lock{mutex};
		if (d.IsPending())
			return;

		/* we don't need to wake up the EventLoop if another
		   InjectEvent has already done it */
		must_wake = !busy && inject.empty();

		inject.push_back(d);
		again = true;
	}

	if (must_wake)
		wake_fd.Write();
}

void
EventLoop::RemoveInject(InjectEvent &d) noexcept
{
	const std::scoped_lock protect{mutex};

	if (d.IsPending())
		inject.erase(inject.iterator_to(d));
}

void
EventLoop::HandleInject() noexcept
{
	while (!inject.empty() && !quit) {
		auto &m = inject.front();
		assert(m.IsPending());

		inject.pop_front();

		const ScopeUnlock unlock(mutex);
		m.Run();
	}
}

void
EventLoop::OnSocketReady([[maybe_unused]] unsigned flags) noexcept
{
	assert(IsInside());

	wake_fd.Read();

	if (quit_injected) {
		Break();
		return;
	}

	const std::scoped_lock lock{mutex};
	HandleInject();
}

#endif

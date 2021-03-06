/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
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

#include "MultiUdpListener.hxx"
#include "UdpHandler.hxx"
#include "system/Error.hxx"

#include <assert.h>
#include <sys/socket.h>

MultiUdpListener::MultiUdpListener(EventLoop &event_loop,
				   UniqueSocketDescriptor _socket,
				   MultiReceiveMessage &&_multi,
				   UdpHandler &_handler) noexcept
	:socket(std::move(_socket)),
	 event(event_loop, socket.Get(),
	       SocketEvent::READ|SocketEvent::PERSIST,
	       BIND_THIS_METHOD(EventCallback)),
	 multi(std::move(_multi)),
	 handler(_handler)
{
	event.Add();
}

MultiUdpListener::~MultiUdpListener() noexcept
{
	assert(socket.IsDefined());

	event.Delete();
}

void
MultiUdpListener::EventCallback(unsigned) noexcept
try {
	if (!multi.Receive(socket)) {
		handler.OnUdpDatagram(nullptr, 0, nullptr, -1);
		return;
	}

	for (auto &d : multi)
		if (!handler.OnUdpDatagram(d.payload.data, d.payload.size,
					   d.address,
					   d.cred != nullptr
					   ? d.cred->uid
					   : -1))
			return;

	multi.Clear();
} catch (...) {
	/* unregister the SocketEvent, just in case the handler does
	   not destroy us */
	event.Delete();

	handler.OnUdpError(std::current_exception());
}

void
MultiUdpListener::Reply(SocketAddress address,
			const void *data, size_t data_length)
{
	assert(socket.IsDefined());

	ssize_t nbytes = sendto(socket.Get(), data, data_length,
				MSG_DONTWAIT|MSG_NOSIGNAL,
				address.GetAddress(), address.GetSize());
	if (gcc_unlikely(nbytes < 0))
		throw MakeErrno("Failed to send UDP packet");

	if ((size_t)nbytes != data_length)
		throw std::runtime_error("Short send");
}

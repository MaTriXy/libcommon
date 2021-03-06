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

#include "ToString.hxx"
#include "SocketAddress.hxx"
#include "IPv4Address.hxx"
#include "IPv6Address.hxx"

#include <algorithm>

#include <assert.h>
#include <sys/un.h>
#include <netdb.h>
#include <string.h>
#include <stdint.h>

static SocketAddress
ipv64_normalize_mapped(SocketAddress address)
{
	const auto &a6 = *(const struct sockaddr_in6 *)(const void *)address.GetAddress();

	if (!address.IsV4Mapped())
		return address;

	struct in_addr inaddr;
	memcpy(&inaddr, ((const char *)&a6.sin6_addr) + 12, sizeof(inaddr));
	const uint16_t port = FromBE16(a6.sin6_port);

	static IPv4Address a4;
	a4 = {inaddr, port};

	return a4;
}

static bool
LocalToString(char *buffer, size_t buffer_size,
	      const struct sockaddr_un *sun, size_t length)
{
	const size_t prefix = (size_t)((struct sockaddr_un *)nullptr)->sun_path;
	assert(length >= prefix);
	length -= prefix;
	if (length >= buffer_size)
		length = buffer_size - 1;

	memcpy(buffer, sun->sun_path, length);
	char *end = buffer + length;

	if (end > buffer && buffer[0] != '\0' && end[-1] == '\0')
		/* don't convert the null terminator of a non-abstract socket
		   to a '@' */
		--end;

	/* replace all null bytes with '@'; this also handles abstract
	   addresses */
	std::replace(buffer, end, '\0', '@');
	*end = 0;

	return true;
}

bool
ToString(char *buffer, size_t buffer_size,
	 SocketAddress address)
{
	if (address.IsNull())
		return false;

	if (address.GetFamily() == AF_LOCAL)
		return LocalToString(buffer, buffer_size,
				     (const struct sockaddr_un *)address.GetAddress(),
				     address.GetSize());

	address = ipv64_normalize_mapped(address);

	char serv[16];
	int ret = getnameinfo(address.GetAddress(), address.GetSize(),
			      buffer, buffer_size,
			      serv, sizeof(serv),
			      NI_NUMERICHOST | NI_NUMERICSERV);
	if (ret != 0)
		return false;

	if (serv[0] != 0) {
		if (address.GetFamily() == AF_INET6) {
			/* enclose IPv6 address in square brackets */

			size_t length = strlen(buffer);
			if (length + 4 >= buffer_size)
				/* no more room */
				return false;

			memmove(buffer + 1, buffer, length);
			buffer[0] = '[';
			buffer[++length] = ']';
			buffer[++length] = 0;
		}

		if (strlen(buffer) + 1 + strlen(serv) >= buffer_size)
			/* no more room */
			return false;

		strcat(buffer, ":");
		strcat(buffer, serv);
	}

	return true;
}

bool
HostToString(char *buffer, size_t buffer_size,
	     SocketAddress address)
{
	if (address.IsNull())
		return false;

	if (address.GetFamily() == AF_LOCAL)
		return LocalToString(buffer, buffer_size,
				     (const struct sockaddr_un *)address.GetAddress(),
				     address.GetSize());

	address = ipv64_normalize_mapped(address);

	return getnameinfo(address.GetAddress(), address.GetSize(),
			   buffer, buffer_size,
			   nullptr, 0,
			   NI_NUMERICHOST | NI_NUMERICSERV) == 0;
}

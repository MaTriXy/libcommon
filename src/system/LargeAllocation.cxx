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

#include "LargeAllocation.hxx"

#include <new>

#include <sys/mman.h>
#include <unistd.h>

/**
 * Round up the parameter, make it page-aligned.
 */
static size_t
AlignToPageSize(size_t size) noexcept
{
	static const long page_size = sysconf(_SC_PAGESIZE);
	if (page_size <= 0)
		return size;

	size_t ps(page_size);
	return (size + ps - 1) / ps * ps;
}

LargeAllocation::LargeAllocation(size_t _size)
	:the_size(AlignToPageSize(_size))
{
	constexpr int flags = MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE;
	data = mmap(nullptr, the_size,
		    PROT_READ|PROT_WRITE, flags,
		    -1, 0);
	if (data == (void *)-1)
		throw std::bad_alloc();
}

void
LargeAllocation::Free(void *p, size_t size) noexcept
{
	munmap(p, size);
}

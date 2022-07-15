/*
 * Copyright 2019 Max Kellermann <max.kellermann@gmail.com>
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

#include "StringParser.hxx"
#include "StringStrip.hxx"

#include <cstdint> // for SIZE_MAX
#include <cstdlib>
#include <stdexcept>

#include <string.h>

bool
ParseBool(const char *s)
{
	if (strcmp(s, "yes") == 0)
		return true;
	else if (strcmp(s, "no") == 0)
		return false;
	else
		throw std::runtime_error("Failed to parse boolean; \"yes\" or \"no\" expected");
}

unsigned long
ParseUnsignedLong(const char *s)
{
	char *endptr;
	auto value = std::strtoul(s, &endptr, 10);
	if (endptr == s || *endptr != 0)
		throw std::runtime_error("Failed to parse integer");

	return value;
}

unsigned long
ParsePositiveLong(const char *s)
{
	auto value = ParseUnsignedLong(s);
	if (value <= 0)
		throw std::runtime_error("Value must be positive");

	return value;
}

unsigned long
ParsePositiveLong(const char *s, unsigned long max_value)
{
	auto value = ParsePositiveLong(s);
	if (value > max_value)
		throw std::runtime_error("Value is too large");

	return value;
}

template<std::size_t OPERAND>
static std::size_t
Multiply(std::size_t value)
{
	static constexpr std::size_t MAX_VALUE = SIZE_MAX / OPERAND;
	if (value > MAX_VALUE)
		throw std::runtime_error("Value too large");

	return value * OPERAND;
}

std::size_t
ParseSize(const char *s)
{
	char *endptr;
	std::size_t value = std::strtoul(s, &endptr, 10);
	if (endptr == s)
		throw std::runtime_error("Failed to parse integer");

	static constexpr std::size_t KILO = 1024;
	static constexpr std::size_t MEGA = 1024 * KILO;
	static constexpr std::size_t GIGA = 1024 * MEGA;

	s = StripLeft(endptr);

	switch (*s) {
	case 'k':
		value = Multiply<KILO>(value);
		++s;
		break;

	case 'M':
		value = Multiply<MEGA>(value);
		++s;
		break;

	case 'G':
		value = Multiply<GIGA>(value);
		++s;
		break;

	case '\0':
		break;

	default:
		throw std::runtime_error("Unknown size suffix");
	}

	/* ignore 'B' for "byte" */
	if (*s == 'B')
		++s;

	if (*s != '\0')
		throw std::runtime_error("Unknown size suffix");

	return value;
}

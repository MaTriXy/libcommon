/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "Key.hxx"
#include "Error.hxx"

#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/err.h>

UniqueEVP_PKEY
GenerateRsaKey(unsigned bits)
{
	const UniqueEVP_PKEY_CTX ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
	if (!ctx)
		throw SslError("EVP_PKEY_CTX_new_id() failed");

	if (EVP_PKEY_keygen_init(ctx.get()) <= 0)
		throw SslError("EVP_PKEY_keygen_init() failed");

	if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), bits) <= 0)
		throw SslError("EVP_PKEY_CTX_set_rsa_keygen_bits() failed");

	EVP_PKEY *pkey = nullptr;
	if (EVP_PKEY_keygen(ctx.get(), &pkey) <= 0)
		throw SslError("EVP_PKEY_keygen() failed");

	return UniqueEVP_PKEY(pkey);
}

UniqueEVP_PKEY
GenerateEcKey()
{
	const UniqueEVP_PKEY_CTX ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_EC, nullptr));
	if (!ctx)
		throw SslError("EVP_PKEY_CTX_new_id() failed");

	if (EVP_PKEY_keygen_init(ctx.get()) <= 0)
		throw SslError("EVP_PKEY_keygen_init() failed");

	if (EVP_PKEY_CTX_set_ec_paramgen_curve_nid(ctx.get(),
						   NID_X9_62_prime256v1) <= 0)
		throw SslError("EVP_PKEY_CTX_set_ec_paramgen_curve_nid() failed");

	EVP_PKEY *pkey = nullptr;
	if (EVP_PKEY_keygen(ctx.get(), &pkey) <= 0)
		throw SslError("EVP_PKEY_keygen() failed");

	return UniqueEVP_PKEY{pkey};
}

UniqueEVP_PKEY
DecodeDerKey(std::span<const std::byte> der)
{
	ERR_clear_error();

	auto data = (const unsigned char *)der.data();
	UniqueEVP_PKEY key(d2i_AutoPrivateKey(nullptr, &data, der.size()));
	if (!key)
		throw SslError("d2i_AutoPrivateKey() failed");

	return key;
}

/**
 * Are both public keys equal?
 */
bool
MatchModulus(EVP_PKEY &key1, EVP_PKEY &key2) noexcept
{
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
	return EVP_PKEY_eq(&key1, &key2) == 1;
#else
	return EVP_PKEY_cmp(&key1, &key2) == 1;
#endif
}

/**
 * Does the certificate belong to the given key?
 */
bool
MatchModulus(X509 &cert, EVP_PKEY &key) noexcept
{
	UniqueEVP_PKEY public_key(X509_get_pubkey(&cert));
	if (public_key == nullptr)
		return false;

	return MatchModulus(*public_key, key);
}

/*
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "scramble.h"
#include "third_party/sha1.h"
#include <string.h>

static void
xor(unsigned char *to, unsigned const char *left,
    unsigned const char *right, uint32_t len)
{
	const uint8_t *end = to + len;
	while (to < end)
		*to++= *left++ ^ *right++;
}

void
scramble_prepare(unsigned char *out, const unsigned char *password,
		 const unsigned char *salt)
{

	unsigned char hash1[SCRAMBLE_SIZE];
	unsigned char hash2[SCRAMBLE_SIZE];
	SHA1_CTX ctx;

	SHA1Init(&ctx);
	SHA1Update(&ctx, password, strlen((const char *) password));
	SHA1Final(hash1, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, hash1, SCRAMBLE_SIZE);
	SHA1Final(hash2, &ctx);

	SHA1Init(&ctx);
	SHA1Update(&ctx, salt, SCRAMBLE_SIZE);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(out, &ctx);

	xor(out, hash1, out, SCRAMBLE_SIZE);
}

int
scramble_check(const unsigned char *scramble, const unsigned char *salt,
	       const unsigned char *hash2)
{
	SHA1_CTX ctx;
	unsigned char candidate_hash2[SCRAMBLE_SIZE];

	SHA1Init(&ctx);
	SHA1Update(&ctx, salt, SCRAMBLE_SIZE);
	SHA1Update(&ctx, hash2, SCRAMBLE_SIZE);
	SHA1Final(candidate_hash2, &ctx);

	xor(candidate_hash2, candidate_hash2, scramble, SCRAMBLE_SIZE);

	return memcmp(hash2, candidate_hash2, SCRAMBLE_SIZE);
}

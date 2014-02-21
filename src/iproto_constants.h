#ifndef TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED
#define TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED
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
#include <stdbool.h>
#include <stdint.h>

enum {
	/** Maximal iproto package body length (2GiB) */
	IPROTO_BODY_LEN_MAX = 2147483648UL,
	IPROTO_GREETING_SIZE = 128,
};


enum iproto_key {
	IPROTO_CODE = 0x00,
	IPROTO_SYNC = 0x01,
	/* Leave a gap for other keys in the header. */
	IPROTO_SPACE_ID = 0x10,
	IPROTO_INDEX_ID = 0x11,
	IPROTO_LIMIT = 0x12,
	IPROTO_OFFSET = 0x13,
	IPROTO_ITERATOR = 0x14,
	/* Leave a gap between integer values and other keys */
	IPROTO_KEY = 0x20,
	IPROTO_TUPLE = 0x21,
	IPROTO_FUNCTION_NAME = 0x22,
	/* Leave a gap between request keys and response keys */
	IPROTO_DATA = 0x30,
	IPROTO_ERROR = 0x31,
	IPROTO_KEY_MAX
};

#define bit(c) (1ULL<<IPROTO_##c)

#define IPROTO_HEAD_BMAP (bit(CODE) | bit(SYNC))
#define IPROTO_BODY_BMAP (bit(SPACE_ID) | bit(INDEX_ID) | bit(LIMIT) |\
			  bit(OFFSET) | bit(KEY) | bit(TUPLE) | \
			  bit(FUNCTION_NAME))
static inline bool
iproto_header_has_key(const char *pos, const char *end)
{
	unsigned char key = pos < end ? *pos : (unsigned char) IPROTO_KEY_MAX;
	return key < IPROTO_KEY_MAX && IPROTO_HEAD_BMAP & (1ULL<<key);
}

static inline bool
iproto_body_has_key(const char *pos, const char *end)
{
	unsigned char key = pos < end ? *pos : (unsigned char) IPROTO_KEY_MAX;
	return key < IPROTO_KEY_MAX && IPROTO_BODY_BMAP & (1ULL<<key);
}

#undef bit


extern unsigned char iproto_key_type[IPROTO_KEY_MAX];

enum iproto_request_type {
	IPROTO_SELECT = 1,
	IPROTO_INSERT = 2,
	IPROTO_REPLACE = 3,
	IPROTO_UPDATE = 4,
	IPROTO_DELETE = 5,
	IPROTO_CALL = 6,
	IPROTO_DML_REQUEST_MAX = 7,
	IPROTO_PING = 64,
	IPROTO_AUTH = 65,
	IPROTO_SUBSCRIBE = 66
};

extern const char *iproto_request_type_strs[];

static inline const char *
iproto_request_name(uint32_t type)
{
	if (type >= IPROTO_DML_REQUEST_MAX)
		return "unknown";
	return iproto_request_type_strs[type];
}

static inline bool
iproto_request_is_select(uint32_t type)
{
	return type <= IPROTO_SELECT || type == IPROTO_CALL;
}

static inline bool
iproto_request_is_dml(uint32_t type)
{
	return type < IPROTO_DML_REQUEST_MAX;
}

#endif /* TARANTOOL_IPROTO_CONSTANTS_H_INCLUDED */

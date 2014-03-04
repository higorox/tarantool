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
#include "request.h"
#include "txn.h"
#include "tuple.h"
#include "index.h"
#include "space.h"
#include "schema.h"
#include "port.h"
#include "lua/call.h"
#include <errinj.h>
#include <pickle.h>
#include <fiber.h>
#include <scoped_guard.h>
#include <third_party/base64.h>

#if 0
static const char *
read_tuple(const char **reqpos, const char *reqend)
{
	const char *tuple = *reqpos;
	if (unlikely(mp_check(reqpos, reqend))) {
		say_error("\n"
			  "********************************************\n"
			  "* Found a corrupted tuple in a request!    *\n"
			  "* This can be either due to a memory       *\n"
			  "* corruption or a bug in the server.       *\n"
			  "* The tuple can not be loaded.             *\n"
			  "********************************************\n"
			  "Request tail, BASE64 encoded:               \n");

		uint32_t tuple_len = reqend - tuple;
		int base64_buflen = base64_bufsize(tuple_len);
		char *base64_buf = (char *) malloc(base64_buflen);
		int len = base64_encode(tuple, tuple_len,
					base64_buf, base64_buflen);
		write(STDERR_FILENO, base64_buf, len);
		free(base64_buf);
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "tuple");
	}

	if (unlikely(mp_typeof(*tuple) != MP_ARRAY))
		tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);

	return tuple;
}
#endif

enum dup_replace_mode
dup_replace_mode(uint32_t op)
{
	return op == IPROTO_INSERT ? DUP_INSERT : DUP_REPLACE_OR_INSERT;
}

static void
execute_replace(struct request *request, struct txn *txn, struct port *port)
{
	(void) port;
	if (unlikely(request->tuple == NULL))
		tnt_raise(ClientError, ER_MISSING_REQUEST_FIELD, "tuple");

	txn_add_redo(txn, request);

	struct space *space = space_find(request->space_id);
	struct tuple *new_tuple = tuple_new(space->format, request->tuple,
					    request->tuple_end);
	TupleGuard guard(new_tuple);
	space_validate_tuple(space, new_tuple);
	enum dup_replace_mode mode = dup_replace_mode(request->type);
	txn_replace(txn, space, NULL, new_tuple, mode);
}

static void
execute_update(struct request *request, struct txn *txn,
	       struct port *port)
{
	(void) port;
	if (unlikely(request->key == NULL))
		tnt_raise(ClientError, ER_MISSING_REQUEST_FIELD, "key");
	if (unlikely(request->tuple == NULL))
		tnt_raise(ClientError, ER_MISSING_REQUEST_FIELD, "tuple");

	txn_add_redo(txn, request);
	/* Parse UPDATE request. */
	/** Search key  and key part count. */

	struct space *space = space_find(request->space_id);
	Index *pk = index_find(space, 0);
	/* Try to find the tuple by primary key. */
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(pk->key_def, key, part_count);
	struct tuple *old_tuple = pk->findByKey(key, part_count);

	if (old_tuple == NULL)
		return;

	/* Update the tuple. */
	struct tuple *new_tuple = tuple_update(space->format,
					       region_alloc_cb,
					       &fiber()->gc,
					       old_tuple, request->tuple,
					       request->tuple_end);
	TupleGuard guard(new_tuple);
	space_validate_tuple(space, new_tuple);
	txn_replace(txn, space, old_tuple, new_tuple, DUP_INSERT);
}

/** }}} */

static void
execute_select(struct request *request, struct txn *txn, struct port *port)
{
	(void) txn;
	struct space *space = space_find(request->space_id);
	Index *index = index_find(space, request->index_id);

	ERROR_INJECT_EXCEPTION(ERRINJ_TESTING);

	uint32_t found = 0;
	uint32_t offset = request->offset;
	uint32_t limit = request->limit;
	if (request->iterator >= iterator_type_MAX)
		tnt_raise(IllegalParams, "Invalid iterator type");
	enum iterator_type type = (enum iterator_type) request->iterator;

	const char *key = request->key;
	uint32_t part_count = (key != NULL) ? mp_decode_array(&key) : 0;

	struct iterator *it = index->position();
	key_validate(index->key_def, type, key, part_count);
	index->initIterator(it, type, key, part_count);

	struct tuple *tuple;
	while ((tuple = it->next(it)) != NULL) {
		if (offset > 0) {
			offset--;
			continue;
		}
		if (limit == found++)
			break;
		port_add_tuple(port, tuple);
	}
}

static void
execute_delete(struct request *request, struct txn *txn, struct port *port)
{
	(void) port;
	if (unlikely(request->key == NULL))
		tnt_raise(ClientError, ER_MISSING_REQUEST_FIELD, "key");
	txn_add_redo(txn, request);
	struct space *space = space_find(request->space_id);

	/* Try to find tuple by primary key */
	Index *pk = index_find(space, 0);
	const char *key = request->key;
	uint32_t part_count = mp_decode_array(&key);
	primary_key_validate(pk->key_def, key, part_count);
	struct tuple *old_tuple = pk->findByKey(key, part_count);

	if (old_tuple == NULL)
		return;

	txn_replace(txn, space, old_tuple, NULL, DUP_REPLACE_OR_INSERT);
}

void
request_check_type(uint32_t type)
{
	if (type < IPROTO_SELECT || type >= IPROTO_DML_REQUEST_MAX)
		tnt_raise(IllegalParams, "unknown request type %u", type);
}

void
request_create(struct request *request, uint32_t type)
{
	request_check_type(type);
	static const request_execute_f execute_map[] = {
		NULL, execute_select, execute_replace, execute_replace,
		execute_update, execute_delete, box_lua_call
	};
	memset(request, 0, sizeof(*request));
	request->type = type;
	request->execute = execute_map[type];
}

void
request_decode(struct request *request, const char *data, uint32_t len)
{
	assert(request->type != 0);
	const char *end = data + len;
	request->data = data;
	request->len = len;

	if (mp_typeof(*data) != MP_MAP || mp_check_map(data, end) > 0) {
error:
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "packet body");
	}
	uint32_t size = mp_decode_map(&data);
	for (uint32_t i = 0; i < size; i++) {
		if (! iproto_body_has_key(data, end)) {
			mp_check(&data, end);
			mp_check(&data, end);
			continue;
		}
		unsigned char key = mp_decode_uint(&data);
		const char *value = data;
		if (mp_check(&data, end))
			goto error;
		if (iproto_key_type[key] != mp_typeof(*value))
			goto error;
		switch (key) {
		case IPROTO_SPACE_ID:
			request->space_id = mp_decode_uint(&value);
			break;
		case IPROTO_INDEX_ID:
			request->index_id = mp_decode_uint(&value);
			break;
		case IPROTO_OFFSET:
			request->offset = mp_decode_uint(&value);
			break;
		case IPROTO_LIMIT:
			request->limit = mp_decode_uint(&value);
			break;
		case IPROTO_ITERATOR:
			request->iterator = mp_decode_uint(&value);
			break;
		case IPROTO_TUPLE:
			request->tuple = value;
			request->tuple_end = data;
			break;
		case IPROTO_KEY:
		case IPROTO_FUNCTION_NAME:
		default:
			request->key = value;
			request->key_end = data;
			break;
		}
	}
#ifndef NDEBUG
	if (data != end)
		tnt_raise(ClientError, ER_INVALID_MSGPACK, "packet end");
#endif
}

void
request_encode(struct request *request)
{
	assert(request->data == NULL);
	const int HEADER_LEN_MAX = 32;
	uint32_t tuple_len = request->tuple_end - request->tuple;
	uint32_t key_len = request->key_end - request->key;
	uint32_t len = HEADER_LEN_MAX + tuple_len + key_len;
	request->data = (char *) region_alloc(&fiber()->gc, len);
	char *d = (char *) request->data + 1; /* Skip 1 byte for MP_MAP */
	int map_size = 0;
	if (true) {
		d = mp_encode_uint(d, IPROTO_SPACE_ID);
		d = mp_encode_uint(d, request->space_id);
		map_size++;
	}
	if (request->index_id) {
		d = mp_encode_uint(d, IPROTO_INDEX_ID);
		d = mp_encode_uint(d, request->index_id);
		map_size++;
	}
	if (request->key) {
		d = mp_encode_uint(d, IPROTO_KEY);
		memcpy(d, request->key, key_len);
		d += key_len;
		map_size++;
	}
	if (request->tuple) {
		d = mp_encode_uint(d, IPROTO_TUPLE);
		memcpy(d, request->tuple, tuple_len);
		d += tuple_len;
		map_size++;
	}
	request->len = (d - request->data);
	assert(request->len <= len);
	mp_encode_map((char *) request->data, map_size);
}

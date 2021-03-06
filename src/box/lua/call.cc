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
#include "box/lua/call.h"

#include <arpa/inet.h>

#include "box/lua/tuple.h"
#include "box/lua/index.h"
#include "box/lua/space.h"
#include "box/tuple.h"

#include "lua/utils.h"
#include "lua/msgpack.h"
#include "tbuf.h"
#include "fiber.h"
#include "scoped_guard.h"
#include "box/box.h"
#include "box/port.h"
#include "box/request.h"
#include "bit/bit.h"

/* contents of box.lua, misc.lua, box.net.lua respectively */
extern char schema_lua[], box_lua[], box_net_lua[], misc_lua[] ;
static const char *lua_sources[] = { schema_lua, box_lua, box_net_lua, misc_lua, NULL };

/*
 * Functions, exported in box_lua.h should have prefix
 * "box_lua_"; functions, available in Lua "box"
 * should start with "lbox_".
 */

/** {{{ Lua I/O: facilities to intercept box output
 * and push into Lua stack.
 */

struct port_lua
{
	struct port_vtab *vtab;
	struct lua_State *L;
};

static inline struct port_lua *
port_lua(struct port *port) { return (struct port_lua *) port; }

/*
 * For addU32/dupU32 do nothing -- the only uint32_t Box can give
 * us is tuple count, and we don't need it, since we intercept
 * everything into Lua stack first.
 * @sa port_add_lua_multret
 */

static void
port_lua_add_tuple(struct port *port, struct tuple *tuple)
{
	lua_State *L = port_lua(port)->L;
	try {
		lbox_pushtuple(L, tuple);
	} catch (...) {
		tnt_raise(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
	}
}


static struct port *
port_lua_create(struct lua_State *L)
{
	static struct port_vtab port_lua_vtab = {
		port_lua_add_tuple,
		null_port_eof,
	};
	struct port_lua *port = (struct port_lua *)
			region_alloc(&fiber()->gc, sizeof(struct port_lua));
	port->vtab = &port_lua_vtab;
	port->L = L;
	return (struct port *) port;
}

static void
port_lua_process_add_tuple(struct port *port, struct tuple *tuple)
{
	lua_State *L = port_lua(port)->L;
	try {
		int idx = luaL_getn(L, -1);	/* TODO: can be optimized */
		lbox_pushtuple(L, tuple);
		lua_rawseti(L, -2, idx + 1);

	} catch (...) {
		tnt_raise(ClientError, ER_PROC_LUA, lua_tostring(L, -1));
	}
}

static struct port *
port_lua_process_create(struct lua_State *L)
{
	static struct port_vtab port_lua_vtab = {
		port_lua_process_add_tuple,
		null_port_eof,
	};
	struct port_lua *port = (struct port_lua *)
			region_alloc(&fiber()->gc, sizeof(struct port_lua));
	port->vtab = &port_lua_vtab;
	port->L = L;
	return (struct port *) port;
}

static void
port_add_lua_ret(struct port *port, struct lua_State *L, int index)
{
	struct tuple *tuple = lua_totuple(L, index, index);
	TupleGuard guard(tuple);
	port_add_tuple(port, tuple);
}

/**
 * Add all elements from Lua stack to fiber iov.
 *
 * To allow clients to understand a complex return from
 * a procedure, we are compatible with SELECT protocol,
 * and return the number of return values first, and
 * then each return value as a tuple.
 *
 * If a Lua stack contains at least one scalar, each
 * value on the stack is converted to a tuple. A Lua
 * is converted to a tuple with multiple fields.
 *
 * If the stack is a Lua table, each member of which is
 * not scalar, each member of the table is converted to
 * a tuple. This way very large lists of return values can
 * be used, since Lua stack size is limited by 8000 elements,
 * while Lua table size is pretty much unlimited.
 */
static void
port_add_lua_multret(struct port *port, struct lua_State *L)
{
	int nargs = lua_gettop(L);
	/** Check if we deal with a table of tables. */
	if (nargs == 1 && lua_istable(L, 1)) {
		/*
		 * The table is not empty and consists of tables
		 * or tuples. Treat each table element as a tuple,
		 * and push it.
		 */
		lua_pushnil(L);
		int has_keys = lua_next(L, 1);
		if (has_keys  && (lua_istable(L, -1) || lua_istuple(L, -1))) {
			do {
				port_add_lua_ret(port, L, lua_gettop(L));
				lua_pop(L, 1);
			} while (lua_next(L, 1));
			return;
		} else if (has_keys) {
			lua_pop(L, 1);
		}
	}
	for (int i = 1; i <= nargs; ++i) {
		port_add_lua_ret(port, L, i);
	}
}

/* }}} */

/**
 * The main extension provided to Lua by Tarantool/Box --
 * ability to call INSERT/UPDATE/SELECT/DELETE from within
 * a Lua procedure.
 *
 * This is a low-level API, and it expects
 * all arguments to be packed in accordance
 * with the binary protocol format (iproto
 * header excluded).
 *
 * Signature:
 * box.process(op_code, request)
 */
static int
lbox_process(lua_State *L)
{
	uint32_t op = lua_tointeger(L, 1); /* Get the first arg. */
	size_t sz;
	const char *req = luaL_checklstring(L, 2, &sz); /* Second arg. */
	if (op == IPROTO_CALL) {
		/*
		 * We should not be doing a CALL from within a CALL.
		 * To invoke one stored procedure from another, one must
		 * do it in Lua directly. This deals with
		 * infinite recursion, stack overflow and such.
		 */
		return luaL_error(L, "box.process(CALL, ...) is not allowed");
	}
	int top = lua_gettop(L); /* to know how much is added by rw_callback */
	lua_newtable(L);

	size_t allocated_size = region_used(&fiber()->gc);
	struct port *port_lua = port_lua_process_create(L);
	try {
		struct request request;
		request_create(&request, op);
		request_decode(&request, req, sz);
		box_process(port_lua, &request);

		/*
		 * This only works as long as port_lua doesn't
		 * use fiber->cleanup and fiber->gc.
		 */
		region_truncate(&fiber()->gc, allocated_size);
	} catch (Exception *e) {
		region_truncate(&fiber()->gc, allocated_size);
		throw;
	}
	return lua_gettop(L) - top;
}

static struct request *
lbox_request_create(struct lua_State *L, enum iproto_request_type type,
		    int key, int tuple)
{
	struct request *request = (struct request *)
		region_alloc(&fiber()->gc, sizeof(struct request));
	request_create(request, type);
	request->space_id = lua_tointeger(L, 1);
	if (key > 0) {
		struct tbuf *key_buf = tbuf_new(&fiber()->gc);
		luamp_encode(L, key_buf, key);
		request->key = key_buf->data;
		request->key_end = key_buf->data + key_buf->size;
		if (mp_typeof(*request->key) != MP_ARRAY)
			tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
	}
	if (tuple > 0) {
		struct tbuf *tuple_buf = tbuf_new(&fiber()->gc);
		luamp_encode(L, tuple_buf, tuple);
		request->tuple = tuple_buf->data;
		request->tuple_end = tuple_buf->data + tuple_buf->size;
		if (mp_typeof(*request->tuple) != MP_ARRAY)
			tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
	}
	return request;
}

static void
port_ffi_add_tuple(struct port *port, struct tuple *tuple)
{
	struct port_ffi *port_ffi = (struct port_ffi *) port;
	if (port_ffi->size >= port_ffi->capacity) {
		uint32_t capacity = (port_ffi->capacity > 0) ?
				2 * port_ffi->capacity : 1024;
		struct tuple **ret = (struct tuple **)
			realloc(port_ffi->ret, sizeof(*ret) * capacity);
		assert(ret != NULL);
		port_ffi->ret = ret;
		port_ffi->capacity = capacity;
	}
	port_ffi->ret[port_ffi->size++] = tuple;
}

struct port_vtab port_ffi_vtab = {
	port_ffi_add_tuple,
	null_port_eof,
};

void
port_ffi_create(struct port_ffi *port)
{
	memset(port, 0, sizeof(*port));
	port->vtab = &port_ffi_vtab;
}

void
port_ffi_destroy(struct port_ffi *port)
{
	free(port->ret);
	port->capacity = port->size = 0;
}

int
boxffi_select(struct port *port, uint32_t space_id, uint32_t index_id,
	      int iterator, uint32_t offset, uint32_t limit,
	      const char *key, const char *key_end)
{
	struct request request;
	request_create(&request, IPROTO_SELECT);
	request.space_id = space_id;
	request.index_id = index_id;
	request.limit = limit;
	request.offset = offset;
	request.iterator = iterator;
	request.key = key;
	request.key_end = key_end;

	try {
		box_process(port, &request);
		return 0;
	} catch (Exception *e) {
		/* will be hanled by box.raise() in Lua */
		return -1;
	}
}

static int
lbox_insert(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1))
		return luaL_error(L, "Usage space:insert(tuple)");

	RegionGuard region_guard(&fiber()->gc);
	struct request *request = lbox_request_create(L, IPROTO_INSERT,
						      -1, 2);
	box_process(port_lua_create(L), request);
	return lua_gettop(L) - 2;
}

static int
lbox_replace(lua_State *L)
{
	if (lua_gettop(L) != 2 || !lua_isnumber(L, 1))
		return luaL_error(L, "Usage space:replace(tuple)");

	RegionGuard region_guard(&fiber()->gc);
	struct request *request = lbox_request_create(L, IPROTO_REPLACE,
						      -1, 2);
	box_process(port_lua_create(L), request);
	return lua_gettop(L) - 2;
}

static int
lbox_update(lua_State *L)
{
	if (lua_gettop(L) != 4 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "Usage space:update(key, ops)");

	RegionGuard region_guard(&fiber()->gc);
	struct request *request = lbox_request_create(L, IPROTO_UPDATE,
						      3, 4);
	/* Ignore index_id for now */
	box_process(port_lua_create(L), request);
	return lua_gettop(L) - 4;
}

static int
lbox_delete(lua_State *L)
{
	if (lua_gettop(L) != 3 || !lua_isnumber(L, 1) || !lua_isnumber(L, 2))
		return luaL_error(L, "Usage space:delete(key)");

	RegionGuard region_guard(&fiber()->gc);
	struct request *request = lbox_request_create(L, IPROTO_DELETE,
						      3, -1);
	/* Ignore index_id for now */
	box_process(port_lua_create(L), request);
	return lua_gettop(L) - 3;
}

static int
lbox_raise(lua_State *L)
{
	if (lua_gettop(L) == 0) {
		/* re-throw saved exceptions (if any) */
		if (cord()->exception == NULL)
			return 0;
		cord()->exception->raise();
		return 0;
	}

	if (lua_gettop(L) < 2)
		luaL_error(L, "box.raise(): bad arguments");
	uint32_t code = lua_tointeger(L, 1);
	if (!code)
		luaL_error(L, "box.raise(): unknown error code");
	const char *str = lua_tostring(L, 2);
	tnt_raise(ClientError, str, code);
	return 0;
}

/**
 * A helper to find a Lua function by name and put it
 * on top of the stack.
 */
static int
box_lua_find(lua_State *L, const char *name, const char *name_end)
{
	int index = LUA_GLOBALSINDEX;
	int objstack = 0;
	const char *start = name, *end;

	while ((end = (const char *) memchr(start, '.', name_end - start))) {
		lua_checkstack(L, 3);
		lua_pushlstring(L, start, end - start);
		lua_gettable(L, index);
		if (! lua_istable(L, -1))
			tnt_raise(ClientError, ER_NO_SUCH_PROC,
				  name_end - name, name);
		start = end + 1; /* next piece of a.b.c */
		index = lua_gettop(L); /* top of the stack */
	}

	/* box.something:method */
	if ((end = (const char *) memchr(start, ':', name_end - start))) {
		lua_checkstack(L, 3);
		lua_pushlstring(L, start, end - start);
		lua_gettable(L, index);
		if (! (lua_istable(L, -1) ||
			lua_islightuserdata(L, -1) || lua_isuserdata(L, -1) ))
				tnt_raise(ClientError, ER_NO_SUCH_PROC,
					  name_end - name, name);
		start = end + 1; /* next piece of a.b.c */
		index = lua_gettop(L); /* top of the stack */
		objstack = index;
	}


	lua_pushlstring(L, start, name_end - start);
	lua_gettable(L, index);
	if (! lua_isfunction(L, -1)) {
		/* lua_call or lua_gettable would raise a type error
		 * for us, but our own message is more verbose. */
		tnt_raise(ClientError, ER_NO_SUCH_PROC,
			  name_end - name, name);
	}
	/* setting stack that it would contain only
	 * the function pointer. */
	if (index != LUA_GLOBALSINDEX) {
		if (objstack == 0) {        /* no object, only a function */
			lua_replace(L, 1);
		} else if (objstack == 1) { /* just two values, swap them */
			lua_insert(L, -2);
		} else {		    /* long path */
			lua_insert(L, 1);
			lua_insert(L, 2);
			objstack = 1;
		}
		lua_settop(L, 1 + objstack);
	}
	return 1 + objstack;
}


/**
 * A helper to find lua stored procedures for box.call.
 * box.call iteslf is pure Lua, to avoid issues
 * with infinite call recursion smashing C
 * thread stack.
 */

static int
lbox_call_loadproc(struct lua_State *L)
{
	const char *name;
	size_t name_len;
	name = lua_tolstring(L, 1, &name_len);
	return box_lua_find(L, name, name + name_len);
}

/**
 * Invoke a Lua stored procedure from the binary protocol
 * (implementation of 'CALL' command code).
 */
void
box_lua_call(struct request *request, struct txn *txn,
	     struct port *port)
{
	(void) txn;
	lua_State *L = lua_newthread(tarantool_L);
	LuarefGuard coro_ref(tarantool_L);
	const char *name = request->key;
	uint32_t name_len = mp_decode_strl(&name);

	/* proc name */
	int oc = box_lua_find(L, name, name + name_len);
	/* Push the rest of args (a tuple). */
	const char *args = request->tuple;
	uint32_t arg_count = mp_decode_array(&args);
	luaL_checkstack(L, arg_count, "call: out of stack");

	for (uint32_t i = 0; i < arg_count; i++) {
		luamp_decode(L, &args);
	}
	lbox_call(L, arg_count + oc - 1, LUA_MULTRET);
	/* Send results of the called procedure to the client. */
	port_add_lua_multret(port, L);
}

/**
 * Convert box.pack() format specifier to Tarantool
 * binary protocol UPDATE opcode
 */
static char format_to_opcode(char format)
{
	switch (format) {
	case '=': return 0;
	case '+': return 1;
	case '&': return 2;
	case '^': return 3;
	case '|': return 4;
	case ':': return 5;
	case '#': return 6;
	case '!': return 7;
	case '-': return 8;
	default: return format;
	}
}

/**
 * Counterpart to @a format_to_opcode
 */
static char opcode_to_format(char opcode)
{
	switch (opcode) {
	case 0: return '=';
	case 1: return '+';
	case 2: return '&';
	case 3: return '^';
	case 4: return '|';
	case 5: return ':';
	case 6: return '#';
	case 7: return '!';
	case 8: return '-';
	default: return opcode;
	}
}

/**
 * To use Tarantool/Box binary protocol primitives from Lua, we
 * need a way to pack Lua variables into a binary representation.
 * We do it by exporting a helper function
 *
 * box.pack(format, args...)
 *
 * which takes the format, which is very similar to Perl 'pack'
 * format, and a list of arguments, and returns a binary string
 * which has the arguments packed according to the format.
 *
 * For example, a typical SELECT packet packs in Lua like this:
 *
 * pkt = box.pack("iiiiiip", -- pack format
 *                         0, -- space id
 *                         0, -- index id
 *                         0, -- offset
 *                         2^32, -- limit
 *                         1, -- number of SELECT arguments
 *                         1, -- tuple cardinality
 *                         key); -- the key to use for SELECT
 *
 * @sa doc/box-protocol.txt, binary protocol description
 * @todo: implement box.unpack(format, str), for testing purposes
 */
static int
lbox_pack(struct lua_State *L)
{
	const char *format = luaL_checkstring(L, 1);
	/* first arg comes second */
	int i = 2;
	int nargs = lua_gettop(L);
	size_t size;
	const char *str;

	RegionGuard region_guard(&fiber()->gc);
	struct tbuf *b = tbuf_new(&fiber()->gc);

	struct luaL_field field;
	double dbl;
	float flt;
	char *data;
	while (*format) {
		if (i > nargs)
			luaL_error(L, "box.pack: argument count does not match "
				   "the format");
		luaL_tofield(L, i, &field);
		switch (*format) {
		case 'B':
		case 'b':
			/* signed and unsigned 8-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 8-bit int");

			tbuf_append(b, (char *) &field.ival, sizeof(uint8_t));
			break;
		case 'S':
		case 's':
			/* signed and unsigned 16-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 16-bit int");

			tbuf_append(b, (char *) &field.ival, sizeof(uint16_t));
			break;
		case 'n':
			/* signed and unsigned 16-bit big endian integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 16-bit int");

			field.ival = (uint16_t) htons((uint16_t) field.ival);
			tbuf_append(b, (char *) &field.ival, sizeof(uint16_t));
			break;
		case 'I':
		case 'i':
			/* signed and unsigned 32-bit integers */
			if (field.type != MP_UINT && field.ival != MP_INT)
				luaL_error(L, "box.pack: expected 32-bit int");

			tbuf_append(b, (char *) &field.ival, sizeof(uint32_t));
			break;
		case 'N':
			/* signed and unsigned 32-bit big endian integers */
			if (field.type != MP_UINT && field.ival != MP_INT)
				luaL_error(L, "box.pack: expected 32-bit int");

			field.ival = htonl(field.ival);
			tbuf_append(b, (char *) &field.ival, sizeof(uint32_t));
			break;
		case 'L':
		case 'l':
			/* signed and unsigned 64-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 64-bit int");

			tbuf_append(b, (char *) &field.ival, sizeof(uint64_t));
			break;
		case 'Q':
		case 'q':
			/* signed and unsigned 64-bit integers */
			if (field.type != MP_UINT && field.type != MP_INT)
				luaL_error(L, "box.pack: expected 64-bit int");

			field.ival = bswap_u64(field.ival);
			tbuf_append(b, (char *) &field.ival, sizeof(uint64_t));
			break;
		case 'd':
			dbl = (double) lua_tonumber(L, i);
			tbuf_append(b, (char *) &dbl, sizeof(double));
			break;
		case 'f':
			flt = (float) lua_tonumber(L, i);
			tbuf_append(b, (char *) &flt, sizeof(float));
			break;
		case 'A':
		case 'a':
			/* A sequence of bytes */
			str = luaL_checklstring(L, i, &size);
			tbuf_append(b, str, size);
			break;
		case 'P':
		case 'p':
			luamp_encode(L, b, i);
			break;
		case 'V':
		{
			int arg_count = luaL_checkint(L, i);
			if (i + arg_count > nargs)
				luaL_error(L, "box.pack: argument count does not match "
					   "the format");
			int first = i + 1;
			int last = i + arg_count;
			i += luamp_encodestack(L, b, first, last);
			break;
		}
		case '=':
			/* update tuple set foo = bar */
		case '+':
			/* set field += val */
		case '-':
			/* set field -= val */
		case '&':
			/* set field & =val */
		case '|':
			/* set field |= val */
		case '^':
			/* set field ^= val */
		case ':':
			/* splice */
		case '#':
			/* delete field */
		case '!':
			/* insert field */
			/* field no */
			tbuf_ensure(b, sizeof(uint32_t) + 1);
			data = b->data + b->size;

			data = pack_u32(data, lua_tointeger(L, i));
			*data++ = format_to_opcode(*format);

			assert(data <= b->data + b->capacity);
			b->size = data - b->data;
			break;
		default:
			luaL_error(L, "box.pack: unsupported pack "
				   "format specifier '%c'", *format);
		}
		i++;
		format++;
	}

	lua_pushlstring(L, tbuf_str(b), b->size);

	return 1;
}

const char *
box_unpack_response(struct lua_State *L, const char *s, const char *end)
{
	uint32_t tuple_count = pick_u32(&s, end);

	/* Unpack and push tuples. */
	while (tuple_count--) {
		const char *t = s;
		if (unlikely(mp_check(&s, end)))
			tnt_raise(ClientError, ER_INVALID_MSGPACK);
		if (unlikely(mp_typeof(*t) != MP_ARRAY))
			tnt_raise(ClientError, ER_TUPLE_NOT_ARRAY);
		struct tuple *tuple = tuple_new(tuple_format_ber, t, s);
		lbox_pushtuple(L, tuple);
	}
	return s;
}

static int
lbox_unpack(struct lua_State *L)
{
	size_t format_size = 0;
	const char *format = luaL_checklstring(L, 1, &format_size);
	const char *f = format;

	size_t str_size = 0;
	const char *str =  luaL_checklstring(L, 2, &str_size);
	const char *end = str + str_size;
	const char *s = str;

	int save_stacksize = lua_gettop(L);

	char charbuf;
	uint8_t  u8buf;
	uint16_t u16buf;
	uint32_t u32buf;
	double dbl;
	float flt;

#define CHECK_SIZE(cur) if (unlikely((cur) >= end)) {	                \
	luaL_error(L, "box.unpack('%c'): got %d bytes (expected: %d+)",	\
		   *f, (int) (end - str), (int) 1 + ((cur) - str));	\
}
	while (*f) {
		switch (*f) {
		case 'b':
			CHECK_SIZE(s);
			u8buf = *(uint8_t *) s;
			lua_pushnumber(L, u8buf);
			s++;
			break;
		case 's':
			CHECK_SIZE(s + 1);
			u16buf = *(uint16_t *) s;
			lua_pushnumber(L, u16buf);
			s += 2;
			break;
		case 'n':
			CHECK_SIZE(s + 1);
			u16buf = ntohs(*(uint16_t *) s);
			lua_pushnumber(L, u16buf);
			s += 2;
			break;
		case 'i':
			CHECK_SIZE(s + 3);
			u32buf = *(uint32_t *) s;
			lua_pushnumber(L, u32buf);
			s += 4;
			break;
		case 'N':
			CHECK_SIZE(s + 3);
			u32buf = ntohl(*(uint32_t *) s);
			lua_pushnumber(L, u32buf);
			s += 4;
			break;
		case 'l':
			CHECK_SIZE(s + 7);
			luaL_pushnumber64(L, *(uint64_t*) s);
			s += 8;
			break;
		case 'q':
			CHECK_SIZE(s + 7);
			luaL_pushnumber64(L, bswap_u64(*(uint64_t*) s));
			s += 8;
			break;
		case 'd':
			CHECK_SIZE(s + 7);
			dbl = *(double *) s;
			lua_pushnumber(L, dbl);
			s += 8;
			break;
		case 'f':
			CHECK_SIZE(s + 3);
			flt = *(float *) s;
			lua_pushnumber(L, flt);
			s += 4;
			break;
		case 'a':
		case 'A': /* The rest of the data is a Lua string. */
			lua_pushlstring(L, s, end - s);
			s = end;
			break;
		case 'P':
		case 'p':
		{
			const char *data = s;
			if (unlikely(mp_check(&s, end)))
				tnt_raise(ClientError, ER_INVALID_MSGPACK);
			luamp_decode(L, &data);
			assert(data == s);
			break;
		}
		case '=':
			/* update tuple set foo = bar */
		case '+':
			/* set field += val */
		case '-':
			/* set field -= val */
		case '&':
			/* set field & =val */
		case '|':
			/* set field |= val */
		case '^':
			/* set field ^= val */
		case ':':
			/* splice */
		case '#':
			/* delete field */
		case '!':
			/* insert field */
			CHECK_SIZE(s + 4);

			/* field no */
			u32buf = *(uint32_t *) s;

			/* opcode */
			charbuf = *(s + 4);
			charbuf = opcode_to_format(charbuf);
			if (charbuf != *f) {
				luaL_error(L, "box.unpack('%s'): "
					   "unexpected opcode: "
					   "offset %d, expected '%c',"
					   "found '%c'",
					   format, s - str, *f, charbuf);
			}

			lua_pushnumber(L, u32buf);
			s += 5;
			break;

		case 'R': /* Unpack server response, IPROTO format. */
		{
			s = box_unpack_response(L, s, end);
			break;
		}
		default:
			luaL_error(L, "box.unpack: unsupported "
				   "format specifier '%c'", *f);
		}
		f++;
	}

	assert(s <= end);

	if (s != end) {
		luaL_error(L, "box.unpack('%s'): too many bytes: "
			   "unpacked %d, total %d",
			   format, s - str, str_size);
	}

	return lua_gettop(L) - save_stacksize;

#undef CHECK_SIZE
}

static const struct luaL_reg boxlib[] = {
	{"process", lbox_process},
	{"_insert", lbox_insert},
	{"_replace", lbox_replace},
	{"_update", lbox_update},
	{"_delete", lbox_delete},
	{"call_loadproc",  lbox_call_loadproc},
	{"raise", lbox_raise},
	{"pack", lbox_pack},
	{"unpack", lbox_unpack},
	{NULL, NULL}
};

void
box_lua_init(struct lua_State *L)
{
	luaL_register(L, "box", boxlib);
	lua_pop(L, 1);
	box_lua_tuple_init(L);
	box_lua_index_init(L);
	box_lua_space_init(L);

	/* Load Lua extension */
	for (const char **s = lua_sources; *s; s++) {
		if (luaL_dostring(L, *s))
			panic("Error loading Lua source %.160s...: %s",
			      *s, lua_tostring(L, -1));
	}

	assert(lua_gettop(L) == 0);
}

#ifndef INCLUDES_TARANTOOL_SESSION_H
#define INCLUDES_TARANTOOL_SESSION_H
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
#include <inttypes.h>
#include <stdbool.h>
#include "trigger.h"

enum {	SESSION_SEED_SIZE = 32 };
/** Predefined user ids. */
enum { GUID = 0, SUID =  1 };

/**
 * Abstraction of a single user session:
 * for now, only provides accounting of established
 * sessions and on-connect/on-disconnect event
 * handling, in future: user credentials, protocol, etc.
 * Session identifiers grow monotonically.
 * 0 sid is reserved to mean 'no session'.
 */

struct session {
	/** Session id. */
	uint32_t id;
	/** File descriptor. */
	int fd;
	/** Peer cookie - description of the peer. */
	uint64_t cookie;
	/** Authentication salt. */
	int salt[SESSION_SEED_SIZE/sizeof(int)];
	/** A look up key to quickly find session user. */
	uint8_t auth_token;
	uint32_t uid;
};

/**
 * Create a session.
 * Invokes a Lua trigger box.session.on_connect if it is
 * defined. Issues a new session identifier.
 * Must called by the networking layer
 * when a new connection is established.
 *
 * @return handle for a created session
 * @exception tnt_Exception or lua error if session
 * trigger fails or runs out of resources.
 */
struct session *
session_create(int fd, uint64_t cookie);

/**
 * Destroy a session.
 * Must be called by the networking layer on disconnect.
 * Invokes a Lua trigger box.session.on_disconnect if it
 * is defined.
 * @param session   session to destroy. may be NULL.
 *
 * @exception none
 */
void
session_destroy(struct session *);

/**
 * Return a file descriptor
 * associated with a session, or -1 if the
 * session doesn't exist.
 */
int
session_fd(uint32_t sid);

/**
 * Check whether a session exists or not.
 */
static inline bool
session_exists(uint32_t sid)
{
	return session_fd(sid) >= 0;
}

/** Set session auth token and user id. */
static inline void
session_set_user(struct session *session, uint8_t auth_token, uint32_t uid)
{
	session->auth_token = auth_token;
	session->uid = uid;
}

/* The global on-connect trigger. */
extern struct rlist session_on_connect;
/* The global on-disconnect trigger. */
extern struct rlist session_on_disconnect;

void
session_init();

void
session_free();

void
session_storage_cleanup(int sid);
#endif /* INCLUDES_TARANTOOL_SESSION_H */

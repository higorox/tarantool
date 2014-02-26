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
#include "access.h"

struct user users[BOX_USER_MAX];
/** Bitmap of used/unused tokens */
typedef unsigned long user_map_t;

user_map_t user_map[BOX_USER_MAX/(CHAR_BIT*sizeof(user_map_t)) + 1];
int user_map_idx = 0;
struct mh_i32ptr_t *user_registry;

uint8_t
user_map_get_slot()
{
        uint32_t idx = __builtin_ffsl(user_map[user_map_idx]);
        while (idx == 0) {
		if (user_map_idx == sizeof(user_map)/sizeof(*user_map)) {
			assert(false);
			return 0;
		}
		user_map_idx++;
                idx = __builtin_ffsl(user_map[user_map_idx]);
        }
        /*
         * find-first-set returns bit index starting from 1,
         * or 0 if no bit is set. Rebase the index to offset 0.
         */
        idx--;
	user_map[user_map_idx] ^= ((user_map_t) 1) << idx;
	idx += user_map_idx * sizeof(*user_map) * CHAR_BIT;
	assert(idx < UINT8_MAX);
	return idx;
}

void
user_map_put_slot(uint8_t auth_token)
{
	memset(users + auth_token, 0, sizeof(struct user));
	uint32_t bit_no = auth_token & (sizeof(user_map_t) * CHAR_BIT - 1);
	auth_token /= sizeof(user_map_t) * CHAR_BIT;
	user_map[auth_token] |= ((user_map_t) 1) << bit_no;
	if (auth_token > user_map_idx)
		user_map_idx = auth_token;
}

const char *
priv_name(uint8_t access)
{
	switch (access) {
	case PRIV_R: return "Read";
	case PRIV_W: return "Write";
	default: return "Execute";
	}
}

void
user_replace(struct user *user)
{
	struct user *old = user_find(user->uid);
	if (old == NULL) {
		user->auth_token = user_map_get_slot();
		old = &users[user->auth_token];
		assert(old->auth_token == 0);
	} else {
		mh_i32ptr_del(users_registry, old);
		user->auth_token = old->auth_token;
	}
	*old = *user;
	mh_i32ptr_put(users_registry, old);
}

void
user_delete(uint32_t uid)
{
	struct user *old = user_find(uid);
	assert(old->auth_token > SUID);
	user_map_put_slot(old->auth_token);
	mh_i32ptr_del(users_registry, old);
}

/** Find user by id. */
struct user *
user_find(uint32_t uid)
{
	mh_i32ptr_get(users_registry, uid);
	(void) uid;
	return NULL;
}

void
user_init()
{
	memset(user_map, 0xFF, sizeof(user_map));
	users_registry = mh_i32ptr_new();
	/*
	 * Solve a chicken-egg problem:
	 * we need a functional user cache entry for superuser to
	 * perform recovery, but the superuser credentials are
	 * stored in the snapshot. So, pre-create cache entries
	 * for guest user and admin users here, they will be
	 * modified with snapshot contents during recovery.
	 */
	struct user guest;
	memset(&guest, 0, sizeof(guest));
	snprintf(guest.name, sizeof(guest.name), "guest");
	user_replace(&guest);
	/* 0 is the auth token and user id by default. */
	assert(guest.auth_token == 0 &&
	       users[guest.auth_token].uid == guest.uid);

	struct user admin;
	memset(&admin, 0, sizeof(admin));
	snprintf(guest.name, sizeof(guest.name), "admin");
	admin.uid = SUID;
	user_replace(&admin);
	assert(admin.auth_token == SUID &&
	       users[admin.auth_token].uid == SUID);
}

void
user_free()
{
	if (users_registry)
		mh_i32ptr_del(users_registy);
}

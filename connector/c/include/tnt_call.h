#ifndef TNT_CALL_H_INCLUDED
#define TNT_CALL_H_INCLUDED

/*
 * Copyright (C) 2011 Mail.RU
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * @defgroup Call
 * @ingroup  Operations
 * @brief Call operation
 *
 * @{
 */

/**
 * Call operation.
 *
 * If bufferization is in use, then request would be placed in
 * internal buffer for later sending. Otherwise, operation
 * would be processed immediately.
 *
 * @param t handler pointer
 * @param reqid user supplied integer value
 * @param flags operation flags
 * @param proc procedure name
 * @param fmt printf-alike format (%s, %*s, %d, %l, %ll, %ul, %ull are supported)
 * @param args tuple containing passing arguments 
 * @returns 0 on success, -1 on error
 */
int tnt_call_tuple(struct tnt *t, int reqid, int flags, char *proc,
		   struct tnt_tuple *args);

int tnt_call(struct tnt *t, int reqid, int flags, char *proc,
	     char *fmt, ...)
             __attribute__ ((format(printf, 5, 6)));
/** @} */

#endif /* TNT_CALL_H_INCLUDED */
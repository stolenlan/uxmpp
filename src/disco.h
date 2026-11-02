/*
 * Copyright (c) 2026 Tyler Anderson <achaean@sinelabs.ca>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef _DISCO_H
#define _DISCO_H 1

#include <stdio.h>
#include <strophe.h>

#define DISCO_MAX_RESULT 8192

#define XEP_0157_ADDRESSES \
	"abuse-addresses",\
	"admin-addresses",\
	"feedback-addresses",\
	"sales-addresses",\
	"security-addresses",\
	"status-addresses",\
	"support-addresses"

typedef struct {
	unsigned char init;
	unsigned char active;
	xmpp_conn_t *conn;
	char target[REGSTR_MAX];
	char *result;
	char *p;
	void *ref;
} disco_ctx_t;

void		display_disco_info(disco_ctx_t *dctx,
			xmpp_stanza_t * const child);
void		request_disco_info(disco_ctx_t *dctx, xmpp_handler hdl);
int		disco_out(const char *jid,
			const char *_password,
			const char *host);

#endif

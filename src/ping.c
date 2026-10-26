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

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>
#include <strophe.h>
#include <stdlib.h>
#include <ctype.h>

#include "uxmpp.h"
#include "ping.h"

const char *ping_host;

static int
on_ping_reply( xmpp_conn_t * const conn,
	xmpp_stanza_t * const stanza,void * const userdata)
{
	xmpp_ctx_t *ctx = userdata;
	(void)ctx;
	
	const char *type = xmpp_stanza_get_type(stanza);
	(void)conn; /* Silence compiler! */
	if (type != NULL && strcmp(type, "error") == 0) {
		fprintf(stderr, "Server returned an IQ error.\n");
		xmpp_disconnect(conn);
		return 0;
	}
	
	printf("PONG!\n");

	xmpp_disconnect(conn);
	
	return 0;
}

static void
send_ping(xmpp_conn_t *conn, xmpp_ctx_t *ctx, const char *target)
{
	
	xmpp_stanza_t *iq = xmpp_stanza_new(ctx);
	xmpp_stanza_t *ping = xmpp_stanza_new(ctx);
	char *idstr = xmpp_uuid_gen(ctx);

	if (iq == NULL || ping == NULL) {
		fprintf(stderr, "Could not allocate disco stanza\n");
		if (iq != NULL)
			xmpp_stanza_release(iq);
		if (ping != NULL)
			xmpp_stanza_release(ping);
		return;
	}

	xmpp_stanza_set_name(iq, "iq");
	xmpp_stanza_set_type(iq, "get");
	xmpp_stanza_set_id(iq, idstr);
	xmpp_stanza_set_to(iq, target);
	xmpp_stanza_set_name(ping, "ping");
	xmpp_stanza_set_ns(ping, "urn:xmpp:ping");
	xmpp_stanza_add_child(iq, ping);
	xmpp_id_handler_add(conn, on_ping_reply, idstr, ctx);
	xmpp_send(conn, iq);
	xmpp_stanza_release(ping);
	xmpp_stanza_release(iq);
	xmpp_free(ctx, idstr);
}

static void
on_connect(xmpp_conn_t * const conn,
	xmpp_conn_event_t status,
	int error, 
	xmpp_stream_error_t *stream_error, 
	void * const userdata)
{
	static int connected = 0;
	xmpp_ctx_t *ctx = userdata;

	if (stream_error) {
		fprintf(stderr, "Connection error: %s\n",
			stream_error->text);
		xmpp_stop(ctx);
	}

	if (status == XMPP_CONN_CONNECT) {
		connected = 1;
		send_ping(conn, ctx, ping_host);
	} else if (status == XMPP_CONN_FAIL) {
		fprintf(stderr, "Connection or authentication "
			"failed (error %d).\n", error);
		xmpp_stop(ctx);
	} else if (status == XMPP_CONN_DISCONNECT) {
		if (!connected)
			fprintf(stderr, "Connection/Authentication (%s) "
				"failed.\n", xmpp_conn_get_jid(conn));
		xmpp_stop(ctx);
	}

}

int
xmpp_ping(const char *jid, const char *password, const char *host)
{
	/* XEP 0199 PING */
	ping_host = host;
	xmpp_ctx_t *ctx;
	xmpp_conn_t *conn;

	if (jid == NULL || password == NULL) {
		fprintf(stderr, "Invalid or missing credentials.\n");
		return 1;
	}

	if (!strlen(jid)) {
		fprintf(stderr, "Invalid or missing credentials.\n");
		return 1;
	}

	xmpp_initialize();

	xmpp_log_t *logger = NULL;
	if (f_debug)
		logger = xmpp_get_default_logger(XMPP_LEVEL_DEBUG);

	ctx = xmpp_ctx_new(NULL, logger);
	if (ctx == NULL) {
		xmpp_shutdown();
		return 1;
	}

	conn = xmpp_conn_new(ctx);
	if (conn == NULL) {
		fprintf(stderr, "Could not start connection.\n");
		xmpp_ctx_free(ctx);
		xmpp_shutdown();
		return 1;
	}

	xmpp_conn_set_jid(conn, jid);
	xmpp_conn_set_pass(conn, password);

	if (xmpp_connect_client(conn, NULL, 0, on_connect, ctx) != 0) 
		fprintf(stderr, "Could not start the connection.\n");
	else
		xmpp_run(ctx);

	xmpp_conn_release(conn);
	xmpp_ctx_free(ctx);
	xmpp_shutdown();

	return 0;
}

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
#include <strophe.h>
#include <string.h>
#include <stdlib.h>
#include "uxmpp.h"
#include "bot.h"

extern int f_debug;


static int
cb_handler(xmpp_conn_t *const conn, xmpp_stanza_t *const stanza,
	void *const userdata) {

	static int reply_sent = 0;

	xmpp_ctx_t *ctx = userdata;

	const char *type = xmpp_stanza_get_type(stanza);
	const char *id = xmpp_stanza_get_id(stanza);

	if (!type || !id)
		return 1;

	fprintf(stderr, "Type: %s, ID: %s\n", type, id);


	if (strncmp(type, "groupchat", strlen(type)) == 0 && id) {
		const char *body = xmpp_message_get_body(stanza);
		// if(xmpp_stanza_to_text(stanza, &buf, &len) == 0) {
		// 	fprintf(stderr, "%s\n", buf);
		// 	// xmpp_free(ctx, sst);
		// }
		if (!body) return 1;

		printf("Received body: %s\n", body);
		if (!reply_sent) {
			xmpp_stanza_t *reply = xmpp_message_new(ctx, "groupchat",
			"moot@conference.xmpp.sinelabs.ca",
			"lksjdlfakjsdlfkajsdlfkjasldkfjadskl");
			xmpp_message_set_body(reply, "HOOOO WEE!");
			xmpp_send(conn, reply);

			reply_sent = 1;
		}
		



		xmpp_free(ctx, &body);
		
	}

	
	return 1;
}

static void
on_connect(xmpp_conn_t * const conn, xmpp_conn_event_t status, int error, 
	xmpp_stream_error_t *stream_error, void * const userdata)
{
	static int connected = 0;
	xmpp_ctx_t *ctx = userdata;

	if (status == XMPP_CONN_CONNECT) {
		// Send presence to the MUC room

		xmpp_stanza_t *presence = xmpp_presence_new(ctx);
		xmpp_stanza_set_id(presence, "achaean@xmpp.sinelabs.ca");
		xmpp_stanza_set_to(presence, "moot@conference.xmpp.sinelabs.ca/shit");
		xmpp_send(conn, presence);
		xmpp_stanza_release(presence);

		// Join the MUC room
		xmpp_handler_add(conn, cb_handler, NULL, NULL, NULL, ctx);
		connected = 1;
	} else if (status == XMPP_CONN_FAIL) {
		fprintf(stderr, "Connection or authentication "
			"failed (error %d).\n", error);
		xmpp_stop(ctx);
	} else if (status == XMPP_CONN_DISCONNECT) {
		if (f_debug)
			fprintf(stderr, "DISCONNECT\n");
		if (!connected)
			fprintf(stderr, "unable to authenticate\n");
		xmpp_stop(ctx);
	}

	(void)stream_error;
}

int
start_bot(const char *jid, const char *muc, const void *usr)
{
	xmpp_ctx_t *ctx;
	xmpp_conn_t *conn;

	const char *password;
	char in_password[256];

	if (jid == NULL)
		jid = getenv("XMPP_JID");

	password = getenv("XMPP_PASSWORD");

	if (jid == NULL) {
		fprintf(stderr, "Missing JID: "
			"use -j or set the XMPP_JID "
			"environment variable.\n");
	}

	if (jid && password == NULL)
		if (pw_prompt("Password: ", in_password))
			password = in_password;

	if (jid == NULL || password == NULL) {
		fprintf(stderr, "Invalid or missing credentials.\n");
		return 1;
	}

	xmpp_initialize();

	printf("Starting bot...\n");

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

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
#include "disco.h"

const char *disco_host;

static char *
addr_label(const char *attribute)
{
	static char abuf[256] = {0};
	char *abuf_p = abuf;
	memset(abuf, '\0', 256);
	if (!attribute)
		return abuf_p;
	snprintf(abuf, sizeof(abuf), "%s", attribute);
	char *attr_p = strsep(&abuf_p, "-");
	for (size_t i = 0; i < strlen(attr_p); i++)
		attr_p[i] = toupper((unsigned char)attr_p[i]);
	return attr_p;
}

static  void
display_disco_info(xmpp_stanza_t * const child)
{
	static const char *address_types[] = {XEP_0157_ADDRESSES, NULL};
	static const int colw = 12;
	const char *name = xmpp_stanza_get_name(child);

	xmpp_ctx_t *ctx = xmpp_stanza_get_context(child);

	int n_atypes = 0;

	for (n_atypes = 0; address_types[n_atypes] != NULL; n_atypes++);

	if (strncmp(name, "feature", strlen(name)) == 0) {
		const char *var = xmpp_stanza_get_attribute(child, "var");
		printf("%*s: %s\n", colw, "FEATURE", var);
	}

	if (strncmp(name, "x", strlen(name)) == 0) {
		xmpp_stanza_t *x = child;

		for (xmpp_stanza_t *field = xmpp_stanza_get_children(x);
			field; field = xmpp_stanza_get_next(field)) {

			const char *fvar = xmpp_stanza_get_attribute(field,
				"var");

			for (int i = 0; i < n_atypes; i++) {
				const char *atype = address_types[i];
				const char *albl = addr_label(atype);
				if (!atype || !albl)
					break;
				if (strncmp(fvar, atype, strlen(fvar)) != 0)
					continue;

				xmpp_stanza_t *value = 
					xmpp_stanza_get_children(field);
				for (; value; value =
					xmpp_stanza_get_next(value)) {
					char *d = xmpp_stanza_get_text(value);
					printf("%*s: %s\n", colw, albl, d);
					xmpp_free(ctx, d);
				}
			}
		}
	}
}

static int
on_disco_reply( xmpp_conn_t * const conn,
	xmpp_stanza_t * const stanza,void * const userdata)
{
	xmpp_ctx_t *ctx = userdata;
	const char *type = xmpp_stanza_get_type(stanza);
	(void)conn; /* Silence compiler! */
	if (type != NULL && strcmp(type, "error") == 0) {
		fprintf(stderr, "Server returned an IQ error.\n");
		xmpp_stop(ctx);
		return 0;
	}
	xmpp_stanza_t *query = xmpp_stanza_get_child_by_name_and_ns(stanza,
		"query", XMPP_NS_DISCO_INFO);
	xmpp_stanza_t *child = xmpp_stanza_get_children(query);
	do {
		display_disco_info(child);
		child = xmpp_stanza_get_next(child);
	} while (child);

	xmpp_stop(ctx);
	
	return 0;
}

static void
request_disco_info(xmpp_conn_t *conn, xmpp_ctx_t *ctx, const char *target)
{
	char disco_id_str[32];
	xmpp_stanza_t *iq = xmpp_stanza_new(ctx);
	xmpp_stanza_t *query = xmpp_stanza_new(ctx);

	if (iq == NULL || query == NULL) {
		fprintf(stderr, "Could not allocate disco stanza\n");
		if (iq != NULL)
			xmpp_stanza_release(iq);
		if (query != NULL)
			xmpp_stanza_release(query);
		return;
	}

#ifdef __OpenBSD__
	snprintf(disco_id_str, 32, "disco1-%08u", arc4random());
#else
	snprintf(disco_id_str, 32, "disco1-%08u", rand());
#endif

	xmpp_stanza_set_name(iq, "iq");
	xmpp_stanza_set_type(iq, "get");
	xmpp_stanza_set_id(iq, disco_id_str);
	xmpp_stanza_set_to(iq, target);
	xmpp_stanza_set_name(query, "query");
	xmpp_stanza_set_ns(query, XMPP_NS_DISCO_INFO);
	xmpp_stanza_add_child(iq, query);
	xmpp_id_handler_add(conn, on_disco_reply, disco_id_str, ctx);
	xmpp_send(conn, iq);
	xmpp_stanza_release(query);
	xmpp_stanza_release(iq);
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
		request_disco_info(conn, ctx, disco_host);
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
host_disco(const char *jid, const char *password, const char *host)
{
	disco_host = host;
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

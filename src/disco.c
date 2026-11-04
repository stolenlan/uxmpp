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

static char *
addr_label(const char *attribute) {
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

static void
display_disco_info_addrs(disco_ctx_t *dctx,
	xmpp_stanza_t *field,
	char buf[DISCO_MAX_RESULT],
	int colw) {

	static const char *address_types[] = {XEP_0157_ADDRESSES, NULL};
	int buflen, remsz, n_atypes = 0;

	for (n_atypes = 0; address_types[n_atypes] != NULL; n_atypes++);

	xmpp_ctx_t *ctx = xmpp_stanza_get_context(field);
	const char *fvar = xmpp_stanza_get_attribute(field, "var");

	for (int i = 0; i < n_atypes; i++) {
		const char *atype = address_types[i];
		const char *albl = addr_label(atype);
		if (!atype || !albl)
			break;
		if (strncmp(fvar, atype, strlen(fvar)) != 0)
			continue;

		xmpp_stanza_t *value = xmpp_stanza_get_children(field);
		for (; value; value = xmpp_stanza_get_next(value)) {
			char *c = xmpp_stanza_get_text(value);
			snprintf(buf, DISCO_MAX_RESULT, "%*s: %s\n",
				colw, albl, c);
			buflen = strlen(buf);
			remsz = DISCO_MAX_RESULT-(dctx->p-dctx->result);
			snprintf(dctx->p, remsz, "%s", buf);
			dctx->p += buflen;
			xmpp_free(ctx, c);
		}
	}
}

void
display_disco_info(disco_ctx_t *dctx, xmpp_stanza_t * const child) {

	static const int colw = 12;
	char buf[DISCO_MAX_RESULT];
	size_t buflen, remsz;

	if (child == NULL && strlen(dctx->result)) {
		snprintf(buf, DISCO_MAX_RESULT, "```\n");
		buflen = strlen(buf);
		remsz = DISCO_MAX_RESULT-(dctx->p-dctx->result);
		snprintf(dctx->p, remsz, "%s", buf);
		dctx->p += buflen;
		return;
	}

	const char *name = xmpp_stanza_get_name(child);
	// xmpp_ctx_t *ctx = xmpp_stanza_get_context(child);
	

	if (dctx->result && strlen(dctx->result) == 0) {
		char *htpl = "*disco result: %s*\n```\n";
		snprintf(buf, DISCO_MAX_RESULT, htpl, dctx->target);
		buflen = strlen(buf);
		remsz = DISCO_MAX_RESULT-(dctx->p-dctx->result);
		snprintf(dctx->p, remsz, "%s", buf);
		dctx->p += buflen;
	}

	if (strncmp(name, "feature", strlen(name)) == 0) {
		const char *var = xmpp_stanza_get_attribute(child, "var");
		snprintf(buf, DISCO_MAX_RESULT, "%*s: %s\n",
			colw, "FEATURE", var);
		buflen = strlen(buf);
		remsz = DISCO_MAX_RESULT-(dctx->p-dctx->result);
		snprintf(dctx->p, remsz, "%s", buf);
		dctx->p += buflen;
	}

	if (strncmp(name, "identity", strlen(name)) == 0) {
		const char *cat = xmpp_stanza_get_attribute(child, "category");
		if (cat && strncmp(cat, "server", strlen(cat)) == 0) {
			const char *server_name = xmpp_stanza_get_attribute(
				child, "name");
			snprintf(buf, DISCO_MAX_RESULT, "%*s: %s\n",
				colw, "IDENTITY", server_name);
			buflen = strlen(buf);
			remsz = DISCO_MAX_RESULT-(dctx->p-dctx->result);
			snprintf(dctx->p, remsz, "%s", buf);
			dctx->p += buflen;
		}
	}

	if (strncmp(name, "x", strlen(name)) == 0) {
		xmpp_stanza_t *x = child;
		for (xmpp_stanza_t *field = xmpp_stanza_get_children(x);
			field; field = xmpp_stanza_get_next(field)) {
			display_disco_info_addrs(dctx, field, buf, colw);
		}
	}
}

static int
on_disco_reply(xmpp_conn_t * const conn,
	xmpp_stanza_t * const stanza,
	void * const userdata) {
	disco_ctx_t *dctx = userdata;
	xmpp_ctx_t *ctx = xmpp_conn_get_context(conn);
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
		display_disco_info(dctx, child);
		child = xmpp_stanza_get_next(child);
	} while (child);

	display_disco_info(dctx, NULL); /* Append markdown formatting */

	printf("%s", dctx->result);
	xmpp_disconnect(conn);

	return 0;
}

void
request_disco_info(disco_ctx_t *dctx, xmpp_handler hdl) {
	xmpp_conn_t *conn = dctx->conn;
	xmpp_ctx_t *ctx = xmpp_conn_get_context(conn);
	xmpp_stanza_t *iq = xmpp_stanza_new(ctx);
	xmpp_stanza_t *query = xmpp_stanza_new(ctx);
	char *idstr = xmpp_uuid_gen(ctx);

	if (iq == NULL || query == NULL) {
		fprintf(stderr, "Could not allocate disco stanza\n");
		if (iq != NULL)
			xmpp_stanza_release(iq);
		if (query != NULL)
			xmpp_stanza_release(query);
		return;
	}

	xmpp_stanza_set_name(iq, "iq");
	xmpp_stanza_set_type(iq, "get");
	xmpp_stanza_set_id(iq, idstr);
	xmpp_stanza_set_to(iq, dctx->target);
	xmpp_stanza_set_name(query, "query");
	xmpp_stanza_set_ns(query, XMPP_NS_DISCO_INFO);
	xmpp_stanza_add_child(iq, query);
	xmpp_id_handler_add(conn, hdl, idstr, dctx);
	xmpp_send(conn, iq);
	xmpp_stanza_release(query);
	xmpp_stanza_release(iq);
	xmpp_free(ctx, idstr);
}

static void
on_connect(xmpp_conn_t * const conn,
	xmpp_conn_event_t status,
	int error, 
	xmpp_stream_error_t *stream_error, 
	void * const userdata) {
	static int connected = 0;
	disco_ctx_t *dctx = (disco_ctx_t *)userdata;
	xmpp_ctx_t *ctx = xmpp_conn_get_context(conn);

	if (stream_error) {
		fprintf(stderr, "Connection error: %s\n",
			stream_error->text);
		xmpp_stop(ctx);
	}

	if (status == XMPP_CONN_CONNECT) {
		connected = 1;
		request_disco_info(dctx, on_disco_reply);
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
disco_out(const char *jid, const char *password, const char *host) {
	disco_ctx_t dctx;
	xmpp_ctx_t *ctx = NULL;
	xmpp_conn_t *conn = NULL;

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


	dctx.conn = conn;
	snprintf(dctx.target, REGSTR_MAX, "%s", host);
	dctx.result = malloc(DISCO_MAX_RESULT);
	memset(dctx.result, '\0', DISCO_MAX_RESULT);
	if (!dctx.result) {
		fprintf(stderr, "Unable to allocate disco result memory.\n");
		perror(NULL);
		return 1;
	}

	dctx.p = dctx.result;

	xmpp_conn_set_jid(conn, jid);
	xmpp_conn_set_pass(conn, password);

	if (xmpp_connect_client(conn, NULL, 0, on_connect, &dctx) != 0) 
		fprintf(stderr, "Could not start the connection.\n");
	else
		xmpp_run(ctx);

	xmpp_conn_release(conn);
	xmpp_ctx_free(ctx);
	xmpp_shutdown();

	free(dctx.result);

	return 0;
}

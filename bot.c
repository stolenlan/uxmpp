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

#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <strophe.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "uxmpp.h"
#include "bot.h"

bot_ctx_t *running_bot = NULL;


static int
send_muc_message(xmpp_conn_t *const conn, const char *text,
	void *const userdata)
{
	bot_ctx_t *bctx = userdata;
	xmpp_ctx_t *ctx = xmpp_conn_get_context(conn);
	xmpp_stanza_t *msg = xmpp_message_new(ctx,
		"groupchat", bctx->muc, NULL);
	xmpp_message_set_body(msg, text);
	xmpp_send(conn, msg);
	xmpp_stanza_release(msg);
	return 0;
}


static void *
th_watch_fifo(void *userdata) {


	bot_ctx_t *bctx = userdata;
	int fd = open(bctx->in_fifo, O_RDONLY | O_NONBLOCK, 0);
	if (fd == -1) {
		fprintf(stderr, "Unable to open: %s\n", bctx->in_fifo);
		return 0;
	}

	while (!bctx->part) {
		char buf[4096];
		memset(buf, '\0', 4096);
		if(read(fd, buf, 4095) > 0) {
			if (f_debug)
				fprintf(stderr,
				"Sending data received from %s to MUC %s\n",
				bctx->in_fifo, bctx->muc);
			send_muc_message(bctx->conn, buf, userdata);
		}
	}

	return NULL;
}

static int
tm_presence_heartbeat(xmpp_conn_t *const conn, void *const userdata)
{
	bot_ctx_t *bctx = userdata;
	if (bctx->part)
		return 1;

	char muc_jid_buf[REGSTR_MAX];
	xmpp_stanza_t *presence = xmpp_presence_new(bctx->ctx);
	xmpp_stanza_set_id(presence, bctx->jid);
	xmpp_stanza_set_type(presence, "available");
	memset(muc_jid_buf, '\0', REGSTR_MAX);
	snprintf(muc_jid_buf, REGSTR_MAX, "%s/%s",
		bctx->muc, bctx->nick);
	xmpp_stanza_set_to(presence, muc_jid_buf);
	xmpp_send(conn, presence);
	xmpp_stanza_release(presence);

	return 1;
}

static int
cb_handler(xmpp_conn_t *const conn, xmpp_stanza_t *const stanza,
	void *const userdata) {

	const char *type, *id, *p,
		*name = NULL, 
		*from = "";
	bot_ctx_t *bctx = userdata;
	xmpp_ctx_t *ctx = bctx->ctx;
	
	name = xmpp_stanza_get_name(stanza);
	type = xmpp_stanza_get_type(stanza);
	from = xmpp_stanza_get_from(stanza);
	id = xmpp_stanza_get_id(stanza);

	(void)conn;
	(void)name;

	if (bctx->part) {
		xmpp_stop(ctx);
		return 0;
	}

	if (name && strncmp(name, "presence", strlen(name)) == 0) {
		xmpp_stanza_t *x = xmpp_stanza_get_child_by_name_and_ns(
			stanza, "x", "http://jabber.org/protocol/muc#user");
		if (!x)
			return 1;
		xmpp_stanza_t *status = xmpp_stanza_get_child_by_name(
			x, "status");

		if (status) {
			const char *code = xmpp_stanza_get_attribute(
				status, "code");
			if (strncmp(code, "110", strlen(code)) == 0) {
				
			}
		}

	}

	if (!type || !id)
		return 1;

	if (strncmp(type, "groupchat", strlen(type)) == 0 && id) {
		char *body = xmpp_message_get_body(stanza);
		if (!body) return 1;


		printf("%s: %s\n", xmpp_stanza_get_from(stanza), body);
		int fd = open(bctx->out_fifo, O_WRONLY);
		if (fd == -1) {
			fprintf(stderr, "Error opening %s\n", bctx->out_fifo);
			return 1;
		}
		p = from; /* Seek the pointer up through the slash */
		for (char c = *p; p && c != '/'; c = *p++);
		const char *sep = ": ";
		// if (*p == '/' && strlen(p+1))
			write(fd, from, strlen(from));
		// else
			// write(fd, bctx->muc, strlen(bctx->muc));
		write(fd, sep, strlen(sep));
		write(fd, body, strlen(body));
		close(fd);

		// Open the read fifo, and see if there are any messages waiting
		xmpp_free(ctx, body);

	}

	return 1;
}

static int
mk_fifos(bot_ctx_t *bctx)
{

	// TOODO, CHECK IF THEY're VALID AND SHIT
	mkfifo(bctx->in_fifo, 0664);
	mkfifo(bctx->out_fifo, 0664);

	return 1;
}

static void
on_connect(xmpp_conn_t * const conn, xmpp_conn_event_t status, int error, 
	xmpp_stream_error_t *stream_error, void * const userdata)
{
	pthread_t thread;
	char muc_jid_buf[REGSTR_MAX];
	bot_ctx_t *bctx = userdata;
	xmpp_ctx_t *ctx = bctx->ctx;

	if (status == XMPP_CONN_CONNECT) {
		xmpp_stanza_t *presence = xmpp_presence_new(ctx);
		xmpp_stanza_set_id(presence, bctx->jid);
		memset(muc_jid_buf, '\0', REGSTR_MAX);
		snprintf(muc_jid_buf, REGSTR_MAX, "%s/%s",
			bctx->muc, bctx->nick);
		xmpp_stanza_set_to(presence, muc_jid_buf);
		
		xmpp_handler_add(conn, cb_handler, NULL, NULL, NULL, bctx);
		xmpp_timed_handler_add(conn,
			tm_presence_heartbeat,
			60000, userdata);

		mk_fifos(bctx);
		
		bctx->connected = 1;
		running_bot = userdata;

		xmpp_send(conn, presence);
		xmpp_stanza_release(presence);

		pthread_create(&thread, NULL, th_watch_fifo, userdata);
		pthread_detach(thread);

	} else if (status == XMPP_CONN_FAIL) {
		fprintf(stderr, "Connection or authentication "
			"failed (error %d).\n", error);
		xmpp_stop(ctx);
	} else if (status == XMPP_CONN_DISCONNECT) {
		if (f_debug)
			fprintf(stderr, "DISCONNECT\n");
		if (!bctx->connected)
			fprintf(stderr, "unable to authenticate\n");
		xmpp_stop(ctx);
	}

	(void)stream_error;
}


int
start_bot(bot_ctx_t *usr)
{
	running_bot = NULL;
	bot_ctx_t *bctx = usr;
	bctx->connected = 0;
	bctx->part = 0;

	if (bctx->jid == NULL || bctx->pass == NULL) {
		fprintf(stderr, "Invalid or missing credentials.\n");
		return 1;
	}

	if (!strlen(bctx->jid)) {
		fprintf(stderr, "Invalid or missing credentials.\n");
		return 1;
	}

	xmpp_initialize();

	printf("Starting bot...\n");

	xmpp_log_t *logger = NULL;
	if (f_debug)
		logger = xmpp_get_default_logger(XMPP_LEVEL_DEBUG);

	bctx->ctx = xmpp_ctx_new(NULL, logger);
	if (bctx->ctx == NULL) {
		xmpp_shutdown();
		return 1;
	}

	bctx->conn = xmpp_conn_new(bctx->ctx);
	if (bctx->conn == NULL) {
		fprintf(stderr, "Could not start connection.\n");
		xmpp_ctx_free(bctx->ctx);
		xmpp_shutdown();
		return 1;
	}

	xmpp_conn_set_jid(bctx->conn, bctx->jid);
	xmpp_conn_set_pass(bctx->conn, bctx->pass);

	if (xmpp_connect_client(bctx->conn, NULL, 0, on_connect, bctx) != 0) 
		fprintf(stderr, "Could not start the connection.\n");
	else
		xmpp_run(bctx->ctx);


	xmpp_conn_release(bctx->conn);
	xmpp_ctx_free(bctx->ctx);
	xmpp_shutdown();


	unlink(bctx->in_fifo);
	/* unlink(bctx->out_fifo); */

	return 0;
}

void
stop_bot(int signal)
{
	static int shutdown = 0;
	if (!running_bot)
		exit(0);

	if (shutdown) {
		const char *sigstr = "SIGNAL";
		if (signal == SIGTERM)
			sigstr = "SIGTERM";
		else if (signal == SIGQUIT)
			sigstr = "SIGQUIT";

		fprintf(stderr,
			"\nRECEIVED %s (%d), for the 2nd time: killing...",
			sigstr, signal);
		exit(1);
	}

	fprintf(stderr,
		"\nRECEIVED SIGINT (%d), shutting down...\n",
		signal);

	bot_ctx_t *bctx = running_bot;
	xmpp_ctx_t *ctx = bctx->ctx;
	xmpp_conn_t *conn = bctx->conn;

	char muc_jid_buf[REGSTR_MAX];
	// TODO: send presence to server
	xmpp_stanza_t *presence = xmpp_presence_new(ctx);
	xmpp_stanza_set_id(presence, bctx->jid);
	xmpp_stanza_set_type(presence, "unavailable");
	memset(muc_jid_buf, '\0', REGSTR_MAX);
	snprintf(muc_jid_buf, REGSTR_MAX, "%s/%s",
		bctx->muc, bctx->nick);
	xmpp_stanza_set_to(presence, muc_jid_buf);
	xmpp_send(conn, presence);
	xmpp_stanza_release(presence);

	bctx->part = 1;
	shutdown = 1;
}

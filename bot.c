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
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <strophe.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

#include "uxmpp.h"
#include "bot.h"


#define DEFAULT_REPLY \
"Sorry?\n"\
"Just a dumb bot here, no LLMs, telemetry, or funny-business.\n"\
"I do a few things, and do them well, but general chat is not one of them.\n"\

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

static char *
iq_time(void)
{
	static char xmpp_time[2048] = {0};

	time_t ct = time(NULL);
	struct tm *lt_tm = localtime(&ct);
	struct tm *gt_tm = gmtime(&ct);

	char utcbuf[64], offbuf[64], sign = ' ';
	memset(utcbuf, '\0', 64);
	memset(offbuf, '\0', 64);
	strftime(utcbuf, 63, "%Y-%m-%dT%H:%M:%SZ", gt_tm);

	long int gmtoff = lt_tm->tm_gmtoff;

	if (gmtoff < 0) {
		sign = '-';
		gmtoff = gmtoff * -1;
	} else {
		sign = '+';
	}

	snprintf(offbuf, 64, "%c%02ld:%02ld",
		sign, gmtoff/60/60, (gmtoff/60)%60);

	snprintf(xmpp_time, REGSTR_MAX,
		"<time xmlns='urn:xmpp:time'>"
			"<tzo>%s</tzo>"
			"<utc>%s</utc>"
		"</time>\n",
		offbuf, utcbuf);

	return xmpp_time;
}

static void *
th_watch_fifo(void *userdata)
{
	bot_ctx_t *bctx = userdata;

	if (bctx->quit || !strlen(bctx->in_fifo))
		return NULL;

	int fd = open(bctx->in_fifo, O_RDONLY, 0);
	if (fd == -1) {
		if (f_debug)
			fprintf(stderr, "Unable to open: %s\n", bctx->in_fifo);
		return NULL;
	}

	char buf[4096];
	memset(buf, '\0', 4096);
	ssize_t buf_br;
	if( (buf_br = read(fd, buf, 4095)) > 0) {
		if (f_debug)
			fprintf(stderr,
			"Sending data received from %s to MUC %s\n",
			bctx->in_fifo, bctx->muc);
		send_muc_message(bctx->conn, buf, userdata);
	} else {
		fprintf(stderr, "Error reading %s\n", bctx->in_fifo);
	}

	close(fd);
	bctx->fifo_thread = 0;
	if (pthread_create(&bctx->fifo_thread,
		NULL, th_watch_fifo, userdata) == 0)
		pthread_detach(bctx->fifo_thread);

	return NULL;
}

static int
cb_iq(xmpp_conn_t *const conn, xmpp_stanza_t *const stanza, 
	void *const userdata) {
	char buf[2048];
	memset(buf, '\0', 2048);

	(void)conn;
	(void)stanza;
	bot_ctx_t *bctx = (bot_ctx_t *)userdata;
	xmpp_ctx_t *ctx = bctx->ctx;

	const char *id = xmpp_stanza_get_id(stanza);
	const char *from = xmpp_stanza_get_from(stanza);
	const char *to = xmpp_stanza_get_to(stanza);
	xmpp_stanza_t *query = NULL;

	if (!id || !from)
		return 1;

	// I need to extract the node info
	char *from_node = xmpp_jid_node(ctx, from);
	char *from_domain = xmpp_jid_domain(ctx, from);
	char *from_resource = xmpp_jid_resource(ctx, from);

	char *to_node = xmpp_jid_node(ctx, to);
	char *to_domain = xmpp_jid_domain(ctx, to);
	char *to_resource = xmpp_jid_resource(ctx, to);

	if ((query = xmpp_stanza_get_child_by_ns(stanza, "urn:xmpp:ping")) 
		!= NULL)
		/* XEP-0092 Software Version */
		snprintf(buf, 2048,
		"<iq type='result' to='%s' from='%s' id='%s'/>",
		xmpp_jid_new(ctx, from_node, from_domain, from_resource),
		xmpp_jid_new(ctx, to_node, to_domain, to_resource),
		id);

	if ((query = xmpp_stanza_get_child_by_ns(stanza, "urn:xmpp:time")) 
		!= NULL) {

		/* XEP-0092 Software Version */
		snprintf(buf, 2048,
		"<iq type='result' to='%s' from='%s' id='%s'>"
		"%s"
		"</iq>",
		xmpp_jid_new(ctx, from_node, from_domain, from_resource),
		xmpp_jid_new(ctx, to_node, to_domain, to_resource),
		id, iq_time());
	}

	if ((query = xmpp_stanza_get_child_by_ns(stanza, "jabber:iq:version")) 
		!= NULL)
		/* XEP-0092 Software Version */
		snprintf(buf, 2048,
		"<iq type='result' to='%s' from='%s' id='%s'>"
		"<query xmlns='jabber:iq:version'>"
		"<name>uxmpp</name>"
		"<version>%d.%d.%d</version>"
		"<os>Win98SE</os>"
		"</query>"
		"</iq>",
		xmpp_jid_new(ctx, from_node, from_domain, from_resource),
		xmpp_jid_new(ctx, to_node, to_domain, to_resource),
		id, VER_MAJ, VER_MIN, VER_PNT);
		

	if ((query = xmpp_stanza_get_child_by_ns(stanza,
			"http://jabber.org/protocol/disco#info")) != NULL)
		snprintf(buf, 2048,
		"<iq from=\"%s\" id=\"%s\" to=\"%s\" type='result'>"
		"<query xmlns='http://jabber.org/protocol/disco#info'"
		" node='https://sinelabs.ca/uxmpp/mucbot#" XEP_0115_VER "'>"
		"<identity category='client' name='uxmpp %d.%d.%d' type='pc'/>"
		"<feature var='http://jabber.org/protocol/caps'/>"
		"<feature var='http://jabber.org/protocol/disco#info'/>"
		"<feature var='http://jabber.org/protocol/disco#items'/>"
		"<feature var='http://jabber.org/protocol/muc'/>"
		"<feature var='urn:xmpp:ping'/>"
		"<feature var='urn:xmpp:time'/>"
		"</query>"
		"</iq>",
		xmpp_jid_new(ctx, to_node, to_domain, to_resource),
		id,
		xmpp_jid_new(ctx, from_node, from_domain, from_resource),
		VER_MAJ, VER_MIN, VER_PNT);

	if ((query = xmpp_stanza_get_child_by_ns(stanza,
		"http://jabber.org/protocol/disco#items")) != NULL)
		snprintf(buf, 2048,
		"<iq from=\"%s\" id=\"%s\" to=\"%s\" type='result'>"
		"<query xmlns='http://jabber.org/protocol/disco#items'/>"
		"</iq>",
		xmpp_jid_new(ctx, to_node, to_domain, to_resource),
		id,
		xmpp_jid_new(ctx, from_node, from_domain, from_resource));

	if (strlen(buf)) {
		xmpp_stanza_t *iq = xmpp_stanza_new_from_string(ctx, buf);
		xmpp_send(conn, iq);
		xmpp_stanza_release(iq);
	}
	
	return 1;
}

static int
cb_presence(xmpp_conn_t *const conn, xmpp_stanza_t *const stanza,
	void *const userdata)
{
	static int joined = 0;
	(void)conn;
	bot_ctx_t *bctx = (bot_ctx_t *)userdata;
	xmpp_stanza_t *x, *status;
	const char *type;

	x = xmpp_stanza_get_child_by_name_and_ns(stanza, "x",
		"http://jabber.org/protocol/muc#user");
	type = xmpp_stanza_get_type(stanza);
	
	if (!x)
		return 1;

	status = xmpp_stanza_get_child_by_name(x, "status");

	if (status) {
		const char *code = xmpp_stanza_get_attribute(
			status, "code");
		if (code && strncmp(code, "110", strlen(code)) == 0) {
			if (type && strlen(type) && strncmp(type, 
				"unavailable", strlen(type)) == 0)
				fprintf(stderr, "LEFT: %s\n", bctx->muc);
			else if (joined == 0) {
				fprintf(stderr, "JOINED: %s\n", bctx->muc);
				joined = 1;
			}
		}
	}

	return 1;
}

static int
cb_message(xmpp_conn_t *const conn, xmpp_stanza_t *const stanza,
	void *const userdata) {
	const char *type, *id, *p,
		*name = NULL, 
		*from = "",
		*soh  = "\x01",
		*nl   = "\n";

	char c, buf[4096];

	bot_ctx_t *bctx = userdata;
	xmpp_ctx_t *ctx = bctx->ctx;
	
	
	name = xmpp_stanza_get_name(stanza);
	type = xmpp_stanza_get_type(stanza);
	from = xmpp_stanza_get_from(stanza);
	id = xmpp_stanza_get_id(stanza);

	(void)conn;
	(void)name;

	if (!type || !id)
		return 1;

	if (strncmp(type, "groupchat", strlen(type)) == 0 && id) {
		char *body = xmpp_message_get_body(stanza);
		if (!body) return 1;

		printf("%s: %s\n", xmpp_stanza_get_from(stanza), body);

		p = from; /* Seek the pointer up through the slash */
		for (c = *p; p && c != '/'; c = *p++);

		if (strncmp(body, "test", strlen(body)) == 0) {
			memset(buf, '\0', sizeof(buf));
			snprintf(buf, sizeof(buf), "%s: %s", p,
				"ACK! I trust your test was successful.");
			send_muc_message(conn, buf, userdata);
		}

		c = body[strlen(bctx->nick)];
		if (strncmp(body, bctx->nick, strlen(bctx->nick)) == 0
			&& (c == ':' || c == ',')) {
			memset(buf, '\0', sizeof(buf));
			snprintf(buf, sizeof(buf), "%s: %s", p, DEFAULT_REPLY);
			snprintf(buf, sizeof(buf), "%s\nsee: !%s help",
				buf, bctx->nick);
			send_muc_message(conn, buf, userdata);
		}

		if (strlen(bctx->out_fifo)) {
			int fd = open(bctx->out_fifo, O_WRONLY);
			if (fd == -1) {
				fprintf(stderr, "Error opening %s\n", bctx->out_fifo);
				return 1;
			}

			write(fd, soh, 1);
			write(fd, from, strlen(from));
			write(fd, nl, 1);
			write(fd, body, strlen(body));
			close(fd);
		}
		
		xmpp_free(ctx, body);

	} else if (strncmp(type, "chat", strlen(type)) == 0 && id) {
		char *body = xmpp_message_get_body(stanza);
		if (!body)
			return 1;
		fprintf(stderr, "%s",   soh);
		fprintf(stderr, "%s: ", from);
		fprintf(stderr, "%s\n", body);

		// memset(buf, '\0', sizeof(buf));
		// snprintf(buf, sizeof(buf), "%s: %s", p, DEFAULT_REPLY);
		// snprintf(buf, sizeof(buf), "%s\nsee: !%s help", 
		// 	buf, bctx->nick);
		
		// TODO create send_direct_message()
		// send_muc_message(conn, buf, userdata);

		xmpp_free(ctx, body);
	} else {
		fprintf(stderr, "received unhandled message: %s:%s:%s:%s\n",
			name, type, id, from);
	}

	return 1;
}

static int
cb_handler(xmpp_conn_t *const conn, xmpp_stanza_t *const stanza,
	void *const userdata) {
	(void)stanza;
	bot_ctx_t *bctx = (bot_ctx_t *)userdata;
	if (bctx->quit) {
		if (f_debug)
			fprintf(stderr, "cb_handler: xmpp_disconnect()");
		xmpp_disconnect(conn);
		return 0;
	}

	return 1;
}

static int
mk_fifos(bot_ctx_t *bctx)
{
	// TODO: exit on failure
	if (strlen(bctx->in_fifo))
		mkfifo(bctx->in_fifo, 0664);

	if (strlen(bctx->out_fifo))
		mkfifo(bctx->out_fifo, 0664);

	return 1;
}

static int
del_fifos(bot_ctx_t *bctx)
{
	if (strlen(bctx->in_fifo))
		unlink(bctx->in_fifo);
	if (strlen(bctx->out_fifo))
		unlink(bctx->out_fifo);

	return 1;
}

static void
on_connect(xmpp_conn_t * const conn, xmpp_conn_event_t status, int error, 
	xmpp_stream_error_t *stream_error, void * const userdata)
{
	char muc_jid_buf[REGSTR_MAX];
	bot_ctx_t *bctx = userdata;
	xmpp_ctx_t *ctx = bctx->ctx;

	if (status == XMPP_CONN_CONNECT) {
		memset(muc_jid_buf, '\0', REGSTR_MAX);
		snprintf(muc_jid_buf, REGSTR_MAX, "%s/%s",
			bctx->muc, bctx->nick);

		bctx->connected = 1;
		running_bot = userdata;

		mk_fifos(bctx);
		
		/* Channel presence ID */
		char *cpid = xmpp_uuid_gen(ctx);

		xmpp_handler_add(conn, cb_message, NULL,
			"message", NULL, bctx);
		// xmpp_handler_add(conn, cb_presence, NULL,
		// 	"presence", NULL, bctx);
		xmpp_id_handler_add(conn, cb_presence, cpid, bctx);
		xmpp_handler_add(conn, cb_iq, NULL,
			"iq", "get", bctx);
		xmpp_handler_add(conn, cb_handler, NULL,
			NULL, NULL, bctx);

		xmpp_stanza_t *pres = xmpp_presence_new(ctx);
		xmpp_send(conn, pres);
		xmpp_stanza_release(pres);

		xmpp_stanza_t *presence = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(presence, "presence");
		
		xmpp_stanza_set_id(presence, cpid);
		xmpp_stanza_set_to(presence, muc_jid_buf);

		xmpp_stanza_t *c = xmpp_stanza_new_from_string(ctx,
			"<c xmlns='http://jabber.org/protocol/caps' "
     				"hash='sha-1' "
     				"node='https://sinelabs.ca/uxmpp/mucbot' "
     				"ver='" XEP_0115_VER "'/>");
		xmpp_stanza_add_child(presence, c);
		xmpp_stanza_release(c);

		xmpp_stanza_t *x = xmpp_stanza_new_from_string(ctx,
			"<x xmlns='http://jabber.org/protocol/muc'/>");
		xmpp_stanza_add_child(presence, x);
		xmpp_stanza_release(x);

		xmpp_send(conn, presence);
		xmpp_stanza_release(presence);
		xmpp_free(ctx, cpid);

		if (strlen(bctx->in_fifo)) {
			if (pthread_create(&bctx->fifo_thread, NULL, 
				th_watch_fifo, userdata) == 0)
				pthread_detach(bctx->fifo_thread);
			else
				fprintf(stderr,
					"ERROR: Unable to spawn thread\n");
		}

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
	bctx->quit = 0;

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

	del_fifos(bctx);

	return 0;
}

void
stop_bot(int signal)
{
	if (!running_bot)
		exit(0);

	if (running_bot->quit) {
		const char *sigstr = "SIGNAL";
		if (signal == 2)
			sigstr = "SIGINT";
		if (signal == 15)
			sigstr = "SIGTERM";
		else if (signal == 3)
			sigstr = "SIGQUIT";

		fprintf(stderr,
			"\nRECEIVED %s (%d), for the 2nd time: killing...",
			sigstr, signal);
		exit(1);
	}

	running_bot->quit = 1;

	fprintf(stderr,
		"\nRECEIVED SIGINT (%d), shutting down...\n",
		signal);

	bot_ctx_t *bctx = running_bot;
	xmpp_ctx_t *ctx = bctx->ctx;
	xmpp_conn_t *conn = bctx->conn;

	char muc_jid_buf[REGSTR_MAX];

	xmpp_stanza_t *presence = xmpp_presence_new(ctx);
	xmpp_stanza_set_id(presence, bctx->jid);
	xmpp_stanza_set_type(presence, "unavailable");
	memset(muc_jid_buf, '\0', REGSTR_MAX);
	snprintf(muc_jid_buf, REGSTR_MAX, "%s/%s",
		bctx->muc, bctx->nick);
	xmpp_stanza_set_to(presence, muc_jid_buf);
	xmpp_send(conn, presence);
	xmpp_stanza_release(presence);
}

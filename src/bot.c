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
#include <ctype.h>
#include <pthread.h>

#include "util.h"
#include "uxmpp.h"
#include "xep-0115-ver.h"
#include "bot.h"
#include "disco.h"

#define MAX_N_BOTS 256

#define DEFAULT_REPLY \
"Sorry?\n"\
"Just a dumb bot here, no LLMs, telemetry, or funny-business.\n"

#define BOT_HELP_TMPL \
"```\n"\
"uxmpp %s: bot\n"\
"XMPP Utilities for UNIX: in-band bot help\n\n"\
"Commands (prefixed with `!%s'):\n"\
"help          Show this help\n"\
"disco  <jid>  Perform disco#info target\n"\
"discor <jid>  Perform disco and return raw IQ\n"\
"fortune       Display a classic fortune\n"\
"ping   <jid>  Ping (urn:xmpp:ping) target jid\n"\
"```"

static bot_ctx_t *running_bot[MAX_N_BOTS];
static int running_bots = 0;


static int
send_muc_message(xmpp_conn_t *const conn,
	const char *text,
	void *const userdata) {
	bot_ctx_t *bctx = userdata;
	xmpp_ctx_t *ctx = xmpp_conn_get_context(conn);
	xmpp_stanza_t *msg = xmpp_message_new(ctx,
		"groupchat", bctx->muc, NULL);
	xmpp_message_set_body(msg, text);
	xmpp_send(conn, msg);
	xmpp_stanza_release(msg);
	return 0;
}

static disco_ctx_t *
disco_init(bot_ctx_t *bctx, const char *host) {
	static disco_ctx_t dctx = {0};

	memset(dctx.target, '\0', sizeof(dctx.target));
	snprintf(dctx.target, sizeof(dctx.target), "%s", host);

	if (dctx.init) {
		memset(dctx.result, '\0', DISCO_MAX_RESULT);
		return &dctx;
	}

	dctx.init = 1;
	dctx.active = 0;
	dctx.conn = bctx->conn;
	dctx.ref = (void *)bctx;
	dctx.result = malloc(DISCO_MAX_RESULT);
	memset(dctx.result, '\0', DISCO_MAX_RESULT);
	dctx.p = dctx.result;

	return &dctx;
}

static int
bot_disco_reply(xmpp_conn_t * const conn,
	xmpp_stanza_t *const stanza, void *const userdata) {

	disco_ctx_t *dctx = userdata;
	xmpp_ctx_t *ctx = xmpp_conn_get_context(conn);
	const char *type = xmpp_stanza_get_type(stanza);

	if (type != NULL && strcmp(type, "error") == 0) {

		xmpp_stanza_t *stext = xmpp_stanza_get_child_by_name(stanza,
			"error");
		
		xmpp_stanza_t *text = xmpp_stanza_get_child_by_name(stext,
			"text");
		const char *err_txt = xmpp_stanza_get_text(text);

		fprintf(stderr, "Server returned an IQ error.\n");
		char buf[1024];
		snprintf(buf, 1024, "disco (%s) ERROR: %s\n",
			dctx->target, err_txt);
		send_muc_message(conn, buf, dctx->ref);

		xmpp_free(ctx, (void *)err_txt);
		free(dctx->result);
		dctx->init = 0;
		return 0;
	}

	xmpp_stanza_t *query = xmpp_stanza_get_child_by_name_and_ns(stanza,
		"query", XMPP_NS_DISCO_INFO);

	xmpp_stanza_t *child = xmpp_stanza_get_children(query);
	do {
		display_disco_info(dctx, child);
		child = xmpp_stanza_get_next(child);
	} while (child);

	display_disco_info(dctx, NULL); /* Add any trailing formatting */
	send_muc_message(conn, dctx->result, dctx->ref);
	free(dctx->result);
	dctx->init = 0;

	return 0;
}

static int
send_direct_message(const char *to_jid,
	const char *text,
	void *const userdata) {

	bot_ctx_t *bctx = userdata;
	xmpp_ctx_t *ctx = bctx->ctx;
	xmpp_conn_t *conn = bctx->conn;

	xmpp_stanza_t *msg = xmpp_message_new(ctx,
		"chat", to_jid, NULL);
	xmpp_message_set_body(msg, text);
	xmpp_send(conn, msg);
	xmpp_stanza_release(msg);
	return 0;
}

static char *
iq_time(void) {
	static char xmpp_time[1024] = {0};

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
th_watch_fifo(void *userdata) {
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
			fprintf(stderr,"RX: %s\n",bctx->in_fifo);
		send_muc_message(bctx->conn, buf, userdata);
	} else {
		fprintf(stderr, "Error reading %s\n", bctx->in_fifo);
	}

	close(fd);
	bctx->in_thread = 0;
	if (pthread_create(&bctx->in_thread,
		NULL, th_watch_fifo, userdata) == 0)
		pthread_detach(bctx->in_thread);

	return NULL;
}

static void *
th_watch_cmd_fifo(void *userdata) {
	bot_ctx_t *bctx = userdata;

	if (bctx->quit || !strlen(bctx->cmd_fifo))
		return NULL;

	int fd = open(bctx->cmd_fifo, O_RDONLY, 0);
	if (fd == -1) {
		if (f_debug)
			fprintf(stderr, "Unable to open: %s\n",
				bctx->cmd_fifo);
		return NULL;
	}

	char buf[4096];
	memset(buf, '\0', 4096);
	ssize_t buf_br;
	if( (buf_br = read(fd, buf, 4095)) > 0) {
		if (f_debug)
			fprintf(stderr, "Received: %s from %s\n",
				buf, bctx->cmd_fifo);
		if (strncmp("quit\n", buf, strlen(buf)) == 0) {
			close(fd);
			xmpp_disconnect(bctx->conn);
			return NULL;
		}
	} else {
		fprintf(stderr, "Error reading %s\n", bctx->in_fifo);
	}

	close(fd);
	bctx->cmd_thread = 0;
	if (pthread_create(&bctx->cmd_thread,
		NULL, th_watch_cmd_fifo, userdata) == 0)
		pthread_detach(bctx->cmd_thread);

	return NULL;
}

static int
cb_heartbeat_rx_ping(xmpp_conn_t *const conn,
                     xmpp_stanza_t *const stanza,
                     void *const userdata) {
	(void)conn;
	bot_ctx_t *bctx = (bot_ctx_t *)userdata;

	const char *type = xmpp_stanza_get_type(stanza);
	const char *domain = xmpp_jid_domain(bctx->ctx, bctx->jid);
	
	if (type != NULL && strcmp(type, "error") == 0) {
		fprintf(stderr, "Ping returned an error!\n");
	}
	if (f_debug)
		fprintf(stderr, "RX: PONG (%s)!\n", domain);

	xmpp_free(bctx->ctx, (void *)domain);
	return 0;
}

static int
cb_heartbeat(xmpp_conn_t *const conn, void *userdata) {
	(void)conn;
	bot_ctx_t *bctx = (bot_ctx_t *)userdata;
	xmpp_ctx_t *ctx = bctx->ctx;

	char *cpid = xmpp_uuid_gen(ctx);
	char *domain = xmpp_jid_domain(ctx, bctx->jid);
	char buf[1024];
	memset(buf, '\0', sizeof(buf));

	snprintf(buf, sizeof(buf),
		"<iq id='%s' from='%s' to='%s' type='get'>"
  		"<ping xmlns='urn:xmpp:ping'/>"
  		"</iq>", cpid, bctx->jid, domain);

	// Create a ping, and send it
	xmpp_stanza_t *ping = xmpp_stanza_new_from_string(ctx, buf);
	xmpp_id_handler_add(conn, cb_heartbeat_rx_ping, cpid, userdata);

	if (f_debug)
		fprintf(stderr, "TX: PING --> %s\n", domain);
	xmpp_send(conn, ping);

	xmpp_stanza_release(ping);
	xmpp_free(ctx, cpid);
	xmpp_free(ctx, domain);

	return 1;
}


static int
cb_iq(xmpp_conn_t *const conn,
      xmpp_stanza_t *const stanza, 
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

	char *resp_to_jid = xmpp_jid_new(ctx,
		from_node, from_domain, from_resource);
	char *resp_from_jid = xmpp_jid_new(ctx,
		to_node, to_domain, to_resource);

	if ((query = xmpp_stanza_get_child_by_ns(stanza, "urn:xmpp:ping"))
		!= NULL) {
		/* XEP-0092 Software Version */
		snprintf(buf, 2048,
		"<iq type='result' to='%s' from='%s' id='%s'/>",
		resp_to_jid,
		resp_from_jid,
		id);
	}

	if ((query = xmpp_stanza_get_child_by_ns(stanza, "urn:xmpp:time")) 
		!= NULL) {
		/* XEP-0092 Software Version */
		snprintf(buf, 2048,
		"<iq type='result' to='%s' from='%s' id='%s'>"
		"%s"
		"</iq>",
		resp_to_jid,
		resp_from_jid,
		id, iq_time());
	}

	if ((query = xmpp_stanza_get_child_by_ns(stanza, "jabber:iq:version")) 
		!= NULL)
		/* XEP-0092 Software Version */
		snprintf(buf, 2048,
		"<iq type='result' id='%s' to='%s' from='%s'>"
		"<query xmlns='jabber:iq:version'>"
		"<name>uxmpp</name>"
		"<version>%s.%s.%s</version>"
		"<os>Win98SE</os>"
		"</query>"
		"</iq>",
		id,
		resp_to_jid,
		resp_from_jid,
		VER_MAJ, VER_MIN, VER_PNT);
		

	if ((query = xmpp_stanza_get_child_by_ns(stanza,
			"http://jabber.org/protocol/disco#info")) != NULL)
		snprintf(buf, 2048,
		"<iq id=\"%s\" from=\"%s\" to=\"%s\" type='result'>"
		"<query xmlns='http://jabber.org/protocol/disco#info'"
		" node='https://sinelabs.ca/uxmpp/mucbot#" XEP_0115_VER "'>"
		"<identity xml:lang='en' category='client'"
		" name='uxmpp %s.%s.%s' type='pc'/>"
		"<feature var='http://jabber.org/protocol/caps'/>"
		"<feature var='http://jabber.org/protocol/disco#info'/>"
		"<feature var='http://jabber.org/protocol/disco#items'/>"
		"<feature var='http://jabber.org/protocol/muc'/>"
		"<feature var='urn:xmpp:ping'/>"
		"<feature var='urn:xmpp:time'/>"
		"</query>"
		"</iq>",
		id,
		resp_from_jid,
		resp_to_jid,
		VER_MAJ, VER_MIN, VER_PNT);

	if ((query = xmpp_stanza_get_child_by_ns(stanza,
		"http://jabber.org/protocol/disco#items")) != NULL)
		snprintf(buf, 2048,
		"<iq id=\"%s\" from=\"%s\" to=\"%s\" type='result'>"
		"<query xmlns='http://jabber.org/protocol/disco#items'/>"
		"</iq>",
		id,
		resp_from_jid,
		resp_to_jid);

	if (strlen(buf)) {
		xmpp_stanza_t *iq = xmpp_stanza_new_from_string(ctx, buf);
		xmpp_send(conn, iq);
		xmpp_stanza_release(iq);
	}
	
	xmpp_free(ctx, from_node);
	xmpp_free(ctx, from_domain);
	xmpp_free(ctx, from_resource);

	xmpp_free(ctx, to_node);
	xmpp_free(ctx, to_domain);
	xmpp_free(ctx, to_resource);

	xmpp_free(ctx, resp_to_jid);
	xmpp_free(ctx, resp_from_jid);
	return 1;
}

static int
cb_presence(xmpp_conn_t *const conn,
	xmpp_stanza_t *const stanza,
	void *const userdata) {
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
			else if (bctx->joined == 0) {
				fprintf(stderr, "JOINED: %s\n", bctx->muc);
				bctx->joined = 1;
			}
		}
	}

	return 1;
}

static void
handle_bot_cmd(bot_ctx_t *bctx, const char *cmd) {
	xmpp_conn_t *conn = bctx->conn;

	const char *p;

	/* Trim leading whitespace */
	for (p = cmd; *p && isspace(*p); p++);

	if (strncmp(p, "disco", strlen("disco")) == 0) {
		/* p Will become the pointer to start of JID */
		for (p = p+strlen("disco");*p && isspace(*p); p++);
		if (!strlen(p)) {
			send_muc_message(conn, "Usage: disco <jid>",
				bctx);
		} else {
			if (strncmp(p, bctx->jid, strlen(bctx->jid)) == 0) {
				send_muc_message(conn,
					"No ;)", bctx);
				return;
			}

			disco_ctx_t *dctx = disco_init(bctx, p);

			char buf[1024];
			snprintf(buf, 1024,
				"performing disco for %s...", p);
			if (!dctx->active) {
				dctx->active = 1;
				send_muc_message(conn, buf, bctx);
				request_disco_info(dctx, bot_disco_reply);
			} else {
				send_muc_message(conn,
					"Service busy.", bctx);
			}
			
		}
	} else if (strncmp(p, "fortune", strlen(p)) == 0) {
		int bufsz = 2048;
		char buf[bufsz], bufout[bufsz];
		memset(buf, '\0', bufsz);
		if (get_fortune(buf, bufsz) == 0) {
			snprintf(bufout, bufsz-strlen(buf),
				"```\n%s\n```", buf);
			send_muc_message(conn, bufout, bctx);
		} else {
			send_muc_message(conn, "Error fetching fortune", bctx);
		}
	} else if (strncmp(p, "help", strlen(p)) == 0) {
		char buf[2048];
		snprintf(buf, 2048, BOT_HELP_TMPL,
			VERSION, bctx->nick);
		send_muc_message(conn, buf, bctx);
	} else {
		send_muc_message(conn, "UNKNOWN COMMAND\n", bctx);
	}
}

static void
default_groupchat(bot_ctx_t *bctx, const char *from, const char *body) {
	char c, buf[4096], keybuf[REGSTR_MAX];
	xmpp_conn_t *conn = bctx->conn;

	/* Ignore the first few seconds of messages */
	if (time(NULL)-bctx->ts_start < 10)
		return;

	char body_buf[1024];
	memset(body_buf, '\0', 1024);
	for (size_t i = 0; i < strlen(body) && i < 1023; i++)
		body_buf[i] = tolower(body[i]);

	memset(keybuf, '\0', REGSTR_MAX);
	snprintf(keybuf, REGSTR_MAX, "!%s ", bctx->nick);

	char *from_nick = xmpp_jid_resource(bctx->ctx, from);

	if (bctx->mode == BOT_MODE_DEFAULT) {
		memset(buf, '\0', sizeof(buf));
		size_t blen = strlen(body_buf);
		
		if (strncmp(body_buf, "test", blen) == 0 && blen == 4) {
			snprintf(buf, sizeof(buf), "%s: %s",
				from_nick, "ACK!");
			send_muc_message(conn, buf, bctx);
		}

		if (strncmp(body_buf, "ping", blen) == 0 && blen == 4) {
			snprintf(buf, sizeof(buf), "%s: %s",
				from_nick, "PONG!");
			send_muc_message(conn, buf, bctx);
		}

		if (strncmp(body, keybuf, strlen(keybuf)) == 0) {
			const char *p = body+strlen(keybuf);
			handle_bot_cmd(bctx, p);
		}

		if (strlen(body) > strlen(bctx->nick)+1) {
			c = body[strlen(bctx->nick)];
			if (strncmp(body, bctx->nick, strlen(bctx->nick)) == 0
				&& (c == ':' || c == ',')) {
				snprintf(buf, sizeof(buf),
					"%s: %s\nsee: !%s help",
					from_nick,
					DEFAULT_REPLY,
					bctx->nick);
				send_muc_message(conn, buf, bctx);
			}
		}

	}

	xmpp_free(bctx->ctx, from_nick);
}

static void
default_chat(bot_ctx_t *bctx, const char *from, const char *body) {
	char buf[256];
	(void)body;
	if (bctx->mode == BOT_MODE_DEFAULT) {
		memset(buf, '\0', sizeof(buf));
		snprintf(buf, sizeof(buf), "I am but a simple bot.");
		send_direct_message(from, buf, bctx);
	}
}

static int
cb_message(xmpp_conn_t *const conn,
	xmpp_stanza_t *const stanza,
	void *const userdata) {
	const char *type, *id,
		*from = "",
		*stx  = "\x02",
		*etx  = "\x03",
		*nl   = "\n";

	bot_ctx_t *bctx = userdata;
	xmpp_ctx_t *ctx = bctx->ctx;
	(void)conn;
	
	type = xmpp_stanza_get_type(stanza);
	if (!type)
		return 1;	
	id = xmpp_stanza_get_id(stanza);
	// if (!id)
		// return 1;
	from = xmpp_stanza_get_from(stanza);
	if (!from)
		return 1;

	if (strncmp(type, "groupchat", strlen(type)) == 0) {
		char *body = xmpp_message_get_body(stanza);
		if (!body)
			return 1;

		printf("%s: %s\n", xmpp_stanza_get_from(stanza), body);
		default_groupchat(bctx, from, body);

		if (strlen(bctx->out_fifo)) {

			int fd = open(bctx->out_fifo,
				O_CREAT|O_WRONLY|O_APPEND, 0640);
			if (fd == -1) {
				fprintf(stderr, "Error opening %s\n",
					bctx->out_fifo);
				return 1;
			}

			write(fd, stx, 1);
			write(fd, nl, 1);
			write(fd, from, strlen(from));
			write(fd, nl, 1);
			write(fd, body, strlen(body));
			write(fd, nl, 1);
			write(fd, etx, 1);
			write(fd, nl, 1);
			close(fd);
		}
		
		xmpp_free(ctx, body);

	} else if (strncmp(type, "chat", strlen(type)) == 0) {
		char *body = xmpp_message_get_body(stanza);
		if (!body)
			return 1;
		fprintf(stderr, "%s: ", from);
		fprintf(stderr, "%s\n", body);

		default_chat(bctx, from, body);

		xmpp_free(ctx, body);
	} else {
		fprintf(stderr, "received unhandled message: %s:%s:%s\n",
			type, id, from);
	}

	return 1;
}

static int
cb_handler(xmpp_conn_t *const conn,
	xmpp_stanza_t *const stanza,
	void *const userdata) {
	(void)stanza;
	(void)userdata;

	int quit = 1;
	for (int i = 0; i < running_bots; i++)
		if (!(running_bot[i])->quit) quit = 0;

	if (quit) {
		if (f_debug)
			fprintf(stderr, "cb_handler: xmpp_disconnect()");
		xmpp_disconnect(conn);
		return 0;
	}

	return 1;
}

static int
mk_fifos(bot_ctx_t *bctx) {
	char buf[PATH_MAX];

	if (!bctx->fifo_path || !strlen(bctx->fifo_path))
		return 0;

	if (access(bctx->fifo_path, R_OK | W_OK | X_OK) != 0) {
		fprintf(stderr,
			"Cannot create FIFOs, invalid permissions for: %s\n",
			bctx->fifo_path);
		return FAIL_FIFO_PATH;
	}

	const char *domain = xmpp_jid_domain(bctx->ctx, bctx->muc);
	const char *node = xmpp_jid_node(bctx->ctx, bctx->muc);
	const char *node_j = xmpp_jid_node(bctx->ctx, bctx->jid);
	const char *domain_j = xmpp_jid_domain(bctx->ctx, bctx->jid);

	if (!domain || !node || !node_j || !domain_j) {
		return FAIL_FIFO_JID;
	}

	snprintf(buf, PATH_MAX, "%s/%s", bctx->fifo_path, domain);
	

	if (!is_dir(buf) && !is_file(buf)) {
		if (mkdir(buf, 0700) != 0)
			return FAIL_FIFO_PATH;
	}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	size_t bwa = snprintf(bctx->in_fifo, PATH_MAX, "%s/in-%s",  buf, node);
	size_t bwb = snprintf(bctx->out_fifo,PATH_MAX, "%s/out-%s", buf, node);
	size_t bwc = snprintf(bctx->cmd_fifo,PATH_MAX, "%s/%s@%s",
		bctx->fifo_path, node_j, domain_j);
	if (bwa >= PATH_MAX || bwb >= PATH_MAX || bwc >= PATH_MAX) {
		/* Truncation is actually the feature we want... */
		fprintf(stderr, "ERROR: invalid output_fifo path(s)\n");
		return 0;
	}
#pragma GCC diagnostic pop

	xmpp_free(bctx->ctx, (void *)node);
	xmpp_free(bctx->ctx, (void *)domain);
	xmpp_free(bctx->ctx, (void *)domain_j);
	xmpp_free(bctx->ctx, (void *)node_j);

	if (strlen(bctx->cmd_fifo))
		if (mkfifo(bctx->cmd_fifo, 0664) != 0) {
			fprintf(stderr, "cannot create FIFO: %s\n",
				bctx->cmd_fifo);
			memset(bctx->cmd_fifo, '\0', PATH_MAX);
			return FAIL_FIFO_PATH;
		}

	if (strlen(bctx->in_fifo))
		if (mkfifo(bctx->in_fifo, 0664) != 0) {
			fprintf(stderr, "cannot create FIFO: %s\n",
				bctx->in_fifo);
			memset(bctx->in_fifo, '\0', PATH_MAX);
			return FAIL_FIFO_PATH;
		}

	return RETURN_SUCCESS;
}

static int
del_fifos(bot_ctx_t *bctx) {
	if (strlen(bctx->in_fifo))
		unlink(bctx->in_fifo);
	// if (strlen(bctx->out_fifo))
	// 	unlink(bctx->out_fifo);
	if (strlen(bctx->cmd_fifo))
		unlink(bctx->cmd_fifo);

	return 1;
}

static void
on_connect(xmpp_conn_t * const conn,
	xmpp_conn_event_t status,
	int error,
	xmpp_stream_error_t *stream_error,
	void * const userdata) {
	char muc_jid_buf[REGSTR_MAX];
	bot_ctx_t *bctx = userdata;
	xmpp_ctx_t *ctx = bctx->ctx;

	if (status == XMPP_CONN_CONNECT) {
		memset(muc_jid_buf, '\0', REGSTR_MAX);
		snprintf(muc_jid_buf, REGSTR_MAX, "%s/%s",
			bctx->muc, bctx->nick);

		bctx->connected = 1;
		running_bot[bctx->index] = userdata;

		/* mk_fifos use xmpp_jid_* routines, and require a ctx */
		if (mk_fifos(bctx) != 0) {
			xmpp_disconnect(conn);
			return;
		}
		
		char *cpid = xmpp_uuid_gen(ctx);

		xmpp_handler_add(conn, cb_message, NULL,
			"message", NULL, bctx);
		xmpp_id_handler_add(conn, cb_presence, cpid, bctx);
		xmpp_handler_add(conn, cb_iq, NULL,
			"iq", "get", bctx);
		xmpp_handler_add(conn, cb_handler, NULL,
			NULL, NULL, bctx);
		if (f_usecs)
			xmpp_timed_handler_add(conn,
				cb_heartbeat, f_usecs, bctx);

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
			if (pthread_create(&bctx->in_thread, NULL, 
				th_watch_fifo, userdata) == 0)
				pthread_detach(bctx->in_thread);
			else
				fprintf(stderr,
					"ERROR: Unable to spawn thread\n");
		}

		if (strlen(bctx->cmd_fifo)) {
			if (pthread_create(&bctx->cmd_thread, NULL, 
				th_watch_cmd_fifo, userdata) == 0)
				pthread_detach(bctx->cmd_thread);
			else
				fprintf(stderr,
					"ERROR: Unable to spawn thread\n");
		}

		bctx->ts_start = time(NULL);

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

bot_ctx_t
bot_ctx_new(void) {
	bot_ctx_t bctx;
	memset(&bctx, 0, sizeof(bot_ctx_t));
	bctx.index = running_bots++;

	return bctx;
}

int
start_bot(bot_ctx_t *usr) {
	bot_ctx_t *bctx = usr;
	for (int i = bctx->index; i < MAX_N_BOTS; i++)
		running_bot[i] = NULL;

	if (bctx->jid == NULL || bctx->pass == NULL) {
		fprintf(stderr, "Invalid or missing credentials.\n");
		return FAIL_GENERIC;
	}

	if (!strlen(bctx->jid)) {
		fprintf(stderr, "Invalid or missing credentials.\n");
		return FAIL_GENERIC;
	}

	xmpp_initialize();

	xmpp_log_t *logger = NULL;
	if (f_debug)
		logger = xmpp_get_default_logger(XMPP_LEVEL_DEBUG);

	bctx->ctx = xmpp_ctx_new(NULL, logger);
	if (bctx->ctx == NULL) {
		xmpp_shutdown();
		return FAIL_GENERIC;
	}

	bctx->conn = xmpp_conn_new(bctx->ctx);
	if (bctx->conn == NULL) {
		fprintf(stderr, "Could not start connection.\n");
		xmpp_ctx_free(bctx->ctx);
		xmpp_shutdown();
		return FAIL_GENERIC;
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

	return RETURN_SUCCESS;
}

void
stop_bots(int signal) {
	const char *sigstr = "SIGNAL";

	for (int i = 0; i < running_bots && running_bot[i] != NULL; i++) {
		bot_ctx_t *rb = running_bot[i];
		if (!rb)
			exit(0);

		if (signal == 2)
			sigstr = "SIGINT";
		if (signal == 15)
			sigstr = "SIGTERM";
		else if (signal == 3)
			sigstr = "SIGQUIT";

		if (rb->quit) {
			fprintf(stderr,
				"\nRECEIVED %s (%d), for the 2nd time: killing...",
				sigstr, signal);
			exit(1);
		}

		rb->quit = 1;

		fprintf(stderr,
			"\nRECEIVED %s (%d), shutting down...\n",
			sigstr, signal);

		bot_ctx_t *bctx = rb;
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
}

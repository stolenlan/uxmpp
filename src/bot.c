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
#include <signal.h>
#include <strophe.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <pthread.h>

#include "util.h"
#include "uxmpp.h"
#include "xep-0115-ver.h"
#include "game.h"
#include "disco.h"
#include "sendraw.h"
#include "bot.h"

/* Running bots */
static bot_ctx_t *bot_p[MAX_N_BOTS] = {0};

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
	char *p;

	disco_ctx_t *dctx = malloc(sizeof(disco_ctx_t));
	if (!dctx)
		return NULL;

	/* This function should check things */
	memset(dctx, 0, sizeof(disco_ctx_t));
	snprintf(dctx->target, REGSTR_MAX, "%s", host);

	/* Sanitize the target */
	for (p = dctx->target; *p; p++) {
		if (isspace(*p)) {
			*p = '\0';
			break;
		}
	}

	dctx->result = malloc(DISCO_MAX_RESULT);

	dctx->init = 1;
	dctx->active = 0;
	dctx->conn = bctx->conn;
	dctx->ref = (void *)bctx;
	memset(dctx->result, '\0', DISCO_MAX_RESULT);
	dctx->p = dctx->result;

	return dctx;
}

static void
disco_ctx_free(disco_ctx_t *dctx) {
	free(dctx->result);
	free(dctx);
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
		disco_ctx_free(dctx);

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
	disco_ctx_free(dctx);
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
		dbgout("Unable to open: %s\n", bctx->cmd_fifo);
		return NULL;
	}

	char buf[4096];
	memset(buf, '\0', 4096);
	ssize_t buf_br;
	if( (buf_br = read(fd, buf, 4095)) > 0) {
		dbgout("Received: %s from %s\n", buf, bctx->cmd_fifo);
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
	dbgout("RX: PONG (%s)!\n", domain);

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

static char *
cb_iq_time(void) {
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

static int
cb_iq_version(xmpp_conn_t *conn, xmpp_stanza_t *stanza, void *userdata) {
	/*  Version id handler */
 	char outbuf[2048], verbuf[256];

	const char *type = xmpp_stanza_get_type(stanza);
	const char *from = xmpp_stanza_get_from(stanza);
	(void)conn;
	sendraw_ctx_t *rsctx = (sendraw_ctx_t *)userdata;

	if (type != NULL && strcmp(type, "error") == 0) {
		memset(outbuf, '\0', sizeof(outbuf));
		snprintf(outbuf, sizeof(outbuf),
			"failed to query version for %s", from);
		send_muc_message(conn, outbuf, rsctx->ref);
		free(rsctx);
		return 0;
	}

	char *buf;
	size_t buflen;
	if (xmpp_stanza_to_text(stanza, &buf, &buflen) != 0) {
		fprintf(stderr, "Error converting stanza to text\n");
		free(rsctx);
		return 0;
	}

	parse_version_stanza(verbuf, sizeof(verbuf), buf);

	memset(outbuf, '\0', sizeof(outbuf));
	snprintf(outbuf, sizeof(outbuf), "%s: %s", from, verbuf);
	send_muc_message(conn, outbuf, rsctx->ref);
	
	free(rsctx);
	free(buf);

	return 0;
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
		/* XEP-0202 Time */
		snprintf(buf, 2048,
			"<iq type='result' to='%s' from='%s' id='%s'>"
			"%s"
			"</iq>",
			resp_to_jid,
			resp_from_jid,
			id, cb_iq_time());
	}

	if ((query = xmpp_stanza_get_child_by_ns(stanza, "jabber:iq:version"))
		!= NULL) {
		/* XEP-0092 Software Version */
		snprintf(buf, 2048,
			"<iq type='result' id='%s' to='%s' from='%s'>"
			"<query xmlns='jabber:iq:version'>"
			"<name>uxmpp</name>"
			"<version>%s</version>"
			"<os>Win98SE</os>"
			"</query>"
			"</iq>",
			id, resp_to_jid, resp_from_jid, VERSION);
	}

	if ((query = xmpp_stanza_get_child_by_ns(stanza,
			"http://jabber.org/protocol/disco#info")) != NULL) {
		snprintf(buf, 2048,
			"<iq id=\"%s\" from=\"%s\" to=\"%s\" type='result'>"
			"<query xmlns='http://jabber.org/protocol/disco#info'"
			" node='" XEP_0115_NODE "#" XEP_0115_VER "'>"
			"<identity category='client'"
			" name='uxmpp %s' type='pc'/>"
			"<feature var='http://jabber.org/protocol/caps'/>"
			"<feature var='http://jabber.org/protocol/disco#info'/>"
			"<feature var='http://jabber.org/protocol/disco#items'/>"
			"<feature var='http://jabber.org/protocol/muc'/>"
			"<feature var='urn:xmpp:ping'/>"
			"<feature var='urn:xmpp:time'/>"
			"</query>"
			"</iq>",
			id, resp_from_jid, resp_to_jid, VERSION);
	}

	if ((query = xmpp_stanza_get_child_by_ns(stanza,
		"http://jabber.org/protocol/disco#items")) != NULL) {
		snprintf(buf, 2048,
			"<iq id=\"%s\" from=\"%s\" to=\"%s\" type='result'>"
			"<query xmlns='http://jabber.org/protocol/disco#items'/>"
			"</iq>",
			id, resp_from_jid, resp_to_jid);
	}

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
	bot_ctx_t *bctx = (bot_ctx_t *)userdata;
	xmpp_stanza_t *x, *status, *error, *error_text;
	const char *type;

	type = xmpp_stanza_get_type(stanza);

	if (type && strncmp(type, "error", strlen(type)) == 0) {
		error = xmpp_stanza_get_child_by_name(stanza, "error");
		error_text = xmpp_stanza_get_child_by_name(error, "text");

		if (error && error_text) {
			char *errstr = xmpp_stanza_get_text(error_text);
			fprintf(stderr, "Could not join room: %s\n", errstr);
			xmpp_free(bctx->ctx, errstr);
		}
		
		xmpp_disconnect(conn);

		return 0;
	}

	x = xmpp_stanza_get_child_by_name_and_ns(stanza, "x",
		"http://jabber.org/protocol/muc#user");
	if (!x)
		return 0;

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
				bctx->ts_start = time(NULL);
			}
		}
	}

	return 0;
}

static void
cb_message_grpchat_botcmd(bot_ctx_t *bctx, const char *cmd) {
	xmpp_conn_t *conn = bctx->conn;

	const char *p;

	/* Trim leading whitespace */
	for (p = cmd; *p && isspace(*p); p++);

	if (strncmp(p, "disco", strlen("disco")) == 0) {
		/* p Will become the pointer to start of JID */
		for (p = p+strlen("disco");*p && isspace(*p); p++);
		if (!strlen(p)) {
			send_muc_message(conn, "Usage: disco <jid>", bctx);
		} else {
			if (strncmp(p, bctx->jid, strlen(bctx->jid)) == 0) {
				send_muc_message(conn,
					"No ;)", bctx);
				return;
			}

			disco_ctx_t *dctx = disco_init(bctx, p);

			char buf[1024];
			snprintf(buf, 1024,
				"performing discovery of %s...", dctx->target);
			if (!dctx->active) {
				dctx->active = 1;
				send_muc_message(conn, buf, bctx);
				request_disco_info(dctx, bot_disco_reply);
			} else {
				send_muc_message(conn,
					"Service busy.", bctx);
			}
		}
	} else if (strncmp(p, "version", strlen("version")) == 0) {
		
		/* p Will become the pointer to start of JID */
		for (p = p+strlen("version");*p && isspace(*p); p++);
		if (!strlen(p)) {
			send_muc_message(conn, "Usage: version <jid>", bctx);
		} else {
			char tgt[2048];
			snprintf(tgt, sizeof(tgt),
				"<iq type='get' to='%s'>"
				"<query xmlns='jabber:iq:version'/></iq>", p);
			char buf[2048];
			memset(buf, '\0', sizeof(buf));

			sendraw_ctx_t *rsctx = malloc(sizeof(sendraw_ctx_t));
			rsctx->conn = conn;
			rsctx->target = tgt;
			rsctx->result = buf;
			rsctx->result_max = sizeof(buf);
			rsctx->ref = bctx;
			/* This must be free'd */
			send_id_rawiq_prepare(rsctx, cb_iq_version);
		}	
	} else if (strncmp(p, "fortune", strlen(p)) == 0) {
		int bufsz = 4096;
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
		snprintf(buf, 2048, BOT_HELP_TMPL, VERSION, bctx->nick);
		send_muc_message(conn, buf, bctx);
	} else if (strncmp(p, "gtfo", strlen(p)) == 0) {
		if (bctx == bot_p[0])
			stop_bots();
		else
			send_muc_message(conn, "This bot is a: "
				"spawn == peasant "
				"== no perms, for now...",
				bctx);
	} else if (strncmp(p, "spawn", strlen("spawn")) == 0) {
		if (bot_p[0] != bctx) {
			send_muc_message(conn, "Spawn may not spawn more "
				"Spawn... For now.", bctx);

			return;
		}

		for (p = p+strlen("spawn");*p && isspace(*p); p++);

		if (!strlen(p)) {
			send_muc_message(conn,
				"Usage: spawn <muc_jid> <nick> <mode>", bctx);
		} else {
			bot_ctx_t *nb = spawn_bot(p);
			if (!nb) {
				send_muc_message(conn,
		 			"Usage: spawn <jid> <nick> <mode>", bctx);
			} else {
				send_muc_message(conn, "Bot spawned...", bctx);
			}
		}
	} else {
		send_muc_message(conn, "Unknown command, see: 'help'", bctx);
	}
}

static bot_ctx_t *
select_bot(bot_ctx_t *bctx, const char *from) {

	const char *from_node = xmpp_jid_node(bctx->ctx, from);
	const char *from_domain = xmpp_jid_domain(bctx->ctx, from);
	
	int i;
	for (i = MAX_N_BOTS; i >= 0; i--) {
		bot_ctx_t *pb = bot_p[i];
		if (!pb)
			continue;

		const char *muc_node = xmpp_jid_node(bctx->ctx, pb->muc);
		const char *muc_domain = xmpp_jid_domain(bctx->ctx, pb->muc);
		if (strncmp(from_node, muc_node, strlen(from_node)) == 0
			&& strncmp(from_domain,muc_domain,strlen(from_node)) == 0) {
			xmpp_free(bctx->ctx, (void *)from_node);
			xmpp_free(bctx->ctx, (void *)from_domain);
			xmpp_free(bctx->ctx, (void *)muc_node);
			xmpp_free(bctx->ctx, (void *)muc_domain);
			return pb;
		}
		xmpp_free(bctx->ctx, (void *)muc_node);
		xmpp_free(bctx->ctx, (void *)muc_domain);
	}

	xmpp_free(bctx->ctx, (void *)from_node);
	xmpp_free(bctx->ctx, (void *)from_domain);

	return NULL;
}

static void
cb_message_grpchat(bot_ctx_t *bctx, const char *from, const char *body) {
	char c, buf[4096], keybuf[REGSTR_MAX];
	xmpp_conn_t *conn = bctx->conn;

	bot_ctx_t *target_bot = select_bot(bctx, from);

	/* Ignore the first few seconds of messages */
	if (!target_bot->joined || !target_bot->ts_start 
		|| time(NULL)-target_bot->ts_start < BOT_DWELL_SECS)
		return;

	char body_buf[1024];
	memset(body_buf, '\0', 1024);
	for (size_t i = 0; i < strlen(body) && i < 1023; i++)
		body_buf[i] = tolower(body[i]);

	memset(keybuf, '\0', REGSTR_MAX);
	snprintf(keybuf, REGSTR_MAX, "!%s ", target_bot->nick);

	char *from_nick = xmpp_jid_resource(target_bot->ctx, from);

	if (target_bot->mode == BOT_MODE_DEFAULT) {
		memset(buf, '\0', sizeof(buf));
		size_t blen = strlen(body_buf);

		handle_game_mbody(target_bot, from_nick, body);
		
		if (strncmp(body_buf, "test", blen) == 0 && blen == 4) {
			snprintf(buf, sizeof(buf), "%s: %s",
				from_nick, "ACK!");

			send_muc_message(conn, buf, target_bot);
		}

		if (strncmp(body_buf, "ping", blen) == 0 && blen == 4) {
			snprintf(buf, sizeof(buf), "%s: %s",
				from_nick, "PONG!");
			send_muc_message(conn, buf, target_bot);
		}

		if (strncmp(body, keybuf, strlen(keybuf)) == 0) {
			const char *p = body+strlen(keybuf);
			cb_message_grpchat_botcmd(target_bot, p);
		}

		if (strlen(body) > strlen(target_bot->nick)+1) {
			c = body[strlen(target_bot->nick)];
			if (strncmp(body, target_bot->nick,
				strlen(target_bot->nick)) == 0
				&& (c == ':' || c == ',')) {
				snprintf(buf, sizeof(buf),
					"%s: %s\nsee: !%s help",
					from_nick,
					BOT_DEFAULT_REPLY,
					target_bot->nick);
				send_muc_message(conn, buf, target_bot);
			}
		}
	}
	
	xmpp_free(bctx->ctx, from_nick);
}

static void
cb_message_chat(bot_ctx_t *bctx, const char *from, const char *body) {
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
	from = xmpp_stanza_get_from(stanza);
	if (!from)
		return 1;

	if (strncmp(type, "groupchat", strlen(type)) == 0) {
		char *body = xmpp_message_get_body(stanza);
		if (!body)
			return 1;

		printf("%s: %s\n", xmpp_stanza_get_from(stanza), body);
		
		cb_message_grpchat(bctx, from, body);

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

		cb_message_chat(bctx, from, body);

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

	/* If any bot is still active (quit == 0) keep conn active */
	for (int i = 0; i < MAX_N_BOTS; i++) {
		if (bot_p[i] && !(bot_p[i])->quit) {
			quit = 0;
			break;
		}
	}

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

		/* mk_fifos use xmpp_jid_* routines, and require a ctx */
		if (mk_fifos(bctx) != 0) {
			xmpp_disconnect(conn);
			return;
		}
		
		char *cpid = xmpp_uuid_gen(ctx);

		xmpp_handler_add(conn, cb_message, NULL, "message", NULL, bctx);
		xmpp_id_handler_add(conn, cb_presence, cpid, bctx);
		xmpp_handler_add(conn, cb_iq, NULL, "iq", "get", bctx);
		xmpp_handler_add(conn, cb_handler, NULL, NULL, NULL, bctx);

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
			"<c xmlns='http://jabber.org/protocol/caps'"
				" hash='sha-1'"
				" node='" XEP_0115_NODE "'"
				" ver='" XEP_0115_VER "'/>");
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


void
bot_ctx_free(bot_ctx_t *bctx) {
	int i;
	for (i = 0; i < MAX_N_BOTS && bot_p[i]; i++) {
		if (bot_p[i] == bctx) {
			free(bot_p[i]);
			bot_p[i] = NULL;
		}
	}
	(void)bctx;
}


bot_ctx_t *
bot_ctx_new(void) {

	/* Iterate over bot_p(ointers) and use the first available NULL */
	int i;
	for (i = 0; i < MAX_N_BOTS && bot_p[i]; i++);

	bot_ctx_t *bctx = malloc(sizeof(bot_ctx_t));
	memset(bctx, 0, sizeof(bot_ctx_t));
	bctx->index = i;
	bot_p[bctx->index] = bctx;

	return bctx;
}


/**
 * WORKING PRINCIPALs
 * 
 * Multiple bots may share one connection, but there must be a "seed" bot.
 * The seed is responsible for both providing an entry point into the
 * corresponding strophe runloop (by way of fifo's or in-band access), and
 * provides the facilities for spawning more bots etc.
 */
int
start_bot(bot_ctx_t *bctx) {

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

	/* The run-loop has exited, cleanup the seed bot... */
	xmpp_conn_release(bctx->conn);
	xmpp_ctx_free(bctx->ctx);
	xmpp_shutdown();
	del_fifos(bctx);
	free(bctx);

	return RETURN_SUCCESS;
}


void
stop_bot(bot_ctx_t *bctx) {
	char muc_jid_buf[REGSTR_MAX];
	int i;

	if (!bctx) {
		fprintf(stderr, "Invalid BOT!!!\n");
		return;
	}

	if (bctx->quit) {
		fprintf(stderr, "stop_bot called twice for the same bot...\n");
		return;
	}

	bctx->quit = 1;
	xmpp_ctx_t *ctx = bctx->ctx;
	xmpp_conn_t *conn = bctx->conn;

	xmpp_stanza_t *presence = xmpp_presence_new(ctx);
	xmpp_stanza_set_id(presence, bctx->jid);
	xmpp_stanza_set_type(presence, "unavailable");
	memset(muc_jid_buf, '\0', REGSTR_MAX);
	snprintf(muc_jid_buf, REGSTR_MAX, "%s/%s",
		bctx->muc, bctx->nick);
	xmpp_stanza_set_to(presence, muc_jid_buf);
	xmpp_send(conn, presence);
	xmpp_stanza_release(presence);

	for (i = MAX_N_BOTS-1; i >= 0 && bctx != bot_p[i]; i--);

	if (i > 0) {
		free(bctx);
		bot_p[i] = NULL;
	}
}

void
stop_bots(void) {
	for (int i = MAX_N_BOTS-1; i >= 0; i--) {
		if (!bot_p[i])
			continue;
		stop_bot(bot_p[i]);
	}
}


static int
valid_muc_jid(const char *jid) {
	fprintf(stderr, "valid_muc_jid (%s): ", jid);
	xmpp_ctx_t *ctx = xmpp_ctx_new(NULL, NULL);

	char *node = xmpp_jid_node(ctx, jid);
	char *domain = xmpp_jid_domain(ctx, jid);
	char *resource = xmpp_jid_resource(ctx, jid);

	if (node && domain && strlen(node) && strlen(domain) && !resource) {
		fprintf(stderr, "SUCCESS\n");
		xmpp_free(ctx, node);
		xmpp_free(ctx, domain);
		xmpp_free(ctx, resource);
		xmpp_ctx_free(ctx);
		return 0;
	}

	fprintf(stderr, " ** FAILURE **: ");
	unsigned short int rv = 0x01;

	if (!node || !strlen(node))
		rv = (rv|0x02);

	if (!domain || !strlen(domain))
		rv = (rv|0x04);

	if (resource)
		rv = (rv|0x08);

	xmpp_free(ctx, node);
	xmpp_free(ctx, domain);
	xmpp_free(ctx, resource);
	xmpp_ctx_free(ctx);

	fprintf(stderr, "errval (%d)\n", rv);
	return rv;
}

bot_ctx_t *
spawn_bot(const char *spawn_cfg) {

	char muc_jid_buf[REGSTR_MAX];
	bot_ctx_t *bot = bot_ctx_new();
	if (!bot)
		return NULL;

	char *mode_str = NULL;
	snprintf(bot->spawn_cfg, SPAWN_CFG_MAX, "%s", spawn_cfg);
	int va = parse_arg_str(bot->spawn_cfg, 3,
		&bot->muc, &bot->nick, &mode_str);

	if (va != 3) {
		fprintf(stderr, "Failed to parse cfg string (%d).\n", va);
		bot_ctx_free(bot);
		return NULL;
	}

	if (valid_muc_jid(bot->muc) != 0) {
		fprintf(stderr, "Failed to create spawn, invalid JID (muc)\n");
		bot_ctx_free(bot);
		return NULL;
	}

	bot->mode 		= atoi(mode_str);
	bot->jid		= (bot_p[0])->jid;
	bot->pass		= (bot_p[0])->pass;
	bot->conn		= (bot_p[0])->conn;
	bot->ctx		= (bot_p[0])->ctx;
	bot->fifo_path 	= (bot_p[0])->fifo_path;
	bot->connected 	= 1;

	xmpp_ctx_t *ctx = bot->ctx;
	xmpp_conn_t *conn = bot->conn;

	char *cpid = xmpp_uuid_gen(ctx);

	memset(muc_jid_buf, '\0', REGSTR_MAX);
	snprintf(muc_jid_buf, REGSTR_MAX, "%s/%s",
		bot->muc, bot->nick);

	xmpp_stanza_t *pres = xmpp_presence_new(ctx);
	xmpp_send(conn, pres);
	xmpp_stanza_release(pres);

	xmpp_stanza_t *presence = xmpp_stanza_new(ctx);
	xmpp_stanza_set_name(presence, "presence");
	
	xmpp_stanza_set_id(presence, cpid);
	xmpp_stanza_set_to(presence, muc_jid_buf);

	xmpp_stanza_t *c = xmpp_stanza_new_from_string(ctx,
		"<c xmlns='http://jabber.org/protocol/caps'"
			" hash='sha-1'"
			" node='" XEP_0115_NODE "'"
			" ver='" XEP_0115_VER "'/>");
	xmpp_stanza_add_child(presence, c);
	xmpp_stanza_release(c);

	xmpp_stanza_t *x = xmpp_stanza_new_from_string(ctx,
		"<x xmlns='http://jabber.org/protocol/muc'/>");
	xmpp_stanza_add_child(presence, x);
	xmpp_stanza_release(x);

	xmpp_id_handler_add(conn, cb_presence, cpid, bot);

	xmpp_send(conn, presence);
	xmpp_stanza_release(presence);
	xmpp_free(ctx, cpid);

	if (strlen(bot->in_fifo)) {
		if (pthread_create(&bot->in_thread, NULL, 
			th_watch_fifo, (void *)bot) == 0)
			pthread_detach(bot->in_thread);
		else
			fprintf(stderr,
				"ERROR: Unable to spawn thread\n");
	}

	if (strlen(bot->cmd_fifo)) {
		if (pthread_create(&bot->cmd_thread, NULL, 
			th_watch_cmd_fifo, (void *)bot) == 0)
			pthread_detach(bot->cmd_thread);
		else
			fprintf(stderr,
				"ERROR: Unable to spawn thread\n");
	}

	return bot;
}

void	bot_sighandler(int signal) {
	if (signal == SIGPIPE) {
		fprintf(stderr, "RECEIVED SIGPIPE (%d): ignoring...\n", signal);
		return;
	}

	static int last_signal = -1;

	if (last_signal != -1) {
		fprintf(stderr, "\nRECEIVED 2nd SIGNAL(%d): killing...\n", signal);
		exit(0);
	}

	last_signal = signal;

	fprintf(stderr, "\nRECEIVED SIGNAL(%d) Shutting down...\n", signal);

	stop_bots();
}

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
#ifndef _BOT_H
#define _BOT_H 1

#include <limits.h>
#include <time.h>
#include <strophe.h>
#include <pthread.h>

#include "uxmpp.h"

#define BOT_DEFAULT_REPLY \
"Sorry?\n"\
"Just a dumb bot here, no LLMs, telemetry, or funny-business.\n"

#define BOT_HELP_TMPL \
"```\n"\
"uxmpp-%s:bot\n"\
"Utilities For XMPP: in-band bot help\n\n"\
"Commands (prefixed with `!%s'):\n"\
"help             Show this help\n"\
"disco    <jid>   Perform disco#info on <jid>\n"\
"version  <jid>   Get version information for <jid>\n"\
"fortune          Display a classic fortune\n"\
"```"

#define BOT_DWELL_SECS 5
#define MAX_N_BOTS 256
#define SPAWN_CFG_MAX 1024

enum bot_mode_t {BOT_MODE_NONE = 0, BOT_MODE_DEFAULT};

typedef struct {
	char		*in_fifo;
	char		*out_fifo;
	char		*cmd_fifo;
	char		*fifo_path;
	char		*jid;
	char		*muc;
	char		*nick;
	char		*pass;
	char 		*spawn_cfg;

	int		connected;
	int		index;
	int		mode;
	int		quit;
	int		joined;

	time_t		ts_start;

	pthread_t	cmd_thread;
	pthread_t	in_thread;

	xmpp_conn_t	*conn;
	xmpp_ctx_t	*ctx;
} bot_ctx_t;

bot_ctx_t *	bot_ctx_new(void);
void		bot_ctx_free(bot_ctx_t *bctx);
void		bot_sighandler(int signal);

/* Spawn bot (using existing connection) and return it's index (-1 on fail) */
bot_ctx_t *	spawn_bot(const char *spawn_cfg);
int		start_bot(bot_ctx_t *bctx);
void		stop_bot(bot_ctx_t *bctx);
void		stop_bot(bot_ctx_t *bctx);
void		stop_bots(void);

#endif

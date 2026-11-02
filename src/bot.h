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

enum bot_mode_t {BOT_MODE_NONE = 0, BOT_MODE_DEFAULT};

typedef struct {
	char 		in_fifo[PATH_MAX];
	char 		out_fifo[PATH_MAX];
	char		cmd_fifo[PATH_MAX];
	char		*fifo_path;
	char 		*jid;
	char 		*muc;
	char 		*nick;
	char 		*pass;

	int 		connected;
	int 		index;
	int 		mode;
	int 		quit;
	int		joined;
	time_t		ts_start;

	pthread_t	cmd_thread;
	pthread_t	in_thread;

	xmpp_conn_t 	*conn;
	xmpp_ctx_t 	*ctx;
} bot_ctx_t;

bot_ctx_t 	bot_ctx_new(void);
int 		start_bot(bot_ctx_t *usr);
void 		stop_bots(int signal);

#endif

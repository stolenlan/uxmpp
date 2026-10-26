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
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <strophe.h>

#include "uxmpp.h"
#include "bot.h"
#include "disco.h"
#include "ping.h"

#define CMDLEN_MAX 256

char *__progname;
int 	f_debug = 0;          /* Debug Flag */
int  	f_fifos = 0;

int
main(int argc, char *argv[])
{
	char 	c, 
		command[CMDLEN_MAX], host[HSTLEN_MAX],
		jid[REGSTR_MAX], password[REGSTR_MAX],
		muc[REGSTR_MAX], cwd[PATH_MAX],
		bot_in_fifo[PATH_MAX], bot_out_fifo[PATH_MAX],
		nick[REGSTR_MAX];

	#ifdef __OpenBSD__
	if (pledge("stdio unveil dns inet rpath cpath dpath wpath tty", 
		NULL) < 0)
		return 1;
	#endif

	memset(command,      '\0', CMDLEN_MAX);
	memset(host,         '\0', HSTLEN_MAX);
	memset(jid,          '\0', REGSTR_MAX);
	memset(password,     '\0', REGSTR_MAX);
	memset(muc,          '\0', REGSTR_MAX);
	memset(nick,         '\0', REGSTR_MAX);
	memset(bot_in_fifo,  '\0', REGSTR_MAX);
	memset(bot_out_fifo, '\0', REGSTR_MAX);
	
	while((c = getopt(argc, argv, "dfh:i:j:m:n:o:p:v")) != -1) {
		switch (c) {
		case 'd':
			f_debug = 1;
			break;
		case 'f':
			f_fifos = 1;
			break;
		case 'h':	
			snprintf(host, sizeof(host), "%s", optarg);
			break;
		case 'i':
			f_fifos = 1;
			snprintf(bot_in_fifo, REGSTR_MAX, "%s", optarg);
			break;
		case 'j':
			snprintf(jid, REGSTR_MAX, "%s", optarg);
			break;
		case 'm':
			snprintf(muc, REGSTR_MAX, "%s", optarg);
			break;
		case 'n':
			snprintf(nick, REGSTR_MAX, "%s", optarg);
			break;
		case 'o':
			f_fifos = 1;
			snprintf(bot_out_fifo, REGSTR_MAX, "%s", optarg);
			break;
		case 'p':
			snprintf(password, REGSTR_MAX, "%s", optarg);
			break;
		case 'v':
			printf("%s\n", VERSION);
			return 0;
		default:
			printf(USAGE_STR, __progname);
			return 0;
		}
	}

	memset(cwd, '\0', PATH_MAX);
	getcwd(cwd, PATH_MAX);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
	/* If the combined path is truncated, we may have an issue */
	if (f_fifos && !strlen(bot_out_fifo) && !strlen(bot_in_fifo)) {
		if (!strlen(bot_in_fifo) && strlen(muc)) {
			snprintf(bot_in_fifo, PATH_MAX, "%s/%s-%s", 
				cwd, "to", muc);
		}

		if (!strlen(bot_out_fifo) && strlen(muc)) {
			snprintf(bot_out_fifo, PATH_MAX, "%s/%s-%s", 
				cwd, "from", muc);
		}
	}
	/* TODO: use access() to determine if the path is valid first */
#pragma GCC diagnostic pop

#ifdef __OpenBSD__
	if (strlen(bot_in_fifo) && unveil(bot_in_fifo, "rwc") < 0)
		return 1;

	if (strlen(bot_out_fifo) && unveil(bot_out_fifo, "rwc") < 0)
		return 1;

	if ( unveil("/etc/hosts", "r") < 0
		|| unveil("/etc/resolv.conf", "r") < 0
		|| unveil("/etc/ssl/cert.pem", "r") < 0)
		return 1;
	if ( unveil(NULL,NULL) < 0)
		return 1;
#endif

	if(optind < argc && strlen(argv[optind]) < CMDLEN_MAX ) {
		snprintf(command, CMDLEN_MAX, "%s", argv[optind++]);
	} else {
		if (strncmp(__progname, "uxmpp", strlen(__progname)) == 0) {
			printf(USAGE_STR, __progname);
			return 1;
		} else {
			snprintf(command, CMDLEN_MAX, "%s", __progname);
		}
		
	}

	if (strncmp(command, "disco", strlen(command)) == 0) {
		if (!strlen(host)) {
			printf("host disco requires a host to disco, "
				"specify with -h\n");
			return 1;
		}

		if (!get_jid_credentials(jid, password))
			return 1;

		return host_disco(jid, password, host);
	}

	if (strncmp(command, "mucbot", strlen(command)) == 0) {
		if (!strlen(muc)) {
			printf("A muc_jid is required, "
				"specify with -m\n");
			return 1;
		}

		if (!get_jid_credentials(jid, password))
			return 1;

		if (!strlen(nick))
			jid_get_local(nick, jid);

		bot_ctx_t bot;
		bot.nick = nick;
		bot.jid = jid;
		bot.pass = password;
		bot.in_fifo = bot_in_fifo;
		bot.out_fifo = bot_out_fifo;
		bot.muc = muc;

		/* 2 == SIGINT, 3 == SIGQUIT, 15 == SIGTERM 
		   The reason for not using the defs is: because Linux. */
		signal(2, stop_bot);
		signal(3, stop_bot);
		signal(15, stop_bot);

		return start_bot(&bot);
	}


	if (strncmp(command, "ping", strlen(command)) == 0) {
		if (!strlen(muc)) {
			printf("A jid is required, "
				"specify with -m\n");
			return 1;
		}

		if (!get_jid_credentials(jid, password))
			return 1;

		return xmpp_ping(jid, password, muc);
		
	}

	printf("Unknown command: %s\n", command);
	printf(USAGE_STR, __progname);
	return 1;
}

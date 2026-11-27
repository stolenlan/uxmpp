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

#include "util.h"
#include "uxmpp.h"
#include "xep-0115-ver.h"
#include "bot.h"
#include "disco.h"
#include "ping.h"
#include "sendraw.h"

#define CMDLEN_MAX 256
#define COL_WIDTH 12

char *__progname;

/* extern (global) flags */
int	f_debug	= 0;		/* Debug Flag */
int 	f_mdown = 0;		/* Markup with markdown... */
int	f_usecs	= 60000;	/* uSeconds */
int	f_adult = 0;

/* Local flags */
int 	f_bot_mode = 0;
int 	f_show_vers = 0;

int
main(int argc, char *argv[])
{
	char 	c, command[CMDLEN_MAX], fifo_path[PATH_MAX],
		jid[REGSTR_MAX], password[REGSTR_MAX],
		rstanza[STANZA_MAX],
		target_jid[REGSTR_MAX],
		in_path[PATH_MAX], out_path[PATH_MAX],
		nick[REGSTR_MAX];

	struct 	sigaction sps;
	sps.sa_flags = 0;
	sps.sa_handler = sigpipe_supress;

	sigemptyset(&sps.sa_mask);
	sigaddset(&sps.sa_mask, SIGPIPE);
	sigaction(SIGPIPE, &sps, NULL);

	memset(command,      '\0', CMDLEN_MAX);
	memset(fifo_path,    '\0', PATH_MAX);
	memset(in_path,      '\0', PATH_MAX);
	memset(jid,          '\0', REGSTR_MAX);
	memset(nick,         '\0', REGSTR_MAX);
	memset(out_path,     '\0', PATH_MAX);
	memset(password,     '\0', REGSTR_MAX);
	memset(rstanza,	     '\0', STANZA_MAX);
	memset(target_jid,   '\0', REGSTR_MAX);
	
	while((c = getopt(argc, argv, "df:i:j:mn:o:p:r:t:u:vz")) != -1) {
		switch (c) {
		case 'd':
			f_debug = 1;
			break;
		case 'f':
			snprintf(fifo_path, PATH_MAX, "%s", optarg);
			break;
		case 'i':
			snprintf(in_path, REGSTR_MAX, "%s", optarg);
			break;
		case 'j':
			snprintf(jid, REGSTR_MAX, "%s", optarg);
			break;
		case 'm':
			f_bot_mode = BOT_MODE_DEFAULT;
			break;
		case 'n':
			snprintf(nick, REGSTR_MAX, "%s", optarg);
			break;
		case 'o':
			snprintf(out_path, REGSTR_MAX, "%s", optarg);
			break;
		case 'p':
			snprintf(password, REGSTR_MAX, "%s", optarg);
			break;
		case 'r':
			snprintf(rstanza, REGSTR_MAX, "%s", optarg);
			break;
		case 't':
			snprintf(target_jid, REGSTR_MAX, "%s", optarg);
			break;
		case 'u':
			f_usecs = atoi(optarg);
			break;
		case 'v':
			f_show_vers = 1;
			break;
		case 'z':
			f_adult = 1;
			break;
		default:
			printf(USAGE_STR, __progname);
			return 0;
		}
	}

#ifdef __OpenBSD__
	/* I don't like having tty here, but password prompt needs it */
	const char *envpass = getenv("XMPP_PASSWORD");
	if (!envpass || !strlen(password)) {
		if (pledge("stdio unveil dns inet rpath cpath "
			"dpath wpath tty proc exec", NULL) < 0)
			return 1;
	} else {
		if (pledge("stdio unveil dns inet rpath cpath "
			"dpath wpath proc exec", NULL) < 0)
			return 1;
	}
#endif

	if (f_show_vers) {
		if (f_debug) {
			printf("%-*s: %s\n", COL_WIDTH,
				"XEP-0115/VER",
				XEP_0115_VER);
			printf("%-*s: %s\n", COL_WIDTH,
				"XEP-0115/S",
				XEP_0115_STR);
			printf("%-*s: ", COL_WIDTH,
				"UXMPP");
		}	
		printf("%s\n", VERSION);
		return 0;
	}

	
	if (strlen(fifo_path)) {
		if (!is_dir(fifo_path)) {
			fprintf(stderr, "%s is not a directory, "
				"or not accessible\n", fifo_path);
			return 1;
		}
	}

#ifdef __OpenBSD__	
	if ( unveil("/etc/hosts", "r") < 0
		|| unveil("/etc/resolv.conf", "r") < 0
		|| unveil("/etc/ssl/cert.pem", "r") < 0
		|| unveil("/usr/games/fortune", "rx") < 0)
		return 1;

	if (strlen(fifo_path))
		if (unveil(fifo_path, "rwc") < 0)
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
		if (!strlen(target_jid)) {
			printf("disco requires a host/target to disco, "
				"specify with -t\n");
			return 1;
		}

		if (!get_jid_credentials(jid, password))
			return 1;

		return disco_out(jid, password, target_jid);
	}

	if (strncmp(command, "bot", strlen(command)) == 0) {
		if (!strlen(target_jid)) {
			printf("A target_jid (MUC) is required, "
				"specify with -t\n");
			return 1;
		}

		if (!get_jid_credentials(jid, password))
			return 1;

		if (!strlen(nick))
			jid_get_local(nick, jid);

		bot_ctx_t *bot = bot_ctx_new();

		if (!bot) {
			printf("Error allocating memory\n");
			return 1;
		}

		bot->nick = nick;
		bot->jid = jid;
		bot->pass = password;
		bot->muc = target_jid;
		bot->mode = f_bot_mode;
		bot->fifo_path = fifo_path;

		struct sigaction sa;
		sa.sa_flags = 0;
		sa.sa_handler = bot_sighandler;

		sigemptyset(&sa.sa_mask);
		sigaddset(&sa.sa_mask, SIGINT);
		sigaction(SIGINT, &sa, NULL);
		
		int retval = start_bot(bot);
		if (retval != RETURN_SUCCESS)
			fprintf(stderr, "Error: unable to start bot\n");

		bot_ctx_free(bot);

		return retval;
	}

	if (strncmp(command, "ping", strlen(command)) == 0) {
		if (!strlen(target_jid)) {
			printf("A target_jid is required, "
				"specify with -t\n");
			return 1;
		}

		if (!get_jid_credentials(jid, password))
			return 1;

		return xmpp_ping(jid, password, target_jid);
	}

	if (strncmp(command, "sendraw", strlen(command)) == 0) {
		if (!strlen(rstanza)) {
			printf("A raw_stanza is required, "
				"specify with -r\n");
			return 1;
		}

		if (!get_jid_credentials(jid, password))
			return 1;

		return send_rawiq_id(jid, password, rstanza, NULL, 0);
	}

	if (strncmp(command, "version", strlen(command)) == 0) {
		if (!strlen(target_jid)) {
			printf("A target_jid is required, "
				"specify with -t\n");
			return 1;
		}

		if (!get_jid_credentials(jid, password))
			return 1;

		memset(rstanza, '\0', STANZA_MAX);
		snprintf(rstanza, STANZA_MAX, "<iq type='get' to='%s'>"
			"<query xmlns='jabber:iq:version'/></iq>", target_jid);

		char version[2048];
		memset(version, '\0', sizeof(version));

		int retval = send_rawiq_id(jid, password,
			rstanza, version, sizeof(version));

		if (retval == 0) {
			char buf[1024];
			parse_version_stanza(buf, sizeof(buf), version);
			printf("%s\n", buf);
		}

		return retval;
	}

	printf("Unknown command: `%s` ("
	       "valid commands: `disco`, `ping`, and `bot`)\n", command);

	printf(USAGE_STR, __progname); 
	return 1;
}

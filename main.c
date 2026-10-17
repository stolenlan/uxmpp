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
#include <stdlib.h>
#include <strophe.h>

#include "uxmpp.h"
#include "bot.h"
#include "disco.h"

char *__progname;
int f_debug;

int
main(int argc, char *argv[])
{
	char c, command[256], host[1024],
		*jid = NULL, *muc = NULL, *password = NULL;

#ifdef __OpenBSD__
	if (pledge("stdio unveil dns inet rpath tty", NULL) < 0)
		return 1;
	if ( unveil("/etc/hosts", "r") < 0
		|| unveil("/etc/resolv.conf", "r") < 0
		|| unveil("/etc/ssl/cert.pem", "r") < 0
		|| unveil(NULL,NULL) < 0)
		return 1;
#endif
	
	while((c = getopt(argc, argv, "dh:m:p:u:v")) != -1) {
		switch (c) {
		case 'd':
			f_debug = 1;
			break;
		case 'h':
			memset(host, '\0', sizeof(host));
			snprintf(host, sizeof(host), "%s", optarg);
			break;
		case 'm':
			muc = optarg;
			break;
		case 'p':
			password = optarg;
			break;
		case 'u':
			jid = optarg;
			break;
		case 'v':
			printf("%s\n", VERSION);
			return 0;
		default:
			printf(USAGE_STR, __progname);
			return 0;
		}
	}

	if(optind < argc && strlen(argv[optind]) < sizeof(command)) {
		snprintf(command, sizeof(command), "%s", argv[optind++]);
	} else {
		printf(USAGE_STR, __progname);
		return 1;
	}

	if (strncmp(command, "disco", strlen(command)) == 0) {
		if (!strlen(host)) {
			printf("host disco requires a host to disco, "
				"specify with -h\n");
			return 1;
		}
		return host_disco(jid, password, host);
	}

	if (strncmp(command, "mucdroid", strlen(command)) == 0) {
		if (!muc || !strlen(muc)) {
			printf("A muc_jid is required, "
				"specify with -m\n");
			return 1;
		}
		return start_bot(jid, muc, NULL);
	}

	printf(USAGE_STR, __progname);
	return 1;
}

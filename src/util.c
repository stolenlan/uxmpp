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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <netdb.h>

#include "color.h"
#include "util.h"

extern int f_debug;

int
checkurl(const char *hostname) {
	struct addrinfo hints, *res;
	int status;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; /* AF_INET | AF_INET6 */
	hints.ai_socktype = SOCK_STREAM;
	
	dbgout("checkurl %s: ", hostname);
	if ((status = getaddrinfo(hostname, "", &hints, &res)) != 0) {
		dbgout("getaddrinfo: %s\n", gai_strerror(status));
		return 1;
	}

	struct addrinfo *p;
	for (p = res; p != NULL; p = p->ai_next) {
		dbgout("VALID\n");
		freeaddrinfo(res); // Free the linked list
		return 0;
	}
	dbgout("INVALID\n");
	freeaddrinfo(res); // Free the linked list

	return 1;
}

void
dbgout(const char *fmt, ...) {
    if (!f_debug)
    	return;

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

int
is_dir(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode);
	return 0;
}

int
is_file(const char *path)
{
	struct stat st;
	if (stat(path, &st) == 0)
		return S_ISREG(st.st_mode);
	return 0;
}

int 
parse_arg_str(char *buf, int n, ...) {

	/* TODO: Handle quoted/escaped arguments */

	va_list ap;
	va_start(ap, n);

	int i = 0;
	char **p, *buf_p = buf;

	// Strip any leading space
	for (; *buf_p && isspace(*buf_p); buf_p++);

	for(i = 0; i < n && *buf_p; i++) {
		p = va_arg(ap, char **);
		*p = buf_p;

		/* seek through all non-space characters */
		for (;*buf_p && !isspace(*buf_p); buf_p++);

		/* seek and replace all space chars up to the next non-space */
		for (; *buf_p && isspace(*buf_p); buf_p++)
			*buf_p = '\0';
	}

	va_end(ap);

	return i;
}

# Copyright (c) 2026 Tyler Anderson <achaean@sinelabs.ca>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

PREFIX?=/usr/local
MANPREFIX=$(PREFIX)

_CFLAGS=-O2 -fstack-protector-strong -fdiagnostics-color -Wall -Wextra \
	-std=c17 \
	-Wpedantic -Wshadow -D_DEFAULT_SOURCE -I/usr/local/include $(CFLAGS)
LDFLAGS=-L/usr/local/lib -lstrophe -lpthread

OBJS=disco.o uxmpp.o bot.o ping.o util.o
TGTS=uxmpp

CLEAN=$(TGTS) *.o *.core ktrace.out \
	xep-0115-ver.h xep-0115-ver-gen

all: $(TGTS)

disco.o: disco.c disco.h
	$(CC) $(_CFLAGS) -c -o $@ disco.c

uxmpp.o: uxmpp.c uxmpp.h
	$(CC) $(_CFLAGS) -c -o $@ uxmpp.c

bot.o: xep-0115-ver.h bot.c bot.h
	$(CC) $(_CFLAGS) -c -o $@ bot.c

ping.o: ping.c ping.h
	$(CC) $(_CFLAGS) -c -o $@ ping.c

util.o: util.c util.h
	$(CC) $(_CFLAGS) -c -o $@ util.c

xep-0115-ver.h: xep-0115-ver-gen
	@./xep-0115-ver-gen

xep-0115-ver-gen: uxmpp.h xep-0115-ver-gen.c
	$(CC) $(_CFLAGS) -o $@ xep-0115-ver-gen.c -lcrypto

uxmpp: $(OBJS) main.c
	$(CC) $(_CFLAGS) -o $@ $(OBJS) main.c $(LDFLAGS)

.PHONY: install
install: all
	install -D -m 0755 uxmpp $(DESTDIR)$(PREFIX)/bin/uxmpp
	install -D -m 0644 uxmpp.1 $(DESTDIR)$(MANPREFIX)/man/man1/uxmpp.1

.PHONY: test
test:
	make CFLAGS=-g
	MALLOC_OPTIONS=DCV ktrace -tu ./uxmpp -dh xmpp.sinelabs.ca disco
	kdump -u malloc -f ./ktrace.out

.PHONY: clean
clean:
	rm -f $(CLEAN)

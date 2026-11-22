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

CC=cc

PREFIX?=/usr
MANPREFIX?=/usr/share/man
DEBUG?=

all:
	@make -C src

.PHONY: clean install lmemtest test
clean:
	@make -C src $@
	rm -f valgrind-*.log
	rm -f vgcore.*
	rm -f *.core
	rm -f ktrace.out

install:
	@make PREFIX=$(PREFIX) MANPREFIX=$(MANPREFIX) -C src $@

lmemtest: clean
	@make DEBUG="-g -DDEBUG" -C src
	valgrind --show-leak-kinds=all \
		--leak-check=full \
		--track-origins=yes \
		--log-file=valgrind-bot.log \
		src/uxmpp \
		-t sinelabs@conference.xmpp.sinelabs.ca \
		-mn abott bot
# 	valgrind --show-leak-kinds=all \
# 		--leak-check=full \
# 		--track-origins=yes \
# 		--log-file=valgrind-disco.log \
# 		src/uxmpp \
# 		-t conference.xmpp.sinelabs.ca \
# 		disco

bmemtest: clean
	@make DEBUG="-g -DDEBUG" -C src
	MALLOC_OPTIONS=DCV ktrace -tu src/uxmpp \
		-t sinelabs@conference.xmpp.sinelabs.ca \
		-mn abott bot
# 	kdump -u malloc -f ./ktrace.out

test:
	@make -C src $@

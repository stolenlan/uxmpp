#!/bin/sh
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

# Example shell-bot
# NOTE: Messages coming from a given MUC are framed with SOT, and EOT
# characters, this implementation uses hexdump identify a given "post"
# The first line is the FROM room-JID, second is a unix timestamp, all
# remaining lines up to the ETX character are the message.


MUC_DOMAIN=conference.xmpp.sinelabs.ca
MUC_NODE=sinelabs
OUT_PATH=${HOME}/xmpp
BOT_NICK=abott

# Connect and start
mkdir -p ${OUT_PATH}
uxmpp -t ${MUC_NODE}@${MUC_DOMAIN} -n ${BOT_NICK} \
	-f ${OUT_PATH} bot &
UXMPP_PID=$!

# function xmpp_shutdown {
# 	trap - 15
# 	kill -2 ${UXMPP_PID}
# }

# trap xmpp_shutdown 15

function handle_message {
	BODY=$(echo "${1}" | sed 's/\x02//g;s/\x03//g')
	FROM=$(echo "${2}" | sed 's/\x02//g;s/\x03//g')
	DEST_FIFO="${3}"
	NICK=$(echo "${FROM}" | grep -oE "\/.*$" | sed -E 's/^\///g')
	

	if [ ! -p "$DEST_FIFO" ]; then
		echo "ERROR: $DEST_FIFO is not a pipe"
		return 1
	fi

	# These two examples, mirror the defaults of the bot
	echo "${BODY}" | grep -qE "^[pP][iI][nN][gG]$" && {
		echo "${NICK}: PONG"
		echo -e "${NICK}: PONG\nI hope your test was successful" \
			> $DEST_FIFO
	}

	echo "${BODY}" | grep -q "^${BOT_NICK}[:,]" && {
		echo -e "${NICK}: I am just a dumb bot.\nNo LLMs here, I do a few things, and do those things well.\nChatting however is not one of them." \
			> $DEST_FIFO
	}

	echo -e "${FROM}:|${BODY}|"
}


echo "$UXMPP_PID" > ${OUT_PATH}/pid

STX=0
tail -n0 -f ${OUT_PATH}/${MUC_DOMAIN}/out-${MUC_NODE} | \
while read -r MESSAGE; do

	NSTART_SEQ=$(echo "$MESSAGE" | hexdump -e '"%04x"' | grep -c '^0a02$')
	N_STOP_SEQ=$(echo "$MESSAGE" | hexdump -e '"%04x"' | grep -c '^0a03$')

	# The MESSAGE is the stop sequence
	if [ $N_STOP_SEQ -gt 0 ] && [ "$FROM" != "" ]; then
		handle_message \
			"${MSG}${MESSAGE}" \
			"${FROM}" \
			"${OUT_PATH}/${MUC_DOMAIN}/in-${MUC_NODE}"
		STX=0
		FROM=""
		MSG=""
		continue;
	fi

	# The MESSAGE is the start sequence, set ST(art inde)X to 1
	if [ $NSTART_SEQ -gt 0 ]; then
		STX=1
		FROM=""
		MSG=""
		continue
	fi

	# MESSAGE is not the start, or stop sequences.

	# If start index (STX) is 1 MESSAGE is the from line 
	if [ $STX -eq 1 ]; then
		FROM="$MESSAGE"
		STX=2
		continue
	fi

	# If the start index is 2 the MESSAGE is the first body line.
	if [ $STX -eq 2 ]; then
		MSG="${MESSAGE}"
		STX=3
		continue
	fi

	if [ $STX -eq 3 ]; then
		MSG="${MSG}${MESSAGE}"
		continue
	fi
done

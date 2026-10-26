#!/bin/sh

START_KEY="$(openssl rand -hex 16)"
NIFS=$IFS


function handle_message {
	IN_MESSAGE="${1}"
	IN_ROOM_FROM="${2}"

	NLINES=$(echo "$IN_MESSAGE" | wc -l | sed -E 's/ //g')
	FROM=$(echo "${IN_MESSAGE}" | head -n 1)
	BODY=$(echo "${IN_MESSAGE}" | tail -n $(( $NLINES -1 )))
	NICK=$(echo "${FROM}" | grep -oE "\/.*$" | sed -E 's/^\///g')
	
	DEST_FIFO=$(echo "${2}" | sed -E 's/^from\-/to\-/g')

	if [ ! -p "$DEST_FIFO" ]; then
		echo "ERROR: $DEST_FIFO is not a pipe"
		return 1
	fi

	# These two examples, mirror the defaults of the bot
	echo "${BODY}" | grep -qE "^ping$" && {
		echo "${NICK}: PONG"
		echo -e "${NICK}: PONG\nI hope your test was successful" \
			> $DEST_FIFO
	}

	echo "${BODY}" | grep -qE "^Abott[:,]" && {
		echo -e "${NICK}: I am just a dumb bot.\nNo LLMs here, I do a few things, and do those things well.\nChatting however is not one of them." \
			> $DEST_FIFO
	}
}


#src/uxmpp -m sinelabs@conference.xmpp.sinelabs.ca -n Abott -df mucbot

# Iterate over all connected rooms
while [ true ]; do
	for ROOM_FROM in $(ls from-* 2> /dev/null); do
		if [ -p $ROOM_FROM ]; then
			IFS="\01"
			for MESSAGE in $(cat $ROOM_FROM); do
				echo "$MESSAGE"
				handle_message "${MESSAGE}" "${ROOM_FROM}"
			done
			IFS=$NIFS
		else
			echo "ERROR: $ROOM_FROM IS NOT A PIPE!"
			sleep 2;
		fi
	done
done

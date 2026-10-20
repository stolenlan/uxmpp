#!/bin/sh

START_KEY="$(openssl rand -hex 16)"

function handle_muc_message {
	# Do something before taking message input
	MESSAGE=$(cat -)
	NICK=$(echo "$MESSAGE" | grep -oE "^.*\/.*: ")
	# For testing just echo back the entire string
	# echo "$MESSAGE" | sed -E 's/^.*\///g' | tee -a to-${1}
	echo "$MESSAGE"
	# Maybe after?
}

# Iterate over all connected rooms
while [ true ]; do
	for ROOM_FROM in $(ls from-*); do
		if [ -p $ROOM_FROM ]; then
			cat $ROOM_FROM \
				| tee -a log.txt \
				| handle_muc_message "$(echo $ROOM_FROM \
					| sed -E 's/^from-//g')"
		else
			echo "ERROR: $ROOM_FROM IS NOT A PIPE!"
			sleep 2;
		fi
	done
done

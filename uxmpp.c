#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <termios.h>

#include "uxmpp.h"

int
get_jid_credentials(char *jid, char *password)
{
	char password_buf[PWLEN_MAX];

	if (jid == NULL)
		jid = getenv("XMPP_JID");

	if (jid == NULL) {
		fprintf(stderr, "Missing JID: "
			"use -j or set the XMPP_JID "
			"environment variable.\n");
		return 0;
	}

	memset(password_buf, '\0', PWLEN_MAX);

	if (password == NULL)
		password = getenv("XMPP_PASSWORD");

	if (password == NULL)
		if (pw_prompt("Password: ", password_buf))
			password = password_buf;

	if (jid == NULL || password == NULL) {
		fprintf(stderr, "Invalid or missing credentials.\n");
		return 0;
	}

	return 1;
}

int
pw_prompt(const char *prompt, char in_password[PWLEN_MAX])
{
	char *in_password_p, *buf_p, buf[PWLEN_MAX];
	struct termios oldt, newt;
	if (tcgetattr(STDIN_FILENO, &oldt) == -1)
		return 0;
	newt = oldt;
	newt.c_lflag &= ~(ICANON | ECHO);
	newt.c_cc[VMIN] = 1;
	newt.c_cc[VTIME] = 0;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == -1) 
		return 0;

	memset(buf, '\0', PWLEN_MAX);
	printf("%s", prompt);
	fgets(buf, PWLEN_MAX, stdin);
	buf_p = buf;

	memset(in_password, '\0', PWLEN_MAX);
	in_password_p = in_password;
	
	for (char c = *buf_p; c && c != '\n'; c  = *(++buf_p))
		*in_password_p++ = c;
	*in_password_p = '\0';

	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
	putchar('\n');
	return in_password_p-in_password;
}

#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>

#include "uxmpp.h"

int
check_outpath(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		return 0;
	}

	if (S_ISDIR(st.st_mode) 
		&& (st.st_mode&S_IRUSR)
		&& (st.st_mode&S_IWUSR)
		&& (st.st_mode&S_IXUSR))
		return 1;

	return 0;
}

int
get_jid_credentials(char *jid, char *password)
{
	if (!jid || !password)
		return 0;

	if (!strlen(jid)) {
		const char *env_jid = getenv("XMPP_JID");
		if (env_jid)
			snprintf(jid, REGSTR_MAX, "%s", env_jid);
	}

	if (!strlen(jid)) {
		fprintf(stderr, "Missing JID: use -j or set the XMPP_JID "
			"environment variable.\n");
		return 0;
	}

	if (!strlen(password)) {
		const char *env_password = getenv("XMPP_PASSWORD");
		if (env_password)
			snprintf(password, REGSTR_MAX, "%s", env_password);
	}

	if (!strlen(password))
		if (!pw_prompt("Password: ", password))
			return 0;

	if (!strlen(jid)) {
		fprintf(stderr, "Invalid or missing credentials.\n");
		return 0;
	}

	return 1;
}

int
pw_prompt(const char *prompt, char in_password[REGSTR_MAX])
{
	char *in_password_p, *buf_p, buf[REGSTR_MAX];
	struct termios oldt, newt;
	if (tcgetattr(STDIN_FILENO, &oldt) == -1) {
		fprintf(stderr,
			"ERROR: failed to setup the password prompt.\n");
		return 0;
	}

	newt = oldt;
	newt.c_lflag &= ~ECHO;

	if (tcsetattr(STDIN_FILENO, TCSANOW, &newt) == -1) 
		return 0;

	memset(buf, '\0', REGSTR_MAX);

	printf("%s", prompt);
	
	fgets(buf, REGSTR_MAX-1, stdin);
	
	tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

	buf_p = buf;

	memset(in_password, '\0', REGSTR_MAX);
	in_password_p = in_password;
	
	for (char c = *buf_p; c && c != '\n'; c  = *(++buf_p))
		*in_password_p++ = c;
	*in_password_p = '\0';
	putchar('\n');

	return 1;
}

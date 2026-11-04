#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <termios.h>

#include <strophe.h>

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
get_fortune(char *fortune_out, int max) {
	int pfd[2];
	if(pipe(pfd) == -1) {
		printf("Error creating pipe\n");
		return 0;
	}

	int pid = fork();
	if(pid == -1) {
		fprintf(stderr, "Error while forking around\n");
		return 0;
	}

	if(pid == 0) {
		close(pfd[0]);
		char *args[] = {
			"fortune", 
			"-l",
			NULL
		};

		dup2(pfd[1], STDOUT_FILENO);
		execve("/usr/games/fortune", args, NULL);

		close(pfd[1]);
		return 1;
	}
	
	close(pfd[1]);
	char buf[max];
	memset(buf, '\0', max);
	read(pfd[0], buf, max-1);
	close(pfd[0]);
	int status;
	waitpid(pid, &status, 0);

	snprintf(fortune_out, max, "%s", buf);
	
	return status;
}

int
get_jid_credentials(char *jid, char *password) {
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

int  jid_get_local(char out[REGSTR_MAX], const char *jid) {
	xmpp_ctx_t *ctx = xmpp_ctx_new(NULL, NULL);
	const char *node = xmpp_jid_node(ctx, jid);
	snprintf(out, REGSTR_MAX, "%s", node);
	xmpp_free(ctx, (void *)node);
	xmpp_ctx_free(ctx);

	return (int)strlen(out);
}

int
pw_prompt(const char *prompt, char in_password[REGSTR_MAX]) {
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

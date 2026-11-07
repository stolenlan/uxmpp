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

	static const char *fpaths[] = {
		"/usr/games/fortune",
		"/usr/bin/fortune",
		"/usr/local/bin/fortune",
		NULL
	};
	
	const char *fpath = fpaths[0];
	for (int i = 1; i < 5 && fpath; fpath = fpaths[i++])
		if (access(fpath, X_OK) == 0) break;

	if (!fpath) {
		fprintf(stderr, "fortune not found\n");
		return 1;
	}

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

	char opts[16];
	memset(opts, '\0', 16);
	if (f_adult)
		snprintf(opts, 16, "-o");
	else
		snprintf(opts, 16, "-l");

	if(pid == 0) {
		close(pfd[0]);
		char *args[] = {
			"fortune",
			opts,
			NULL
		};

		dup2(pfd[1], STDOUT_FILENO);
		execve(fpath, args, NULL);

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
parse_version_stanza(char *out, int len, const char *stanza_text) {
	char *name = NULL,*vers = NULL,*os = NULL;

	xmpp_ctx_t *ctx = xmpp_ctx_new(NULL, NULL);
	xmpp_stanza_t *stanza = xmpp_stanza_new_from_string(ctx, stanza_text);

	xmpp_stanza_t *query = xmpp_stanza_get_child_by_ns(stanza,
		"jabber:iq:version");
	if (!query)
		return 1;

	xmpp_stanza_t *sname = xmpp_stanza_get_child_by_name(query, "name");
	xmpp_stanza_t *svers = xmpp_stanza_get_child_by_name(query, "version");
	xmpp_stanza_t *sos   = xmpp_stanza_get_child_by_name(query, "os");

	if (sname)
		name = xmpp_stanza_get_text(sname);
	if (svers)
		vers = xmpp_stanza_get_text(svers);
	if (sos)
		os = xmpp_stanza_get_text(sos);

	if (os)
		snprintf(out, len, "%s-%s (%s)", name, vers, os);
	else
		snprintf(out, len, "%s-%s", name, vers);

	xmpp_free(ctx, name);
	xmpp_free(ctx, vers);
	xmpp_free(ctx, os);

	xmpp_stanza_release(stanza);
	xmpp_ctx_free(ctx);

	return 0;
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

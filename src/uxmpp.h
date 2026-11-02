#ifndef _UXMPP_H
#define _UXMPP_H 1

#define VER_MAJ	"0"
#define VER_MIN	"0"
#define VER_PNT	"8"
#define VERSION	VER_MAJ "." VER_MIN "." VER_PNT

#define REGSTR_MAX	256

enum err_code_t {
	RETURN_SUCCESS = 0,
	FAIL_GENERIC,
	FAIL_FIFO,
	FAIL_FIFO_JID,
	FAIL_FIFO_PATH
};

#define USAGE_STR \
"Usage: %s [-d] [-f] [-i in_fifo] [-j user_jid] [-m] [-n nickname]\n"\
"       [-o out_fifo] [-p password] [-t target_jid] [-v] command\n"

extern int f_debug;
extern int f_mdown;
extern int f_usecs;

int		check_outpath(const char *path);
int		get_fortune(char *fortune_out, int max);
int		get_jid_credentials(char *jid, char *password);
int 		jid_get_local(char out[REGSTR_MAX], const char *jid);
int		pw_prompt(const char *prompt, char in_password[256]);

#endif

#ifndef _UXMPP_H
#define _UXMPP_H 1

#define VER_MAJ		0
#define VER_MIN		0
#define VER_PNT		2

#define REGSTR_MAX   256
#define HSTLEN_MAX   1024

#define USAGE_STR    \
"Usage: %s [-d] [-h bare_xmpp_host] [-p password] "\
"[-u user_jid] [-v] command\n"

extern int f_debug;

int 		check_outpath(const char *path);
int 		get_jid_credentials(char *jid, char *password);
int 		pw_prompt(const char *prompt, char in_password[256]);

#endif

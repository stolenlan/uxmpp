#ifndef _UXMPP_H
#define _UXMPP_H 1

#define VERSION "0.0.1"
#define PWLEN_MAX 256
#define USAGE_STR "Usage: %s [-dvjh] command_mode\n"

int 		get_jid_credentials(char *jid, char *password);
int 		pw_prompt(const char *prompt, char in_password[256]);

#endif

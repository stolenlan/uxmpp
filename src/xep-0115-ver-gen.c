#include <stdio.h>
#include <openssl/evp.h>
#include <string.h>

/* Version info */
#include "uxmpp.h"

/**
 * TODO: Parse the actual disco#info, and generate this 
 * Currently, any changes in the disco response will require changes
 * 
 * The resulting program is used during the build process only, if it
 * fails to produce the target header file the program must return > 0 so
 * that the build process may be halted by make.
 * 
 * NOTE: -lcrypto is only required for building, the uxmpp binary uses
 * none of it's routines.
 * 
 **/
#define XEP_0115_S \
"client/pc/en/uxmpp " VERSION "<"\
"http://jabber.org/protocol/caps<"\
"http://jabber.org/protocol/disco#info<"\
"http://jabber.org/protocol/disco#items<"\
"http://jabber.org/protocol/muc<"\
"urn:xmpp:ping<"\
"urn:xmpp:time<"

#define HDR_TPL \
"#ifndef _XEP115VER_H\n"\
"#define _XEP115VER_H 1\n"\
"\n"\
"#define XEP_0115_STR \"%s\"\n"\
"#define XEP_0115_VER \"%s\"\n"\
"\n"\
"#endif\n"


static int
sha1(const char *input, const char *filename)
{
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digest_len = 0;

	char base64[4 * ((EVP_MAX_MD_SIZE +2) / 3) +1];

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	if (!ctx)
		return 1;

	if (EVP_DigestInit_ex(ctx, EVP_sha1(), NULL) != 1 ||
		EVP_DigestUpdate(ctx, input, strlen(input)) != 1 ||
		EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
		EVP_MD_CTX_free(ctx);
		return 1;
	}
	EVP_MD_CTX_free(ctx);

	int encoded_len = EVP_EncodeBlock(
		(unsigned char *)base64, digest, (int)digest_len);
	if (encoded_len < 0)
		return 1;

	FILE *fp = fopen(filename, "w");
	if (!fp) {
		fprintf(stderr, "Unable to open %s: ", filename);
		perror(NULL);
		return 1;
	}
	size_t nbw = (size_t)fprintf(fp, HDR_TPL, XEP_0115_S, base64);
	fclose(fp);

	if (nbw <= 0) {
		perror("Error writing header file...\n");
		return 1;
	}

	return 0;
}


int main(void) {

	const char *filename = "xep-0115-ver.h";

	FILE *fp = fopen("/tmp/uxmpp-xep-0115-ver.txt", "w");
	if (!fp) {
		fprintf(stderr, "Unable to open %s: ", filename);
		perror(NULL);
		return 1;
	}
	fprintf(fp, XEP_0115_S);
	fclose(fp);

	return sha1(XEP_0115_S, filename);

}

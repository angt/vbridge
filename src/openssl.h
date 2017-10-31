#pragma once

#include "common.h"
#include "base64.h"

#include <openssl/ssl.h>

void   openssl_init          (void);
void   openssl_exit          ();
void   openssl_print_error   (const char *);
char  *openssl_new_rsa       (int, int);
void   openssl_use_rsa       (char *);
void   openssl_use_dh        (void);
void   openssl_use_ecdh      (const char *);
void   openssl_use_ciphers   (const char *);
char  *openssl_get_cert      (void);
char **openssl_get_certs     (void);
void   openssl_set_certs     (char **);
void   openssl_add_cert      (char *);
void   openssl_delete_cert   (char *);
SSL   *openssl_create        (int, int);
void   openssl_delete        (SSL *);
int    openssl_verify        (SSL *, int);

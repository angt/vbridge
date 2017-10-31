#pragma once

#include "common.h"

#include <gssapi/gssapi_krb5.h>

typedef struct auth_gss auth_gss_t;

struct auth_gss {
    OM_uint32 major;
    OM_uint32 minor;
    gss_ctx_id_t context;
    gss_name_t name;
    gss_OID oid;
    gss_cred_id_t cred;
    OM_uint32 services;
    char *cred_file;
};

int   auth_gss_create      (auth_gss_t *, const char *, buffer_t *, buffer_t *);
void  auth_gss_delete      (auth_gss_t *);
void  auth_gss_delegate    (void);
void  auth_gss_print_error (auth_gss_t *, const char *);

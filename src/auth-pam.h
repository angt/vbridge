#pragma once

#include "common.h"
#include "user.h"

#include <security/pam_appl.h>

typedef struct auth_pam auth_pam_t;

struct auth_pam {
    buffer_t name;
    buffer_t pass;
    pam_handle_t *handle;
    int err;
};

int   auth_pam_ready       (void);
char *auth_pam_create      (auth_pam_t *, int);
void  auth_pam_delete      (auth_pam_t *);
void  auth_pam_print_error (auth_pam_t *, const char *);

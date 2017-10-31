#pragma once

#include "auth-gss.h"
#include "user.h"

char *auth_gss_get_name    (auth_gss_t *);
int   auth_gss_get_level   (auth_gss_t *, const char *);

#pragma once

#include "common.h"

typedef struct acl acl_t;

struct acl {
    char *name;
    int level;
    acl_t *next;
};

void acl_put (acl_t **, const char *, int);
int  acl_get (acl_t **, const char *);

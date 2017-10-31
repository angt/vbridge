#pragma once

#include "common.h"

void  terminal_set_tty   (const char *);
char *terminal_get       (const char *, int);
char *terminal_get_min   (const char *, int, size_t);
int   terminal_get_yesno (const char *, int);

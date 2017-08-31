#pragma once

#include "common.h"

void  user_init    (void);
void  user_change  (char *);
void  user_restore (void);
int   user_locked  (void);
void  user_lock    (void);
char *user_name    (void);

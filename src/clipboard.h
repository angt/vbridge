#pragma once

#include "display.h"

void     clipboard_init  (Window);
int      clipboard_event (XEvent *);
uint8_t *clipboard_get   (void);
void     clipboard_set   (uint8_t *);

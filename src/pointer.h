#pragma once

#include "display.h"

int  pointer_event    (XEvent *);
int  pointer_is_warp  (void);
void pointer_warp     (Window, int, int);

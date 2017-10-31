#pragma once

#include "display.h"

void xrandr_init   (void);
int  xrandr_event  (XEvent *);
int  xrandr_resize (int, int);

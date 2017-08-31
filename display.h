#pragma once

#include "common.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xfixes.h>

struct display_extern {
    Display *id;
    volatile int error;
    Visual *visual;
    int depth;
    Window root;
    int xfixes_event; // XXX
};

extern struct display_extern display;

int  display_init (void);
void display_exit ();

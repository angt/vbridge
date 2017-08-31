#pragma once

#include "display.h"

void input_init         (void);
void input_exit         ();
int  input_event        (XEvent *);
void input_release      (void);
int  input_need_release (void);
void input_pointer      (int, int, int);
void input_button       (uint8_t, int);
void input_key          (uint8_t, KeySym, int);

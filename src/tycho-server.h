#pragma once

#include "tycho.h"

void tycho_setup_server  (tycho_t *);
int  tycho_send          (tycho_t *, buffer_t *);
int  tycho_set_image     (image_info_t *);
void tycho_set_quality   (unsigned, unsigned);

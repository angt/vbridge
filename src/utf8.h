#pragma once

#include "common.h"

size_t utf8_to_utf32   (uint32_t *, const char *);
size_t utf8_from_utf32 (char *, uint32_t);

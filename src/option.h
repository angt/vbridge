#pragma once

#include "common.h"

enum option_type {
    opt_flag = 0,
    opt_bool,
    opt_str,
    opt_int,
    opt_real,
    opt_host,
    opt_port,
    opt_file,
    opt_name,
    opt_list,
};

typedef enum option_type option_type_t;

void option     (option_type_t, void *, const char *, const char *);
void option_run (int, char **);

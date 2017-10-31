#pragma once

#include "openssl.h"
#include "storage.h"

void  auth_ssl_setup_rsa (int, int);
void  auth_ssl_load      (int);
void  auth_ssl_save      (int);

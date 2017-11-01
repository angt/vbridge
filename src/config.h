#pragma once

#define PROG_NAME     "VBridge"
#define PROG_SERVICE  "vbridge"

#ifndef PROG_VERSION
#define PROG_VERSION  "(unversioned)"
#endif

#define CONFIG_PORT           "5000"

#define CONFIG_SSL_CIPHERS    "AES128"
#define CONFIG_SSL_RSA_LEN     1024
#define CONFIG_SSL_RSA_EXP     65537
#define CONFIG_SSL_DH_LEN      512
#define CONFIG_SSL_DH_GEN      2
#define CONFIG_SSL_ECDH_CURVE "prime256v1"

#define CONFIG_MASTER_TIMEOUT  200
#define CONFIG_GRAB_TIMEOUT    30

#define CONFIG_BUFFER_SIZE     32*1024

#define CONFIG_QUALITY_MIN     3
#define CONFIG_QUALITY_MAX     5

#define CONFIG_KEY_PRIVATE    "key"
#define CONFIG_KEY_ACCEPT     "accept"
#define CONFIG_KEY_CONNECT    "connect"

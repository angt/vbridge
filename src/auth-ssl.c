#include "auth-ssl.h"

void
auth_ssl_setup_rsa(int len, int exp)
{
    char **str = storage_load(CONFIG_KEY_PRIVATE);

    if (str) {
        openssl_use_rsa(str[0]);
        safe_free_strs(str);
        return;
    }

    char *rsa = openssl_new_rsa(len, exp);

    if (!rsa)
        return;

    openssl_use_rsa(rsa);

    char *tmp[] = {rsa, NULL};
    storage_save(CONFIG_KEY_PRIVATE, tmp);
    safe_free(rsa);
}

void
auth_ssl_save(int server)
{
    char **str = openssl_get_certs();
    storage_save(server ? CONFIG_KEY_ACCEPT : CONFIG_KEY_CONNECT, str);
    safe_free_strs(str);
}

void
auth_ssl_load(int server)
{
    char **str = storage_load(server ? CONFIG_KEY_ACCEPT : CONFIG_KEY_CONNECT);
    openssl_set_certs(str);
    safe_free_strs(str);
}

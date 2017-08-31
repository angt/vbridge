#include "openssl.h"
#include "buffer-static.h"
#include "common-static.h"

#include <openssl/conf.h>
#include <openssl/engine.h>

typedef struct openssl_data openssl_data_t;

struct openssl_data {
    int state;
    int verify;
    int error;
};

static struct openssl_global {
    SSL_CTX *ctx;
    X509 *cert;
    int index;
} global;

static int
gen_cb(_unused_ int p, _unused_ int n, _unused_ BN_GENCB *cb)
{
    return running;
}

static int
verify_cb(int ok, X509_STORE_CTX *store)
{
    if (!store)
        return 0;

    SSL *ssl = X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx());

    if (!ssl)
        return 0;

    openssl_data_t *data = SSL_get_ex_data(ssl, global.index);

    if (!data)
        return 0;

    if (!data->state)
        data->verify = ok;

    data->error = X509_STORE_CTX_get_error(store);

    switch (data->error) {
    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
        data->state = 1;
        return 1;
    }

    return ok;
}

static char *
rsa_to_str(RSA *rsa)
{
    if (!rsa)
        return NULL;

    const int len = i2d_RSAPrivateKey(rsa, NULL);

    if (len <= 0)
        return NULL;

    buffer_t buf, b64;

    buffer_setup(&buf, NULL, len);

    if (i2d_RSAPrivateKey(rsa, &buf.write) != len) {
        safe_free(buf.data);
        return NULL;
    }

    buffer_setup(&b64, NULL, 2 * len + 1);
    base64_encode(&b64, &buf);
    buffer_write(&b64, 0);

    safe_free(buf.data);

    return (char *)b64.data;
}

static RSA *
str_to_rsa(char *str)
{
    const size_t len = str_len(str);

    if (!len)
        return NULL;

    buffer_t buf, b64;

    buffer_setup(&b64, str, len);
    b64.write += len;

    buffer_setup(&buf, NULL, len);
    base64_decode(&buf, &b64);

    RSA *rsa = d2i_RSAPrivateKey(NULL, (const uint8_t **)&buf.read,
                                 buffer_read_size(&buf));

    safe_free(buf.data);

    return rsa;
}

static char *
cert_to_str(X509 *cert)
{
    if (!cert)
        return NULL;

    const int len = i2d_X509(cert, NULL);

    if (len <= 0)
        return NULL;

    buffer_t buf, b64;

    buffer_setup(&buf, NULL, len);

    if (i2d_X509(cert, &buf.write) != len) {
        safe_free(buf.data);
        return NULL;
    }

    buffer_setup(&b64, NULL, 2 * len + 1);
    base64_encode(&b64, &buf);
    buffer_write(&b64, 0);

    safe_free(buf.data);

    return (char *)b64.data;
}

static X509 *
str_to_cert(char *str)
{
    const size_t len = str_len(str);

    if (!len)
        return NULL;

    buffer_t buf, b64;

    buffer_setup(&b64, str, len);
    b64.write += len;

    buffer_setup(&buf, NULL, len);
    base64_decode(&buf, &b64);

    X509 *cert = d2i_X509(NULL, (const uint8_t **)&buf.read,
                          buffer_read_size(&buf));

    safe_free(buf.data);

    return cert;
}

static void
create_cert(EVP_PKEY *pkey)
{
    if (!global.ctx || !pkey)
        return;

    X509 *cert = X509_new();

    if (!cert)
        return;

    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
    ASN1_TIME_set_string(X509_get_notBefore(cert), "110111000000Z");
    ASN1_TIME_set_string(X509_get_notAfter(cert), "210111000000Z");
    X509_set_pubkey(cert, pkey);

    unsigned int n = 0;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned char md64[2 * EVP_MAX_MD_SIZE];

    X509_digest(cert, EVP_sha1(), md, &n);

    buffer_t buf;
    buffer_setup(&buf, md, sizeof(md));
    buf.write += n;

    buffer_t b64;
    buffer_setup(&b64, md64, sizeof(md64));
    base64_encode(&b64, &buf);

    if (buffer_read_size(&b64) > 64)
        b64.write = b64.data + 64;

    b64.write[0] = '\0';

    X509_NAME *name = X509_get_subject_name(cert);

    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC,
                               (uint8_t *)"FR", -1, -1, 0);

    X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                               (uint8_t *)PROG_NAME, -1, -1, 0);

    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               b64.data, -1, -1, 0);

    X509_set_issuer_name(cert, name);

    if (!X509_sign(cert, pkey, EVP_sha1())) {
        warning("couldn't generate x509 certificate\n");
        X509_free(cert);
        return;
    }

    SSL_CTX_use_certificate(global.ctx, cert);
    SSL_CTX_use_PrivateKey(global.ctx, pkey);

    global.cert = cert;
}

void
openssl_init(void)
{
    CRYPTO_malloc_init();

    OPENSSL_no_config();

    SSL_library_init();
    SSL_load_error_strings();

    OpenSSL_add_all_algorithms();

    ENGINE_load_builtin_engines();
    ENGINE_register_all_complete();

    if (!RAND_status())
        error("couldn't seed the prng\n");

    STACK_OF(SSL_COMP) *cm = SSL_COMP_get_compression_methods();
    sk_SSL_COMP_zero(cm);

    global.ctx = SSL_CTX_new(SSLv23_method());

    if (!global.ctx)
        error("couldn't create SSL context\n");

    SSL_CTX_set_session_cache_mode(global.ctx, SSL_SESS_CACHE_OFF);

    SSL_CTX_set_options(global.ctx, 0
            | SSL_OP_NO_SSLv2
            | SSL_OP_NO_SSLv3
            | SSL_OP_NO_TICKET
#ifdef SSL_OP_NO_COMPRESSION
            | SSL_OP_NO_COMPRESSION
#endif
            | SSL_OP_SINGLE_DH_USE
            | SSL_OP_SINGLE_ECDH_USE);

    SSL_CTX_set_mode(global.ctx, 0
            | SSL_MODE_ENABLE_PARTIAL_WRITE
            | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);

    SSL_CTX_set_verify(global.ctx, 0
            | SSL_VERIFY_PEER
            | SSL_VERIFY_CLIENT_ONCE,
                       verify_cb);

    SSL_CTX_set_verify_depth(global.ctx, 0);
    SSL_CTX_set_read_ahead(global.ctx, 1);

    global.index = SSL_get_ex_new_index(0, NULL, NULL, NULL, NULL);
}

char **
openssl_get_certs(void)
{
    if (!global.ctx)
        return NULL;

    X509_STORE *store = SSL_CTX_get_cert_store(global.ctx);

    if (!store)
        return NULL;

    int n = sk_X509_OBJECT_num(store->objs);

    if (!n)
        return NULL;

    char **certs = safe_calloc(n + 1, sizeof(char *));
    int k = 0;

    for (int i = 0; i < n; i++) {
        X509_OBJECT *obj = sk_X509_OBJECT_value(store->objs, i);

        if (!obj || obj->type != X509_LU_X509)
            continue;

        char *str = cert_to_str(obj->data.x509);

        if (str)
            certs[k++] = str;
    }

    return certs;
}

void
openssl_add_cert(char *str)
{
    if (!global.ctx)
        return;

    X509_STORE *store = SSL_CTX_get_cert_store(global.ctx);

    if (!store)
        return;

    X509 *cert = str_to_cert(str);

    if (!cert)
        return;

    X509_STORE_add_cert(store, cert);
}

void
openssl_delete_cert(char *str)
{
    if (!global.ctx)
        return;

    X509_STORE *store = SSL_CTX_get_cert_store(global.ctx);

    if (!store)
        return;

    X509 *cert = str_to_cert(str);

    if (!cert)
        return;

    X509_STORE *new_store = X509_STORE_new();

    if (new_store) {
        int n = sk_X509_OBJECT_num(store->objs);

        for (int i = 0; i < n; i++) {
            X509_OBJECT *obj = sk_X509_OBJECT_value(store->objs, i);

            if (obj && (obj->type == X509_LU_X509) && X509_cmp(obj->data.x509, cert))
                X509_STORE_add_cert(new_store, obj->data.x509);
        }

        SSL_CTX_set_cert_store(global.ctx, new_store);
    }

    X509_free(cert);
}

void
openssl_set_certs(char **certs)
{
    if (!global.ctx)
        return;

    X509_STORE *store = X509_STORE_new();

    if (!store)
        return;

    if (certs) {
        for (int i = 0; certs[i]; i++) {
            X509 *cert = str_to_cert(certs[i]);

            if (cert)
                X509_STORE_add_cert(store, cert);
        }
    }

    SSL_CTX_set_cert_store(global.ctx, store);
}

int
openssl_verify(SSL *ssl, int add)
{
    if (!ssl)
        return 0;

    openssl_data_t *data = SSL_get_ex_data(ssl, global.index);

    if (!data)
        return 0;

    if (data->verify || !add)
        return data->verify;

    X509_STORE *store = SSL_CTX_get_cert_store(global.ctx);

    if (!store)
        return 0;

    X509 *cert = SSL_get_peer_certificate(ssl);

    if (!cert)
        return 0;

    X509_STORE_add_cert(store, cert);

    return 1;
}

void
openssl_exit()
{
    if (global.ctx)
        SSL_CTX_free(global.ctx);

    OBJ_cleanup();
    EVP_cleanup();
    ENGINE_cleanup();

    byte_set(&global, 0, sizeof(global));
}

void
openssl_print_error(const char *str)
{
    if (!str)
        str = "SSL";

    unsigned long err;

    while (err = ERR_get_error(), err)
        warning("%s: %s\n", str, ERR_reason_error_string(err));
}

SSL *
openssl_create(int fd, int set_accept)
{
    if (!global.ctx || fd < 0)
        return NULL;

    SSL *ssl = SSL_new(global.ctx);

    if (!ssl)
        return NULL;

    SSL_set_fd(ssl, fd);

    if (set_accept) {
        SSL_set_accept_state(ssl);
    } else {
        SSL_set_connect_state(ssl);
    }

    openssl_data_t *data = safe_calloc(1, sizeof(openssl_data_t));
    SSL_set_ex_data(ssl, global.index, data);

    return ssl;
}

void
openssl_delete(SSL *ssl)
{
    if (!ssl)
        return;

    openssl_data_t *data = SSL_get_ex_data(ssl, global.index);
    SSL_set_ex_data(ssl, global.index, NULL);

    safe_free(data);
    SSL_free(ssl);
}

void
openssl_use_ciphers(const char *ciphers)
{
    if (!SSL_CTX_set_cipher_list(global.ctx, ciphers))
        error("couldn't set ciphers\n");
}

void
openssl_use_ecdh(const char *name)
{
    if (!global.ctx || str_empty(name))
        return;

    int nid = OBJ_sn2nid(name);

    if (!nid) {
        warning("couldn't find curve `%s'\n", name);
        return;
    }

    EC_KEY *ecdh = EC_KEY_new_by_curve_name(nid);

    if (!ecdh) {
        warning("couldn't load curve `%s'\n", name);
        return;
    }

    SSL_CTX_set_tmp_ecdh(global.ctx, ecdh);

    EC_KEY_free(ecdh);
}

void
openssl_use_dh(void)
{
    if (!global.ctx)
        return;

    DH *dh = DH_new();

    if (!dh)
        return;

    static const unsigned char rfc_2409_prime_1024[] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xC9, 0x0F, 0xDA, 0xA2,
        0x21, 0x68, 0xC2, 0x34, 0xC4, 0xC6, 0x62, 0x8B, 0x80, 0xDC, 0x1C, 0xD1,
        0x29, 0x02, 0x4E, 0x08, 0x8A, 0x67, 0xCC, 0x74, 0x02, 0x0B, 0xBE, 0xA6,
        0x3B, 0x13, 0x9B, 0x22, 0x51, 0x4A, 0x08, 0x79, 0x8E, 0x34, 0x04, 0xDD,
        0xEF, 0x95, 0x19, 0xB3, 0xCD, 0x3A, 0x43, 0x1B, 0x30, 0x2B, 0x0A, 0x6D,
        0xF2, 0x5F, 0x14, 0x37, 0x4F, 0xE1, 0x35, 0x6D, 0x6D, 0x51, 0xC2, 0x45,
        0xE4, 0x85, 0xB5, 0x76, 0x62, 0x5E, 0x7E, 0xC6, 0xF4, 0x4C, 0x42, 0xE9,
        0xA6, 0x37, 0xED, 0x6B, 0x0B, 0xFF, 0x5C, 0xB6, 0xF4, 0x06, 0xB7, 0xED,
        0xEE, 0x38, 0x6B, 0xFB, 0x5A, 0x89, 0x9F, 0xA5, 0xAE, 0x9F, 0x24, 0x11,
        0x7C, 0x4B, 0x1F, 0xE6, 0x49, 0x28, 0x66, 0x51, 0xEC, 0xE6, 0x53, 0x81,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };

    static const unsigned char generator[] = {2};

    dh->p = BN_bin2bn(rfc_2409_prime_1024, sizeof(rfc_2409_prime_1024), NULL);
    dh->g = BN_bin2bn(generator, sizeof(generator), NULL);

    SSL_CTX_set_tmp_dh(global.ctx, dh);

    DH_free(dh);
}

void
openssl_use_rsa(char *str)
{
    if (!global.ctx || str_empty(str))
        return;

    RSA *rsa = str_to_rsa(str);

    if (!rsa) {
        warning("couldn't use RSA key\n");
        return;
    }

    RSA_blinding_on(rsa, NULL);

    EVP_PKEY *pkey = EVP_PKEY_new();

    if (!pkey)
        return;

    EVP_PKEY_set1_RSA(pkey, rsa);

    create_cert(pkey);

    EVP_PKEY_free(pkey);
}

char *
openssl_new_rsa(int len, int exp)
{
    info("generate %i bits RSA key...\n", len);

    RSA *rsa = RSA_new();

    if (!rsa)
        return NULL;

    BIGNUM *bn = BN_new();

    if (!bn) {
        RSA_free(rsa);
        return NULL;
    }

    BN_set_word(bn, exp);

    BN_GENCB cb;
    BN_GENCB_set(&cb, gen_cb, NULL);

    char *str = NULL;

    if (RSA_generate_key_ex(rsa, len, bn, &cb))
        str = rsa_to_str(rsa);

    BN_free(bn);
    RSA_free(rsa);

    return str;
}

char *
openssl_get_cert(void)
{
    return cert_to_str(global.cert);
}

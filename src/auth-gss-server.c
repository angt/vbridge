#include "auth-gss-server.h"
#include "common-static.h"

char *
auth_gss_get_name(auth_gss_t *gss)
{
    gss_OID oid;
    gss_buffer_desc name = GSS_C_EMPTY_BUFFER;

    gss->major = gss_display_name(&gss->minor, gss->name, &name, &oid);

    char *ret = STR_MAKE(name.value);

    OM_uint32 minor;
    gss_release_buffer(&minor, &name);

    return ret;
}

int
auth_gss_get_level(auth_gss_t *gss, const char *name)
{
    if (str_empty(name))
        return 0;

    int level = 0;

    krb5_context ctx;

    if (krb5_init_context(&ctx))
        goto fail_init;

    krb5_principal princ;

    if (krb5_parse_name(ctx, name, &princ))
        goto fail_parse_name;

    level = 1 + !!krb5_kuserok(ctx, princ, user_name());

    if ((level == 2) && gss->cred) {
        char *ccname = STR_MAKE("FILE:/tmp/krb5cc_" PROG_SERVICE "_XXXXXX");
        char *filename = ccname + 5;

        umask(0177);

        int file = mkstemp(filename);

        if (file == -1) {
            warning("%s(%s): %m\n", "mkstemp", filename);
            safe_free(ccname);
            goto fail_cc_resolve;
        }

        safe_close(file);

        gss->cred_file = ccname;

        krb5_ccache ccache;

        if (krb5_cc_resolve(ctx, ccname, &ccache))
            goto fail_cc_resolve;

        if (krb5_cc_initialize(ctx, ccache, princ))
            goto fail_cc_initialize;

        gss->major = gss_krb5_copy_ccache(&gss->minor, gss->cred, ccache);

    fail_cc_initialize:
        krb5_cc_close(ctx, ccache);
    }

fail_cc_resolve:
    krb5_free_principal(ctx, princ);

fail_parse_name:
    krb5_free_context(ctx);

fail_init:
    return level;
}

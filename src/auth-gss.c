#include "auth-gss.h"
#include "buffer-static.h"

static struct auth_gss_global {
    gss_OID_desc oid;
    OM_uint32 services;
} global = {
    .oid = {9, "\x2a\x86\x48\x86\xf7\x12\x01\x02\x02"},
    .services = GSS_C_MUTUAL_FLAG | GSS_C_INTEG_FLAG | GSS_C_CONF_FLAG,
};

void
auth_gss_delegate(void)
{
    global.services |= GSS_C_DELEG_FLAG;
}

void
auth_gss_print_error(auth_gss_t *gss, const char *str)
{
    OM_uint32 type, code;
    OM_uint32 ctx = 0;

    if (!str)
        str = "GSS";

    if (gss->major == GSS_S_FAILURE) {
        type = GSS_C_MECH_CODE;
        code = gss->minor;
    } else {
        type = GSS_C_GSS_CODE;
        code = gss->major;
    }

    do {
        OM_uint32 minor;
        gss_buffer_desc buffer = GSS_C_EMPTY_BUFFER;

        gss_display_status(&minor, code, type, gss->oid, &ctx, &buffer);

        if (code)
            warning("%s: %s\n", str, buffer.value);

        gss_release_buffer(&minor, &buffer);
    } while (ctx);
}

void
auth_gss_delete(auth_gss_t *gss)
{
    OM_uint32 minor;

    if (gss->context)
        gss_delete_sec_context(&minor, &gss->context, GSS_C_NO_BUFFER); // XXX

    if (gss->name)
        gss_release_name(&minor, &gss->name);

    if (gss->cred)
        gss_release_cred(&minor, &gss->cred);

    if (gss->cred_file)
        safe_delete_file(gss->cred_file + 5);

    safe_free(gss->cred_file);

    byte_set(gss, 0, sizeof(auth_gss_t));
}

int
auth_gss_valid_oid(auth_gss_t *gss)
{
    if (!gss->oid)
        return 0;

    if (gss->oid->length != global.oid.length)
        return 0;

    uint8_t *have_oid = gss->oid->elements;
    uint8_t *want_oid = global.oid.elements;

    for (unsigned i = 0; i < gss->oid->length; i++)
        if (have_oid[i] != want_oid[i])
            return 0;

    return 1;
}

static void
auth_gss_setup_name(auth_gss_t *gss, const char *host)
{
    if (str_empty(host) || gss->name)
        return;

    static gss_OID_desc hostbased = {10, "\x2a\x86\x48\x86\xf7\x12\x01\x02\x01\x04"};

    gss_buffer_desc buffer;
    buffer.value = STR_MAKE(PROG_SERVICE, "@", host);
    buffer.length = str_len(buffer.value);

    gss->major = gss_import_name(&gss->minor, &buffer, &hostbased, &gss->name);

    safe_free(buffer.value);
}

int
auth_gss_create(auth_gss_t *gss, const char *host, buffer_t *send, buffer_t *recv)
{
    auth_gss_setup_name(gss, host);

    gss_buffer_desc _recv;
    _recv.length = buffer_read_size(recv);
    _recv.value = recv->read;

    gss_buffer_desc _send = GSS_C_EMPTY_BUFFER;

    if (host) {
        gss->major = gss_init_sec_context(&gss->minor, GSS_C_NO_CREDENTIAL,
                                          &gss->context, gss->name, &global.oid, global.services, 0,
                                          GSS_C_NO_CHANNEL_BINDINGS, &_recv, &gss->oid, &_send,
                                          &gss->services, NULL);
    } else {
        gss->major = gss_accept_sec_context(&gss->minor, &gss->context,
                                            GSS_C_NO_CREDENTIAL, &_recv, GSS_C_NO_CHANNEL_BINDINGS,
                                            &gss->name, &gss->oid, &_send, &gss->services, NULL,
                                            &gss->cred);
    }

    safe_free(recv->data);
    safe_free(send->data);

    byte_set(recv, 0, sizeof(buffer_t));
    byte_set(send, 0, sizeof(buffer_t));

    if (_send.length && _send.value) {
        buffer_setup(send, NULL, _send.length);
        buffer_write_data(send, _send.value, _send.length);
    }

    OM_uint32 minor;
    gss_release_buffer(&minor, &_send);

    if (GSS_ERROR(gss->major))
        return -1;

    if (gss->major != GSS_S_COMPLETE)
        return 0;

    if (!(auth_gss_valid_oid(gss)))
        return -1;

    if (!(gss->services & GSS_C_MUTUAL_FLAG))
        return -1;

    return 1;
}

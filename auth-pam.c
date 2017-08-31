#include "auth-pam.h"
#include "buffer-static.h"

static struct auth_pam_global {
    uint64_t time;
    uint64_t delay;
} global;

static int
auth_pam_setcred(pam_handle_t *handle, int flags)
{
    user_change(NULL);
    int pam_err = pam_setcred(handle, flags);
    user_restore();

    return pam_err;
}

static int
auth_pam_delay(int ret, unsigned usec, _unused_ void *ptr)
{
    global.time = time_now();
    global.delay = usec / 1000;
    return ret;
}

static int
auth_pam_conv(int num,
              const struct pam_message **msg,
              struct pam_response **rsp,
              void *ptr)
{
    if (num <= 0 || num > PAM_MAX_NUM_MSG || num > 255)
        return PAM_CONV_ERR;

    auth_pam_t *pam = ptr;

    struct pam_response *reply = safe_calloc(num, sizeof(struct pam_response));

    for (int i = 0; i < num; i++) {
        if (msg[i]->msg_style != PAM_PROMPT_ECHO_OFF)
            continue;
        reply[i].resp = (char *)pam->pass.data;
        byte_set(&pam->pass, 0, sizeof(buffer_t));
        break;
    }

    *rsp = reply;

    return PAM_SUCCESS;
}

static void
auth_pam_cleanup(auth_pam_t *pam)
{
    if (pam->pass.data)
        byte_set_safe(pam->pass.data, 0, buffer_size(&pam->pass));

    safe_free(pam->name.data);
    safe_free(pam->pass.data);

    byte_set(&pam->name, 0, sizeof(buffer_t));
    byte_set(&pam->pass, 0, sizeof(buffer_t));
}

void
auth_pam_delete(auth_pam_t *pam)
{
    auth_pam_cleanup(pam);

    if (!pam->handle)
        return;

    pam->err = auth_pam_setcred(pam->handle, PAM_DELETE_CRED);
    pam_end(pam->handle, pam->err);

    byte_set(pam, 0, sizeof(auth_pam_t));
}

int
auth_pam_ready(void)
{
    return time_dt(global.time, time_now()) >= global.delay;
}

void
auth_pam_print_error(auth_pam_t *pam, const char *str)
{
    if (!pam->handle)
        return;

    if (pam->err == PAM_SUCCESS)
        return;

    if (!str)
        str = "PAM";

    warning("%s: %s\n", str, pam_strerror(pam->handle, pam->err));
}

char *
auth_pam_create(auth_pam_t *pam, int reinit)
{
    struct pam_conv conv = {
        .conv = auth_pam_conv,
        .appdata_ptr = pam,
    };

    char *name = NULL;
    pam_handle_t *handle = NULL;

    pam->err = pam_start(PROG_SERVICE, (const char *)pam->name.data, &conv, &handle);

    pam->handle = handle;

    if (pam->err != PAM_SUCCESS)
        goto end;

    pam_set_item(handle, PAM_FAIL_DELAY, auth_pam_delay);
    pam_set_item(handle, PAM_TTY, PROG_SERVICE);

    user_change((char *)pam->name.data);
    pam->err = pam_authenticate(handle, 0);
    user_restore();

    if (pam->err != PAM_SUCCESS)
        goto end;

    user_change((char *)pam->name.data);
    pam->err = pam_acct_mgmt(handle, 0);
    user_restore();

    if (pam->err != PAM_SUCCESS)
        goto end;

    pam->err = auth_pam_setcred(handle, reinit ? PAM_REINITIALIZE_CRED : PAM_ESTABLISH_CRED);

    pam_get_item(handle, PAM_USER, (const void **)&name);

end:
    auth_pam_cleanup(pam);

    if (pam->err == PAM_SUCCESS)
        global.delay = 0;

    return STR_MAKE(name);
}

#include "user.h"
#include "common-static.h"

#include <pwd.h>
#include <sys/capability.h>
#include <sys/prctl.h>

static struct user_global {
    char *username;
    struct {
        uid_t uid;
    } lock, restore;
} global;

static void
user_set_cap(unsigned set)
{
    cap_value_t val[] = {CAP_NET_BIND_SERVICE, CAP_SETUID};

    cap_t cap = cap_get_proc();

    if (!cap)
        return;

    cap_clear(cap);

    cap_set_flag(cap, CAP_PERMITTED, COUNT(val), val, CAP_SET);
    cap_set_flag(cap, CAP_EFFECTIVE, CLAMP(set, 0, COUNT(val)), val, CAP_SET);

    cap_set_proc(cap);

    cap_free(cap);
}

static int
user_set_uid(uid_t r, uid_t e, uid_t s)
{
    int ret = setresuid(r, e, s);
    int err = errno;

    if (ret == -1 && err != EPERM)
        warning("setresuid(%i,%i,%i)==-1, errno=%i\n", r, e, s, err);

    errno = err;
    return ret;
}

static int
user_set_gid(gid_t r, gid_t e, gid_t s)
{
    int ret = setresgid(r, e, s);
    int err = errno;

    if (ret == -1 && err != EPERM)
        warning("setresgid(%i,%i,%i)==-1, errno=%i\n", r, e, s, err);

    errno = err;
    return ret;
}

void
user_init(void)
{
    struct passwd *pw;

    gid_t gid = getgid();
    uid_t uid = getuid();

    do {
        errno = 0;
        pw = getpwuid(uid);
    } while (!pw && errno == EINTR);

    if (pw) {
        global.username = STR_MAKE(pw->pw_name);
    } else if (errno) {
        warning("%s: %m\n", "getpwuid");
    }

    if (str_empty(global.username))
        error("unable to find a name for uid %i\n", uid);

    global.lock.uid = geteuid();

    if (!global.lock.uid) {
        global.lock.uid = uid;

        do {
            errno = 0;
            pw = getpwnam(PROG_SERVICE);
        } while (!pw && errno == EINTR);

        if (pw) {
            global.lock.uid = pw->pw_uid;
        } else if (errno) {
            warning("%s: %m\n", "getpwnam");
        }
    }

    if (!uid)
        warning("WARNING: you are running this service as root!\n");

    if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) == -1)
        warning("%s: %m\n", "prctl");

    user_set_gid(gid, gid, gid);

    user_set_cap(2);
    user_set_uid(uid, uid, global.lock.uid);
    user_set_cap(1);

    global.restore.uid = uid;
}

char *
user_name(void)
{
    return global.username;
}

void
user_change(char *username)
{
    uid_t uid = geteuid();
    global.restore.uid = getuid();

    if (username) {
        struct passwd *pw;

        do {
            errno = 0;
            pw = getpwnam(username);
        } while (!pw && errno == EINTR);

        if (pw) {
            uid = pw->pw_uid;
        } else if (errno) {
            warning("%s: %m\n", "getpwnam");
        }
    }

    user_set_cap(2);
    user_set_uid(uid, -1, -1);
    user_set_cap(1);
}

void
user_restore(void)
{
    user_set_uid(global.restore.uid, -1, -1);
}

int
user_locked(void)
{
    return getuid() != geteuid();
}

void
user_lock(void)
{
    user_set_uid(global.lock.uid, -1, -1);
}

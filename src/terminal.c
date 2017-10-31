#include "terminal.h"
#include "common-static.h"
#include "utf8-static.h"

#include <termios.h>

static struct terminal_global {
    const char *tty;
} global = {
    .tty = "/dev/tty",
};

static void
backspace(int fd, int count)
{
    while (count-- > 0)
        safe_write(fd, "\b \b", 3);
}

void
terminal_set_tty(const char *tty)
{
    global.tty = tty;
}

char *
terminal_get(const char *prompt, int hide)
{
    struct termios term_new, term_old;
    char *str = NULL;
    size_t len = 0;
    int pos = 0;

    int fd = safe_open(global.tty, O_RDWR | O_NOCTTY);

    if (fd < 0)
        fd = STDIN_FILENO;

    const int tty = isatty(fd);

    if (tty) {
        if (tcgetattr(fd, &term_old) < 0)
            goto fail_tty;

        term_new = term_old;
        term_new.c_lflag &= ~(ICANON | ECHO | ISIG);
        term_new.c_iflag |= IUTF8;
        term_new.c_cc[VMIN] = 1;
        term_new.c_cc[VTIME] = 0;

        if (tcsetattr(fd, TCSAFLUSH, &term_new) < 0)
            goto fail_tty;
    }

    if (tty)
        safe_write(fd, prompt, str_len(prompt));

    str = safe_calloc(1, 1);

    while (str) {
        size_t n = 1;
        char buf[8];

        if (safe_read(fd, buf, 1) != 1)
            break;

        if ((buf[0] == '\n') || (buf[0] == '\r'))
            break;

        size_t m = utf8_count(buf[0]);

        if (!m)
            break;

        if (--m) {
            if (safe_read(fd, &buf[1], m) != m)
                break;
            if (!utf8_check(buf, m))
                break;
            n += m;
        }

        if (((buf[0] >= ' ') && (buf[0] <= '~')) || (buf[0] < '\0')) {
            str = safe_realloc(str, len + n + 1);
            byte_copy(&str[len], buf, n);
            len += n;
            if (tty) {
                if (hide)
                    safe_write(fd, "*", 1);
                else
                    safe_write(fd, buf, n);
                pos++;
            }
            continue;
        }

        if (!tty)
            continue;

        switch (buf[0]) {
        case CTRL('C'):
            str = safe_free(str);
            break;
        case CTRL('U'):
            if (pos > 0) {
                backspace(fd, pos);
                pos = 0;
            }
            len = 0;
            break;
        case CTRL('H'):
        case CERASE:
            if (pos > 0) {
                backspace(fd, 1);
                pos--;
            }
            len = utf8_prev(str, len);
            break;
        default:
            tcflush(fd, TCIFLUSH);
        }
    }

    if (tty) {
        safe_write(fd, "\n", 1);
        tcsetattr(fd, TCSADRAIN, &term_old);
    }

fail_tty:
    if (fd != STDIN_FILENO)
        safe_close(fd);

    if (str)
        str[len] = '\0';

    return str;
}

char *
terminal_get_min(const char *prompt, int hide, size_t min)
{
    while (1) {
        char *str = terminal_get(prompt, hide);

        if (!str) {
            warning("cancelled\n");
            return NULL;
        }

        const size_t len = utf8_len(str);

        if (len >= min)
            return str;

        safe_free(str);

        if (len > 0)
            warning("too short, must be at least %i characters\n", min);
    }
}

int
terminal_get_yesno(const char *prompt, int yes)
{
    char *ask = STR_MAKE(prompt, " [", yes ? "Y/n" : "y/N", "]? ");
    int ret = yes;

    while (1) {
        char *str = terminal_get(ask, 0);

        if (!str)
            break;

        char c = str_skip_space(str)[0];
        safe_free(str);

        if (!c)
            break;

        if (c == 'y' || c == 'Y') {
            ret = 1;
            break;
        }

        if (c == 'n' || c == 'N') {
            ret = 0;
            break;
        }
    }

    safe_free(ask);

    return ret;
}

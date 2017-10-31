#include "storage.h"
#include "buffer-static.h"

#include <pwd.h>

static struct storage_global {
    char *dir;
} global;

static int
storage_init(void)
{
    if (global.dir)
        return 1;

    struct passwd *pw;

    do {
        errno = 0;
        pw = getpwuid(geteuid());
    } while (!pw && errno == EINTR);

    if (!pw) {
        if (errno)
            warning("%s: %m\n", "getpwuid");
        return 0;
    }

    if (str_empty(pw->pw_dir))
        return 0;

    global.dir = STR_MAKE(pw->pw_dir, "/." PROG_SERVICE "/");

    if (mkdir(global.dir, 0700) == -1 && errno != EEXIST)
        warning("%s(%s): %m\n", "mkdir", global.dir);

    return 1;
}

void
storage_save(const char *basename, char **str)
{
    if (!storage_init())
        return;

    char *filename = STR_MAKE(global.dir, basename);
    int file = safe_open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    safe_free(filename);

    if (str) {
        for (int i = 0; str[i]; i++) {
            safe_write(file, str[i], str_len(str[i]));
            safe_write(file, "\n", 1);
        }
    }

    safe_close(file);
}

char **
storage_load(const char *basename)
{
    if (!storage_init())
        return NULL;

    char *filename = STR_MAKE(global.dir, basename);
    int file = safe_open(filename, O_RDONLY);
    safe_free(filename);

    if (file == -1)
        return NULL;

    buffer_t buffer;
    buffer_setup(&buffer, NULL, CONFIG_BUFFER_SIZE);

    char **str = safe_calloc(1, sizeof(char *));
    int i = 0;

    while (1) {
        if (!buffer_read_size(&buffer)) {
            buffer_shift(&buffer);
            buffer.write += safe_read(file, buffer.write, buffer_write_size(&buffer));
            if (!buffer_read_size(&buffer))
                break;
        }

        char *line = (char *)buffer.read;
        int nl = 0;

        while (buffer_read_size(&buffer) && !nl)
            nl = (buffer_read(&buffer) == '\n');

        int len = (char *)buffer.read - line - nl;

        if (len) {
            line[len] = 0;
            if (str[i]) {
                str[i] = STR_MAKE(str[i], STR_FREE, line);
            } else {
                str[i] = STR_MAKE(line);
            }
        }

        if (str[i]) {
            str = safe_realloc(str, (i + 2) * sizeof(char *));
            str[i + 1] = NULL;
            i += nl;
        }
    }

    safe_free(buffer.data);
    safe_close(file);

    return str;
}

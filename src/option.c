#include "option.h"

typedef struct option option_t;

struct option {
    option_type_t type;
    const char *name;
    int len;
    const char *help;
    int set;
    void *data;
    option_t *next;
};

static struct option_global {
    option_t *options;
    int len;
} global;

static int
set_bool(void *dst, const char *src)
{
    const char *const str[] = {
        "on", "off", "true", "false", "enable", "disable",
    };

    int len = 0;

    while (len <= 7 && src[len] != '\0')
        len++;

    if (len == 1) {
        *(int *)dst = src[0] != '0';
        return 0;
    }

    if (len >= 2 && len <= 7) {
        for (int i = 0; i < len; i++)
            if ((str[len - 2][i] != src[i]) &&
                (str[len - 2][i] != src[i] + 'a' - 'A'))
                return -1;
        *(int *)dst = !(len & 1);
        return 0;
    }

    return -1;
}

static int
set_int(void *dst, const char *src)
{
    char *str = NULL;

    errno = 0;
    long tmp = strtol(src, &str, 0);

    if (errno || src == str)
        return -1;

    if (tmp == LONG_MIN || tmp == LONG_MAX)
        return -1;

    switch (str[0]) {
    case 'G': tmp <<= 10;
    case 'M': tmp <<= 10;
    case 'K': tmp <<= 10;
    }

    if (tmp < INT_MIN || tmp > INT_MAX)
        return -1;

    *(int *)dst = tmp;

    return 0;
}

static int
set_real(void *dst, const char *src)
{
    char *str = NULL;

    errno = 0;
    const double tmp = strtod(src, &str);

    if (errno || src == str)
        return -1;

    *(double *)dst = tmp;

    return 0;
}

static int
set_str(void *dst, const char *src)
{
    *(const char **)dst = src;

    return 0;
}

static int
set_flag(void *dst, _unused_ const char *src)
{
    *(int *)dst = 1;

    return 0;
}

static const struct {
    const char *label;
    int (*set)(void *, const char *);
} types[] = {
        [opt_flag] = {"", set_flag},
        [opt_bool] = {"BOOLEAN", set_bool},
        [opt_str ] = {"STRING", set_str},
        [opt_int ] = {"NUMBER", set_int},
        [opt_real] = {"REAL", set_real},
        [opt_host] = {"HOST", set_str},
        [opt_port] = {"PORT", set_str},
        [opt_file] = {"FILE", set_str},
        [opt_name] = {"NAME", set_str},
        [opt_list] = {"LIST", set_str},
};

_pure_ static int
str_dist(const char *sa, int la,
         const char *sb, int lb)
{
    int ci[256] = {0};
    int d[la + 2][lb + 2];

    d[0][0] = la + lb;

    for (int i = 0; i <= la; i++) {
        d[i + 1][0] = d[0][0];
        d[i + 1][1] = i;
    }

    for (int j = 0; j <= lb; j++) {
        d[0][j + 1] = d[0][0];
        d[1][j + 1] = j;
    }

    for (int i = 1; i <= la; i++) {
        const uint8_t ca = sa[i - 1];
        int cj = 0;
        for (int j = 1; j <= lb; j++) {
            const uint8_t cb = sb[j - 1];
            const int k = ci[cb];
            const int c = ca == cb;
            d[i + 1][j + 1] = MIN(MIN(MIN(d[i][j] + ((1 - c) << 1),
                                          d[i + 1][j] + 1),
                                      d[i][j + 1] + 1),
                                  d[k][cj] + i - k + j - cj - 1);
            if (c)
                cj = j;
        }
        ci[ca] = i;
    }

    int dmin = d[la + 1][lb + 1];

    for (int l = la; l <= lb; l++) {
        if (d[la + 1][l + 1] < dmin)
            dmin = d[la + 1][l + 1];
    }

    return dmin;
}

static const char *
get_word(const char *str, int *len)
{
    int i = 0;

    if (str) {
        while (*str == '-')
            str++;
        if (str[0] >= 'a' && str[0] <= 'z')
            while ((i < 64) && ((str[i] >= '0' && str[i] <= '9') ||
                                (str[i] >= 'a' && str[i] <= 'z') ||
                                (str[i] == '-' || str[i] == '_')))
                i++;
    }

    *len = i;

    return str;
}

void
option(option_type_t type, void *data, const char *name, const char *help)
{
    if (!name && (!data || help || !type))
        return;

    int len = 0;
    name = get_word(name, &len);

    if (len && help)
        global.len = MAX(global.len, len);

    option_t *opt = safe_calloc(1, sizeof(option_t));
    opt->name = name;
    opt->len = len;
    opt->type = type;
    opt->help = help;
    opt->data = data;

    static option_t **last = &global.options;

    *last = opt;
    last = &opt->next;
}

static void
option_list(void)
{
    print("options:\n");

    for (option_t *opt = global.options; opt; opt = opt->next) {
        if (opt->help)
            print("  --%s%c%-*s %s\n", opt->name, opt->type ? '=' : ' ',
                  global.len - opt->len - 1 + 10, types[opt->type].label, opt->help);
    }
}

static void
option_print(const char *name)
{
    print("usage: %s [OPTION]...", name);

    for (option_t *opt = global.options; opt; opt = opt->next) {
        if (!opt->name)
            print(" [%s]", types[opt->type].label);
    }

    print("\n");

    option_list();
}

static option_t *
option_search_use(const char *arg)
{
    for (option_t *opt = global.options; opt; opt = opt->next)
        if (!opt->name && !opt->set)
            return opt;

    warning("useless argument `%s'\n", arg);

    return NULL;
}

static option_t *
option_search(const char *name, int len)
{
    option_t *found = NULL;
    int distmin = INT_MAX;

    for (option_t *opt = global.options; opt; opt = opt->next) {
        if (!opt->name)
            continue;

        int dist = str_dist(name, len, opt->name, opt->len);

        if (opt->len == len && !dist)
            return opt;

        if (!opt->help)
            continue;

        if (dist > DIV(len, 2))
            continue;

        if (dist < distmin) {
            found = opt;
            distmin = dist;
            continue;
        }

        if (dist == distmin)
            found = NULL;
    }

    if (!found)
        warning("option `%.*s' is not recognized\n", len, name);

    return found;
}

static int
option_run_loop(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        int len = 0;
        const char *arg = argv[i];
        const char *word = get_word(arg, &len);

        if (word > arg + 1 && !len)
            break;

        option_t *opt = NULL;

        if (word != arg && len) {
            opt = option_search(word, len);
        } else if (arg) {
            opt = option_search_use(arg);
        }

        if (!opt)
            return -1;

        if (opt->type && opt->name) {
            arg = word[len] == '=' ? &word[len + 1] : argv[++i];
            if (!arg) {
                warning("missing argument for option `%s'\n", opt->name);
                return -1;
            }
        }

        if (!opt->data) {
            warning("option `%s' is ignored\n", opt->name);
            continue;
        }

        if (types[opt->type].set(opt->data, arg)) {
            warning("argument `%s' is not a valid %s\n", arg, types[opt->type].label);
            return -1;
        }

        opt->set = 1;
    }

    return 0;
}

void
option_run(int argc, char **argv)
{
    int help = 0;
    int version = 0;

    option(opt_flag, &version, "version", "display version information");
    option(opt_flag, &help, "help", "display this help");

    if (option_run_loop(argc, argv))
        error("try `%s --help' for more information\n", argv[0]);

    if (help) {
        option_print(argv[0]);
        exit(EXIT_SUCCESS);
    }

    if (version) {
        print("%s version %s\n", PROG_NAME, PROG_VERSION);
        exit(EXIT_SUCCESS);
    }

    while (global.options) {
        option_t *opt = global.options;
        global.options = opt->next;
        safe_free(opt);
    }
}

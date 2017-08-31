#include "acl.h"
#include "common-static.h"

static acl_t **
acl_find(acl_t **acl, const char *name)
{
    if (!acl || str_empty(name))
        return NULL;

    while (*acl) {
        if (!str_cmp((*acl)->name, name))
            break;
        acl = &(*acl)->next;
    }

    return acl;
}

void
acl_put(acl_t **acl, const char *name, int level)
{
    if (!acl || str_empty(name))
        return;

    acl_t **ref = acl_find(acl, name);

    if (level) {
        if (!*ref) {
            (*ref) = safe_calloc(1, sizeof(acl_t));
            (*ref)->name = STR_MAKE(name);
        }
        (*ref)->level = level;
    } else if (*ref) {
        acl_t *tmp = *ref;
        *ref = tmp->next;
        safe_free(tmp->name);
        safe_free(tmp);
    }
}

int
acl_get(acl_t **acl, const char *name)
{
    if (!acl || str_empty(name))
        return 0;

    acl_t **ref = acl_find(acl, name);

    if (!*ref)
        return 0;

    return (*ref)->level;
}

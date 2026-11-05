#include <stdio.h>

#include "safefile.h"
#include "statefs.h"

#define NEW_PATH_MAX 48

static bool new_path(char *out, const char *path) {
    int n = snprintf(out, NEW_PATH_MAX, "%s.NEW", path);
    return n > 0 && n < NEW_PATH_MAX;
}

bool safefile_save(const char *path, const void *data, size_t size) {
    char fresh[NEW_PATH_MAX];
    if (!new_path(fresh, path))
        return false;
    FILE *f = statefs_fopen(fresh, "wb");
    if (f == NULL)
        return false;
    bool ok = fwrite(data, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    ok = statefs_sync() && ok;
    if (!ok) {
        statefs_remove(fresh);
        statefs_sync();
        return false;
    }
    return safefile_take_new(path);
}

bool safefile_take_new(const char *path) {
    char fresh[NEW_PATH_MAX];
    if (!new_path(fresh, path) || !statefs_file_exists(fresh))
        return false;
    /* FatFs won't rename over a file. */
    if (statefs_file_exists(path))
        statefs_remove(path);
    bool ok = statefs_rename(fresh, path) == 0;
    return statefs_sync() && ok;
}

void safefile_recover(const char *path) {
    if (!statefs_file_exists(path))
        safefile_take_new(path);
}

bool safefile_remove(const char *path) {
    char fresh[NEW_PATH_MAX];
    if (!new_path(fresh, path))
        return false;
    if (statefs_file_exists(fresh))
        statefs_remove(fresh);
    if (statefs_file_exists(path))
        statefs_remove(path);
    return statefs_sync();
}

size_t safefile_read(const char *path, bool new_copy, void *data, size_t size) {
    char fresh[NEW_PATH_MAX];
    if (new_copy && !new_path(fresh, path))
        return 0;
    FILE *f = statefs_fopen(new_copy ? fresh : path, "rb");
    if (f == NULL)
        return 0;
    size_t got = fread(data, 1, size, f);
    fclose(f);
    return got;
}

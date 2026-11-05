/* safefile.c on the Mac's files, with the drive's failures made to order. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "safefile.h"
#include "statefs.h"

static const char *dir;
static int failures;
static int write_room = 1 << 20; /* bytes before the drive is full */
static bool sync_fails, cut_before_rename;

#define CHECK(cond, what)                  \
    do {                                   \
        if (!(cond)) {                     \
            printf("FAILED: %s\n", what);  \
            failures++;                    \
        }                                  \
    } while (0)

static void real_path(char *out, const char *path) {
    snprintf(out, 200, "%s%s", dir, path);
}

static int limited_write(void *cookie, const char *buf, int n) {
    int put = n < write_room ? n : write_room;
    write_room -= put;
    if (put == 0)
        return -1;
    return (int) fwrite(buf, 1, (size_t) put, cookie);
}

static int limited_close(void *cookie) {
    return fclose(cookie);
}

FILE *statefs_fopen(const char *name, const char *mode) {
    char path[200];
    real_path(path, name);
    FILE *f = fopen(path, mode);
    if (f == NULL || mode[0] != 'w')
        return f;
    return funopen(f, NULL, limited_write, NULL, limited_close);
}

bool statefs_sync(void) {
    return !sync_fails;
}

bool statefs_file_exists(const char *name) {
    char path[200];
    real_path(path, name);
    return access(path, F_OK) == 0;
}

int statefs_remove(const char *name) {
    char path[200];
    real_path(path, name);
    if (cut_before_rename && strstr(name, ".NEW") == NULL) {
        remove(path);
        return 0;
    }
    return remove(path);
}

int statefs_rename(const char *oldname, const char *newname) {
    if (cut_before_rename)
        return -1;
    char from[200], to[200];
    real_path(from, oldname);
    real_path(to, newname);
    if (access(to, F_OK) == 0)
        return -1; /* as FatFs: no renaming over a file */
    return rename(from, to);
}

static bool holds(const char *path, bool new_copy, const char *text) {
    char got[32] = {0};
    size_t n = safefile_read(path, new_copy, got, sizeof(got) - 1);
    return n == strlen(text) && strcmp(got, text) == 0;
}

int main(int argc, char **argv) {
    dir = argc > 1 ? argv[1] : ".";
    const char *file = "/SAFE.DAT";

    CHECK(safefile_save(file, "first", 5) && holds(file, false, "first"), "a new file");
    CHECK(safefile_save(file, "second", 6) && holds(file, false, "second"), "over an old one");
    CHECK(!statefs_file_exists("/SAFE.DAT.NEW"), "nothing left beside it");

    write_room = 3;
    CHECK(!safefile_save(file, "third!!", 7), "a full drive says so");
    write_room = 1 << 20;
    CHECK(holds(file, false, "second"), "a full drive keeps the old one");
    CHECK(!statefs_file_exists("/SAFE.DAT.NEW"), "and leaves no part of the new");

    sync_fails = true;
    CHECK(!safefile_save(file, "third!!", 7), "a flush that fails says so");
    sync_fails = false;
    CHECK(holds(file, false, "second"), "and keeps the old one");

    cut_before_rename = true;
    CHECK(!safefile_save(file, "fourth", 6), "a cut between the steps");
    cut_before_rename = false;
    CHECK(safefile_read(file, false, (char[8]) {0}, 8) == 0, "the file itself is gone then");
    CHECK(holds(file, true, "fourth"), "and the new copy can be read");

    CHECK(safefile_remove(file) && !statefs_file_exists(file) && !statefs_file_exists("/SAFE.DAT.NEW"), "removed");
    CHECK(safefile_remove(file), "removing what isn't there is fine");

    if (failures)
        return 1;
    printf("Safe files: all passed\n");
    return 0;
}

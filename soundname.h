#ifndef SOUNDNAME_H
#define SOUNDNAME_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The file a sound's name in ALPHA means, "<folder>/<name><ext>" -- kept free
 * of hardware for tests/run.sh. False if the name can't be a file: empty, not
 * plain ASCII (ALPHA also holds the HP-42S's symbols), a backslash, "..", or a
 * "/" when slashes is false, or the path doesn't fit. */
bool sound_name_path(char *path, size_t size, const char *folder, const char *ext,
                     bool slashes, const char *name, int len);

#ifdef __cplusplus
}
#endif

#endif

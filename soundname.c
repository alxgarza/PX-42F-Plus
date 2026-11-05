#include <stdio.h>

#include "soundname.h"

bool sound_name_path(char *path, size_t size, const char *folder, const char *ext,
                     bool slashes, const char *name, int len) {
    if (len <= 0)
        return false;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char) name[i];
        if (c < ' ' || c > '~' || c == '\\' || (c == '/' && !slashes))
            return false;
        if (c == '.' && i > 0 && name[i - 1] == '.')
            return false;
    }
    int n = snprintf(path, size, "%s/%.*s%s", folder, len, name, ext);
    return n > 0 && (size_t) n < size;
}

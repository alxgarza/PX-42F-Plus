#include "bootlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "rtc.h"
#include "statefs.h"

#define LOG_PATH "/BOOTLOG.TXT"
#define LOG_MAX  2048 /* the file keeps its last lines up to this many bytes */

static char log[LOG_MAX + 1];

/* The file into log (NUL-terminated); its length. */
static int read_log(void) {
    int len = 0;
    FILE *f = statefs_fopen(LOG_PATH, "rb");
    if (f != NULL) {
        len = (int) fread(log, 1, LOG_MAX, f);
        fclose(f);
    }
    log[len] = 0;
    return len;
}

void bootlog_add(const char *fmt, ...) {
    if (!statefs_ready())
        return;
    int len = read_log();

    rtc_datetime_t now;
    rtc_get_datetime(&now);
    char line[200];
    int n = snprintf(line, sizeof(line), "%02d-%02d %02d:%02d:%02d ", now.month, now.day, now.hour, now.min, now.sec);
    va_list args;
    va_start(args, fmt);
    n += vsnprintf(line + n, sizeof(line) - (size_t) n - 1, fmt, args);
    va_end(args);
    if (n > (int) sizeof(line) - 2)
        n = (int) sizeof(line) - 2;
    line[n++] = '\n';

    /* Room for it: the oldest lines go. */
    while (len + n > LOG_MAX) {
        char *newline = memchr(log, '\n', (size_t) len);
        int cut = newline != NULL ? (int) (newline - log) + 1 : len;
        memmove(log, log + cut, (size_t) (len - cut));
        len -= cut;
    }
    memcpy(log + len, line, (size_t) n);
    len += n;

    FILE *f = statefs_fopen(LOG_PATH, "wb");
    if (f != NULL) {
        fwrite(log, 1, (size_t) len, f);
        fclose(f);
        statefs_sync();
    }
}

void bootlog_print(void) {
    if (!statefs_ready()) {
        printf("The drive is not available.\n");
        return;
    }
    read_log();
    printf("%s", log[0] != 0 ? log : "(empty)\n");
}

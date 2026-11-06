#ifndef BOOTLOG_H
#define BOOTLOG_H

#ifdef __cplusplus
extern "C" {
#endif

/* /BOOTLOG.TXT on the drive: one line for each start that wasn't a plain ON
 * with the memory from RAM (what restarted the calculator, where its memory
 * came from and why not from the other place) -- the last ~2KB. Written for the question
 * "why did the memory clear?": after it happens, the file says which restart
 * it was and whether a saved copy was missing, empty or damaged. Also
 * `!BOOTLOG` on the console. */
void bootlog_add(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void bootlog_print(void);

#ifdef __cplusplus
}
#endif

#endif

#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Native apps and games (px_app.h): .bin files on the drive, run from
 * Settings -> Games & Apps. Each has its own folder next to it for its
 * files: /BIN/GAMES/SNAKE.bin keeps its best score in /BIN/GAMES/SNAKE/. */
#define BIN_DIR   "/BIN"
#define APPS_DIR  BIN_DIR "/APPS"
#define GAMES_DIR BIN_DIR "/GAMES"
#define APP_NAME_MAX 28 /* file name without ".bin" */

/* Makes /BIN and its two folders, if they're missing. */
void app_folders(void);

/* Runs <dir>/<name>.bin until it returns. NULL once it has run -- then
 * *power_off says whether it was ended to turn off (ON held, the Sleep
 * Timer, an empty battery) -- or why it couldn't run (one line of the
 * menu); the calculator's memory is then as it was. */
const char *app_run(const char *dir, const char *name, bool *power_off);

/* The icon <dir>/<name>.bin carries (px_app_icon_t): 32 x 32, rows of 4
 * bytes, 1 = black. false if it has none, or the file can't be read. */
bool app_icon(const char *dir, const char *name, uint8_t bits[128]);

#ifdef __cplusplus
}
#endif

#endif

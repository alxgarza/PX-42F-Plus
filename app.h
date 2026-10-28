#ifndef APP_H
#define APP_H

#include <stdbool.h>

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

#ifdef __cplusplus
}
#endif

#endif

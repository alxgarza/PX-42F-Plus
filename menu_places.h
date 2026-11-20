#ifndef MENU_PLACES_H
#define MENU_PLACES_H

/* Where the icons are: the home screen's (Off, the menu's own, the
 * favourites) and each folder's (Apps, Games), blank places among them,
 * kept on the drive by name so they outlast a reset or a reflash. No
 * hardware here, only the drive (statefs): the menu_shots tool runs it on
 * the Mac and checks the files it writes. */

#include <stdbool.h>

#include "app.h"
#include "menu_icons.h"

enum {
    HOME_OFF,
    HOME_SETTINGS,
    HOME_PLUS42,
    HOME_APPS,
    HOME_GAMES,
    HOME_USB,
    HOME_INFO,
    HOME_ALARMS,
    HOME_HELP,
    HOME_FIXED
};

typedef struct {
    const char *name;
    menu_icon_t icon;
} home_item_t;
extern const home_item_t home_fixed[HOME_FIXED];

#define PLACES_ROW   7  /* icons in a row of the grid: a new row of places opens while carrying */
#define MAX_APPS     64 /* of each folder */
#define HOME_SLOTS   (HOME_FIXED + 2 * MAX_APPS + 3 * PLACES_ROW) /* places, blanks among them */
#define FOLDER_SLOTS (MAX_APPS + 3 * PLACES_ROW)
#define HOME_BLANK   -1

/* Both folders' apps, by name: app_dirs[] is APPS_DIR or GAMES_DIR. */
extern char app_names[2 * MAX_APPS][APP_NAME_MAX + 1];
extern const char *app_dirs[2 * MAX_APPS];
extern int app_count;

/* What's at each place: < HOME_FIXED the menu's, else HOME_FIXED + an app,
 * or HOME_BLANK (its name NULL). home_count: places up to the last icon. */
extern int home_order[HOME_SLOTS];
extern const char *home_names[HOME_SLOTS];
extern int home_count;

/* The folder folder_list() read: an app (into app_names[]) or HOME_BLANK.
 * folder_count: places up to the last app, 0 if none. */
extern int folder_order[FOLDER_SLOTS];
extern const char *folder_names[FOLDER_SLOTS];
extern int folder_count;

/* The apps on the drive now, and the home screen's places. */
void home_list(void);
/* After home_list(): dir's apps that aren't favourites, in their places.
 * Returns how many apps dir has, favourites too. */
int folder_list(const char *dir);

/* Moving (run_grid()'s grid_moves_t): swaps two places; then the count
 * without the trailing blanks, the file written only if `changed`. */
void home_move(int from, int to);
int home_save(bool changed);
void folder_move(int from, int to);
int folder_save(bool changed);

/* FAV: the folder's app at `place` to the home screen's first empty place,
 * its folder place left blank. False if the home screen is full. */
bool places_fav(int place);
/* UNFAV: the favourite at home `place` back to its folder (its first empty
 * place there, at the next folder_list()); its home place left blank. */
void places_unfav(int place);

#endif

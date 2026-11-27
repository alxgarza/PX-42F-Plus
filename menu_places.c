#include "menu_places.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "safefile.h"
#include "statefs.h"

/* The home screen: Off, the menu's own icons (the Apps and Games folders
 * among them), then the favourites, the apps and games put there from
 * their folder (FAV). In the order the icons were moved into, kept in
 * HOME_ORDER_FILE one place a line after Off: the menu's by their names,
 * the favourites as APPS/NAME and GAMES/NAME, an empty line a blank place;
 * a favourite no longer on the drive leaves its place blank. */
#define HOME_ORDER_FILE "/HOME.TXT"

/* A folder's apps that aren't favourites, the same way: FOLDER_ORDER in the
 * folder, an app's name a line. One the file doesn't name (new on the
 * drive, back from the home screen) takes the first empty place, by name;
 * a favourite's or a deleted app's place is left blank. */
#define FOLDER_ORDER "ORDER.TXT"

/* Both written through safefile (beside, then in place): a reset in the
 * middle of a write left a cut file, and the layout and favourites with
 * it. Names are matched without case, as the drive's own are: an app
 * copied again may come back as snake.bin. */

const home_item_t home_fixed[HOME_FIXED] = {
        {"Off", MENU_ICON_OFF},       {"Settings", MENU_ICON_SETTINGS}, {"Plus42", MENU_ICON_PLUS42},
        {"Apps", MENU_ICON_APPS},     {"Games", MENU_ICON_GAMES},       {"USB Drive", MENU_ICON_USB},
        {"Info", MENU_ICON_INFO},     {"Alarms", MENU_ICON_ALARMS},     {"Help", MENU_ICON_HELP},
        {"CAS", MENU_ICON_CAS},
};

/* Where each is at first: the top row Off, USB Drive, Plus42, Alarms,
 * Apps, Games, Settings; the second row empty (the favourites go there);
 * the CAS first on the third, Help and Info at its right. */
static const unsigned char home_default[HOME_FIXED] = {
        [HOME_OFF] = 0,  [HOME_USB] = 1,  [HOME_PLUS42] = 2, [HOME_ALARMS] = 3, [HOME_APPS] = 4,
        [HOME_GAMES] = 5, [HOME_SETTINGS] = 6, [HOME_CAS] = 14, [HOME_HELP] = 19, [HOME_INFO] = 20,
};

char app_names[2 * MAX_APPS][APP_NAME_MAX + 1];
const char *app_dirs[2 * MAX_APPS];
int app_count;

int home_order[HOME_SLOTS];
const char *home_names[HOME_SLOTS];
int home_count;

int folder_order[FOLDER_SLOTS];
const char *folder_names[FOLDER_SLOTS];
int folder_count;
static const char *folder_dir;

/* A file of places, read or written whole: the longest is the home
 * screen's, a line of up to "GAMES/" + a name each. */
static char text[HOME_SLOTS * (APP_NAME_MAX + 8)];

/* The file (or, after a cut, the copy written beside it) into text[],
 * NUL-ended; "" if neither. */
static void read_places(const char *path) {
    size_t n = statefs_ready() ? safefile_read(path, false, text, sizeof(text) - 1) : 0;
    if (n == 0 && statefs_ready())
        n = safefile_read(path, true, text, sizeof(text) - 1);
    text[n] = 0;
}

/* The next line of text[] at *at, ended there (\r\n or \n), or NULL at the end. */
static char *next_line(char **at) {
    char *line = *at;
    if (*line == 0)
        return NULL;
    size_t len = strcspn(line, "\n");
    *at = line + len + (line[len] != 0);
    line[len] = 0;
    if (len > 0 && line[len - 1] == '\r')
        line[len - 1] = 0;
    return line;
}

static int by_name(const void *a, const void *b) {
    return strcasecmp((const char *) a, (const char *) b);
}

/* ----- the home screen ----- */

/* An item as the order file names it. */
static void home_key(int id, char *buf, size_t size) {
    if (id < HOME_FIXED)
        snprintf(buf, size, "%s", home_fixed[id].name);
    else
        snprintf(buf, size, "%s/%s", app_dirs[id - HOME_FIXED] + strlen(BIN_DIR) + 1, app_names[id - HOME_FIXED]);
}

/* home_count from the places: an icon may have been carried past the last. */
static void home_trim(void) {
    home_count = HOME_SLOTS;
    while (home_count > 1 && home_order[home_count - 1] == HOME_BLANK)
        home_count--;
}

/* The first empty place after Off; past the last icon if there's none. */
static int home_free(void) {
    int at = 1;
    while (at < HOME_SLOTS - 1 && home_order[at] != HOME_BLANK)
        at++;
    return at;
}

static void home_name_all(void) {
    for (int i = 0; i < HOME_SLOTS; i++) {
        int id = home_order[i];
        home_names[i] = id == HOME_BLANK ? NULL : id < HOME_FIXED ? home_fixed[id].name : app_names[id - HOME_FIXED];
    }
}

void home_list(void) {
    int apps = 0;
    if (statefs_ready()) {
        static const char *const dirs[] = {APPS_DIR, GAMES_DIR};
        for (int d = 0; d < 2; d++) {
            int n = statefs_list_files(dirs[d], ".bin", (char *) app_names[apps], APP_NAME_MAX + 1, MAX_APPS);
            qsort(app_names[apps], (size_t) n, APP_NAME_MAX + 1, by_name);
            for (int i = 0; i < n; i++)
                app_dirs[apps + i] = dirs[d];
            apps += n;
        }
    }
    app_count = apps;
    int total = HOME_FIXED + apps;
    static bool placed[HOME_FIXED + 2 * MAX_APPS];
    memset(placed, 0, sizeof(placed));
    for (int i = 0; i < HOME_SLOTS; i++)
        home_order[i] = HOME_BLANK;
    home_order[0] = HOME_OFF;
    placed[HOME_OFF] = true;
    home_count = 1;

    /* A file from before the folders names every app (they were all on
     * the home screen) and no "Apps": it's left out, the icons go where
     * they are at first. */
    read_places(HOME_ORDER_FILE);
    bool folders = false;
    for (const char *s = text; *s != 0; s += strcspn(s, "\n") + (s[strcspn(s, "\n")] != 0))
        folders |= strncasecmp(s, "Apps", 4) == 0 && (s[4] == 0 || s[4] == '\r' || s[4] == '\n');
    char *at = text, *line, key[APP_NAME_MAX + 8];
    while (folders && home_count < HOME_SLOTS - HOME_FIXED && (line = next_line(&at)) != NULL) {
        int found = HOME_BLANK;
        for (int id = 0; line[0] != 0 && id < total; id++) {
            home_key(id, key, sizeof(key));
            if (!placed[id] && strcasecmp(line, key) == 0) {
                found = id;
                break;
            }
        }
        if (found != HOME_BLANK)
            placed[found] = true;
        home_order[home_count++] = found; /* not found: a blank place, the others stay put */
    }
    /* The menu's own the file doesn't name (all of them without a file;
     * one added by a newer firmware) where they are at first, or in the
     * first empty place if that one's taken. */
    for (int id = 0; id < HOME_FIXED; id++)
        if (!placed[id]) {
            int to = home_default[id];
            if (home_order[to] != HOME_BLANK)
                to = home_free();
            home_order[to] = id;
        }
    home_trim();
    home_name_all();
}

void home_move(int from, int to) {
    int id = home_order[from];
    const char *name = home_names[from];
    home_order[from] = home_order[to];
    home_names[from] = home_names[to];
    home_order[to] = id;
    home_names[to] = name;
}

int home_save(bool changed) {
    home_trim();
    if (!changed || !statefs_ready())
        return home_count;
    size_t n = 0;
    for (int i = 1; i < home_count; i++) { /* Off is always first */
        char key[APP_NAME_MAX + 8] = "";
        if (home_order[i] != HOME_BLANK)
            home_key(home_order[i], key, sizeof(key));
        n += (size_t) snprintf(text + n, sizeof(text) - n, "%s\n", key);
    }
    safefile_save(HOME_ORDER_FILE, text, n);
    return home_count;
}

/* ----- the folders ----- */

static bool on_home(int app) {
    for (int i = 0; i < home_count; i++)
        if (home_order[i] == HOME_FIXED + app)
            return true;
    return false;
}

static void folder_trim(void) {
    folder_count = FOLDER_SLOTS;
    while (folder_count > 0 && folder_order[folder_count - 1] == HOME_BLANK)
        folder_count--;
}

static void folder_path(char *buf, size_t size) {
    snprintf(buf, size, "%s/" FOLDER_ORDER, folder_dir);
}

int folder_list(const char *dir) {
    static bool placed[2 * MAX_APPS];
    folder_dir = dir;
    int in_dir = 0;
    for (int app = 0; app < app_count; app++) {
        /* By text: APPS_DIR in another file may be another copy of it. */
        bool here = strcmp(app_dirs[app], dir) == 0;
        placed[app] = !here || on_home(app);
        in_dir += here;
    }
    for (int i = 0; i < FOLDER_SLOTS; i++)
        folder_order[i] = HOME_BLANK;

    char path[48];
    folder_path(path, sizeof(path));
    read_places(path);
    char *at = text, *line;
    for (int place = 0; place < FOLDER_SLOTS && (line = next_line(&at)) != NULL; place++)
        for (int app = 0; line[0] != 0 && app < app_count; app++)
            if (!placed[app] && strcasecmp(line, app_names[app]) == 0) {
                placed[app] = true;
                folder_order[place] = app;
                break;
            }
    /* At most MAX_APPS of them: there's always an empty place. */
    for (int app = 0, place = 0; app < app_count; app++)
        if (!placed[app]) {
            while (folder_order[place] != HOME_BLANK)
                place++;
            folder_order[place] = app;
        }
    folder_trim();
    for (int i = 0; i < FOLDER_SLOTS; i++)
        folder_names[i] = folder_order[i] == HOME_BLANK ? NULL : app_names[folder_order[i]];
    return in_dir;
}

void folder_move(int from, int to) {
    int app = folder_order[from];
    const char *name = folder_names[from];
    folder_order[from] = folder_order[to];
    folder_names[from] = folder_names[to];
    folder_order[to] = app;
    folder_names[to] = name;
}

int folder_save(bool changed) {
    folder_trim();
    if (!changed || !statefs_ready())
        return folder_count;
    size_t n = 0;
    for (int i = 0; i < folder_count; i++)
        n += (size_t) snprintf(text + n, sizeof(text) - n, "%s\n",
                               folder_order[i] == HOME_BLANK ? "" : app_names[folder_order[i]]);
    char path[48];
    folder_path(path, sizeof(path));
    safefile_save(path, text, n);
    return folder_count;
}

/* ----- favourites ----- */

bool places_fav(int place) {
    int to = home_free();
    if (home_order[to] != HOME_BLANK)
        return false;
    home_order[to] = HOME_FIXED + folder_order[place];
    home_names[to] = folder_names[place];
    home_save(true);
    folder_order[place] = HOME_BLANK;
    folder_names[place] = NULL;
    folder_save(true);
    return true;
}

void places_unfav(int place) {
    home_order[place] = HOME_BLANK;
    home_names[place] = NULL;
    home_save(true);
}

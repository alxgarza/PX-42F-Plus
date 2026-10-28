#include "statefs.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "pico/mutex.h"
#include "pico/platform.h"
#include "pico/time.h"
#include "ff.h"
#include "flashdisk.h"
#include "usb_msc.h"
#include "hardware/watchdog.h"
#include "debug_checkpoint.h"
#include "ramstate.h"

#define STATE_DIR "/STATE"

/* The volume label -- the drive's name on a computer. FAT labels are
 * upper case, up to 11 characters. */
#define VOLUME_LABEL "PX-42F"
#define STATE_EXT ".p42"

/* In USB Disk mode, write back once the computer has paused this long. */
#define USB_IDLE_FLUSH_US 20000

static FATFS fs;
static bool mounted = false;

/* FatFs, and the flash cache under it, serve one caller at a time: core 1,
 * and core 0 reading a sound as it plays (speaker.c, through a FILE from
 * statefs_fopen()). Every function here that reaches either holds this
 * lock to its end -- FS_LOCKED() at its top -- and so do the open files'
 * callbacks. Recursive: these call each other. */
auto_init_recursive_mutex(fs_mutex);

static int fs_lock(void) {
    recursive_mutex_enter_blocking(&fs_mutex);
    return 0;
}

static void fs_unlock(int *unused) {
    (void) unused;
    recursive_mutex_exit(&fs_mutex);
}

/* The lock, held until the enclosing block is left, whichever way. */
#define FS_LOCKED() int fs_locked_ __attribute__((cleanup(fs_unlock), unused)) = fs_lock()

/* Gives an already-formatted volume the current label (volumes formatted
 * by older firmware are labeled "PX-42S"). Only writes when it differs. */
static void check_label(void) {
    char label[12];
    if (f_getlabel("", label, NULL) == FR_OK && strcmp(label, VOLUME_LABEL) != 0)
        f_setlabel(VOLUME_LABEL);
}

/* Just mounted: the folders, the label, and anything left in the cache. */
static void after_mount(void) {
    f_mkdir(STATE_DIR); /* FR_EXIST when it's already there -- fine */
    f_mkdir(STATEFS_SPLASH_DIR);
    check_label();      /* the computer may have renamed the drive */
    flashdisk_flush_all();
}

static bool mount(void) {
    mounted = f_mount(&fs, "", 1) == FR_OK;
    if (mounted)
        after_mount();
    return mounted;
}

/* ===================== mounting ===================== */

/* An empty drive: FAT, no partition table, a "superfloppy", which is what
 * removable drives usually are and every OS reads. Mounted after. */
static bool format(void) {
    static BYTE work[FF_MAX_SS];
    const MKFS_PARM opt = {FM_FAT | FM_SFD, 0, 0, 0, 0};
    if (f_mkfs("", &opt, work, sizeof(work)) != FR_OK)
        return false;
    flashdisk_flush_all();
    if (f_mount(&fs, "", 1) != FR_OK)
        return false;
    f_setlabel(VOLUME_LABEL);
    return true;
}

bool statefs_init(void) {
    FS_LOCKED();
    debug_checkpoint(CKPT_FS_INIT, 0);
    if (!flashdisk_init())
        return false;

    FRESULT r = f_mount(&fs, "", 1);
    if (r == FR_NO_FILESYSTEM) {
        /* First boot (blank flash): format. Only if it really is blank: a
         * drive that just can't be read -- reformatted as exFAT on a
         * computer, or its first sector cut off by a power loss -- is left
         * alone (not mounted; USB Drive still hands it to a computer, and
         * Format Drive is there), not silently wiped. */
        static BYTE work[FF_MAX_SS];
        if (!flashdisk_read(0, work, sizeof(work)))
            return false;
        for (size_t i = 0; i < sizeof(work); i++)
            if (work[i] != 0xFF) {
                printf("The drive isn't FAT and isn't blank -- left as it is, not formatted.\n");
                return false;
            }
        if (!format())
            return false;
    } else if (r != FR_OK) {
        return false;
    }
    mounted = true;
    after_mount();
    return true;
}

bool statefs_format(void) {
    FS_LOCKED();
    if (flashdisk_block_count() == 0)
        return false;
    if (mounted)
        f_mount(NULL, "", 0);
    mounted = false;
    if (!format())
        return false;
    mounted = true;
    after_mount();
    return true;
}

bool statefs_ready(void) {
    return mounted;
}

bool statefs_sync(void) {
    FS_LOCKED();
    return flashdisk_flush_all();
}

void statefs_state_path(const char *name, char *path, size_t size) {
    snprintf(path, size, "%s/%s%s", STATE_DIR, name, STATE_EXT);
}

bool statefs_file_exists(const char *path) {
    FS_LOCKED();
    FILINFO info;
    return mounted && f_stat(path, &info) == FR_OK;
}

void statefs_mkdir(const char *path) {
    FS_LOCKED();
    if (mounted)
        f_mkdir(path); /* FR_EXIST when it's already there -- fine */
}

bool statefs_set_readonly(const char *path, bool readonly) {
    FS_LOCKED();
    return mounted && f_chmod(path, readonly ? AM_RDO : 0, AM_RDO) == FR_OK;
}

bool statefs_state_exists(const char *name) {
    FS_LOCKED();
    char path[48];
    statefs_state_path(name, path, sizeof(path));
    FILINFO info;
    return mounted && f_stat(path, &info) == FR_OK;
}

bool statefs_delete_state(const char *name) {
    FS_LOCKED();
    char path[48];
    statefs_state_path(name, path, sizeof(path));
    bool ok = mounted && f_unlink(path) == FR_OK;
    flashdisk_flush_all();
    return ok;
}

bool statefs_rename_state(const char *old_name, const char *new_name) {
    FS_LOCKED();
    char old_path[48], new_path[48];
    statefs_state_path(old_name, old_path, sizeof(old_path));
    statefs_state_path(new_name, new_path, sizeof(new_path));
    bool ok = mounted && f_rename(old_path, new_path) == FR_OK; /* FR_EXIST if taken */
    flashdisk_flush_all();
    return ok;
}

static int compare_names(const void *a, const void *b) {
    return strcmp((const char *) a, (const char *) b);
}

/* The files (dirs false) or folders (true) in dir_path ending in ext. */
static int list_entries(const char *dir_path, bool dirs, const char *ext, char *names, size_t name_size, int max) {
    FS_LOCKED();
    if (!mounted)
        return 0;
    DIR dir;
    if (f_opendir(&dir, dir_path) != FR_OK)
        return 0;

    int count = 0;
    FILINFO info;
    size_t ext_len = strlen(ext);
    while (count < max && f_readdir(&dir, &info) == FR_OK && info.fname[0] != 0) {
        /* Hidden/system files include the "._name" AppleDouble files macOS
         * writes next to everything it copies -- skip those by name too. */
        if ((info.fattrib & (AM_HID | AM_SYS)) || ((info.fattrib & AM_DIR) != 0) != dirs || info.fname[0] == '.')
            continue;
        size_t len = strlen(info.fname);
        if (len <= ext_len || len - ext_len > name_size - 1 || strcasecmp(info.fname + len - ext_len, ext) != 0)
            continue; /* wrong kind of file, or a name too long to show */
        char *name = names + (size_t) count * name_size;
        memcpy(name, info.fname, len - ext_len);
        name[len - ext_len] = 0;
        count++;
    }
    f_closedir(&dir);
    qsort(names, (size_t) count, name_size, compare_names);
    return count;
}

int statefs_list_files(const char *dir_path, const char *ext, char *names, size_t name_size, int max) {
    return list_entries(dir_path, false, ext, names, name_size, max);
}

int statefs_list_dirs(const char *dir_path, char *names, size_t name_size, int max) {
    return list_entries(dir_path, true, "", names, name_size, max);
}

int statefs_list_states(char names[][STATEFS_NAME_MAX + 1], int max) {
    return statefs_list_files(STATE_DIR, STATE_EXT, (char *) names, STATEFS_NAME_MAX + 1, max);
}

/* ===================== stdio bridge for Free42 ===================== */

/* The read/write callbacks feed the watchdog: saving or loading a big
 * state is a long run of them, inside the Free42 core, with nothing else
 * feeding it meanwhile. Only on core 1: core 0 reading a sound mustn't
 * keep a hang on core 1 from being noticed. */
static int fil_read(void *cookie, char *buf, int n) {
    FS_LOCKED();
    if (get_core_num() == 1)
        watchdog_update();
    UINT got;
    return f_read((FIL *) cookie, buf, (UINT) n, &got) == FR_OK ? (int) got : -1;
}

static int fil_write(void *cookie, const char *buf, int n) {
    FS_LOCKED();
    watchdog_update();
    UINT put;
    return f_write((FIL *) cookie, buf, (UINT) n, &put) == FR_OK ? (int) put : -1;
}

static fpos_t fil_seek(void *cookie, fpos_t offset, int whence) {
    FS_LOCKED();
    FIL *fil = (FIL *) cookie;
    FSIZE_t base = whence == SEEK_CUR ? f_tell(fil) : whence == SEEK_END ? f_size(fil) : 0;
    if (f_lseek(fil, (FSIZE_t) ((long) base + offset)) != FR_OK)
        return -1;
    return (fpos_t) f_tell(fil);
}

static int fil_close(void *cookie) {
    FS_LOCKED();
    FIL *fil = (FIL *) cookie;
    FRESULT r = f_close(fil);
    free(fil);
    return r == FR_OK ? 0 : -1;
}

FILE *statefs_fopen(const char *name, const char *mode) {
    FS_LOCKED();
    if (ramstate_path(name))
        return ramstate_fopen(mode); /* the RAM-kept state, not a flash file */
    if (!mounted)
        return NULL;
    bool writing = mode[0] == 'w';
    if (!writing && mode[0] != 'r')
        return NULL;

    FIL *fil = (FIL *) malloc(sizeof(FIL));
    if (fil == NULL)
        return NULL;
    debug_checkpoint_saved_t saved = debug_checkpoint_save();
    debug_checkpoint(CKPT_FS_OPEN, 0);
    FILE *f = NULL;
    if (f_open(fil, name, writing ? FA_WRITE | FA_CREATE_ALWAYS : FA_READ) == FR_OK) {
        f = funopen(fil, writing ? NULL : fil_read, writing ? fil_write : NULL, fil_seek, fil_close);
        if (f == NULL)
            f_close(fil);
    }
    if (f == NULL)
        free(fil);
    debug_checkpoint_restore(saved);
    return f;
}

/* Free42 saves to name.<time>.crash and then renames it over name; a
 * /RAM/ "file" is written in place, so for those there's nothing to do. */
int statefs_rename(const char *oldname, const char *newname) {
    FS_LOCKED();
    if (ramstate_path(oldname) || ramstate_path(newname))
        return 0;
    return mounted && f_rename(oldname, newname) == FR_OK ? 0 : -1;
}

int statefs_remove(const char *name) {
    FS_LOCKED();
    if (ramstate_path(name))
        return 0;
    return mounted && f_unlink(name) == FR_OK ? 0 : -1;
}

/* ===================== USB Disk mode ===================== */

void statefs_usb_begin(void) {
    FS_LOCKED();
    flashdisk_flush_all();
    if (mounted)
        f_mount(NULL, "", 0); /* FatFs keeps its own copy of the FAT -- drop it */
    mounted = false;
    usb_msc_attach();
}

void statefs_usb_poll(void) {
    FS_LOCKED();
    bool sync = usb_msc_take_sync_request();
    bool idle = time_us_64() - flashdisk_last_write_us() >= USB_IDLE_FLUSH_US;
    if (flashdisk_room_needed() || (flashdisk_dirty() && (sync || idle)))
        flashdisk_flush_one();
}

void statefs_usb_end(void) {
    FS_LOCKED();
    usb_msc_detach();
    flashdisk_flush_all();
    mount(); /* re-reads everything the computer changed */
}

#include "ramstate.h"

#include <stdint.h>
#include <string.h>

#include "hardware/watchdog.h"
#include "hardware/regs/addressmap.h"
#include "retained.h"

#define RAMSTATE_MAGIC    0x52414D53u /* "RAMS" */
#define RAMSTATE_CAPACITY (128u * 1024u - 16u)

typedef struct {
    uint32_t magic;    /* RAMSTATE_MAGIC once a save completed */
    uint32_t length;   /* bytes of state in data[] */
    uint32_t checksum; /* over data[0..length) */
    uint32_t reserved;
    uint8_t data[RAMSTATE_CAPACITY];
} ram_state_t;

/* The top 128KB of RAM, which survives the deep power-off and the reboot
 * after it -- see ramstate.h and retained.h. */
_Static_assert(RETAINED_RAMSTATE + sizeof(ram_state_t) == SRAM_STRIPED_END, "must end at the top of RAM");
#define ram_state (*(ram_state_t *) RETAINED_RAMSTATE)

static uint32_t pos;       /* current read/write position */
static bool overflowed;    /* a write didn't fit: the state is incomplete */

static uint32_t checksum(const uint8_t *p, uint32_t n) {
    uint32_t sum = 0x811C9DC5u; /* FNV-1a */
    for (uint32_t i = 0; i < n; i++)
        sum = (sum ^ p[i]) * 16777619u;
    return sum;
}

bool ramstate_path(const char *path) {
    return strncmp(path, "/RAM/", 5) == 0;
}

bool ramstate_valid(void) {
    return ram_state.magic == RAMSTATE_MAGIC && ram_state.length <= RAMSTATE_CAPACITY
           && ram_state.checksum == checksum(ram_state.data, ram_state.length);
}

/* Both feed the watchdog, as statefs.c's file callbacks do: saving or
 * loading a big state is a long run of them inside the Free42 core. */
static int ram_read(void *cookie, char *buf, int n) {
    (void) cookie;
    watchdog_update();
    uint32_t left = ram_state.length - pos;
    uint32_t k = (uint32_t) n < left ? (uint32_t) n : left;
    memcpy(buf, ram_state.data + pos, k);
    pos += k;
    return (int) k;
}

static int ram_write(void *cookie, const char *buf, int n) {
    (void) cookie;
    watchdog_update();
    if ((uint32_t) n > RAMSTATE_CAPACITY - pos) {
        overflowed = true;
        return -1;
    }
    memcpy(ram_state.data + pos, buf, (size_t) n);
    pos += (uint32_t) n;
    if (pos > ram_state.length)
        ram_state.length = pos;
    return n;
}

static fpos_t ram_seek(void *cookie, fpos_t offset, int whence) {
    (void) cookie;
    long base = whence == SEEK_CUR ? (long) pos : whence == SEEK_END ? (long) ram_state.length : 0;
    long target = base + (long) offset;
    if (target < 0 || target > (long) RAMSTATE_CAPACITY)
        return -1;
    pos = (uint32_t) target;
    return (fpos_t) pos;
}

static int ram_close_read(void *cookie) {
    (void) cookie;
    return 0;
}

static int ram_close_write(void *cookie) {
    (void) cookie;
    if (overflowed)
        return -1; /* stays invalid */
    ram_state.checksum = checksum(ram_state.data, ram_state.length);
    ram_state.magic = RAMSTATE_MAGIC;
    return 0;
}

FILE *ramstate_fopen(const char *mode) {
    pos = 0;
    if (mode[0] == 'w') {
        ram_state.magic = 0; /* invalid until this save completes */
        ram_state.length = 0;
        overflowed = false;
        return funopen(&ram_state, NULL, ram_write, ram_seek, ram_close_write);
    }
    if (mode[0] == 'r' && ramstate_valid()) {
        ram_state.magic = 0; /* used up: read once (the data stays for this read) */
        return funopen(&ram_state, ram_read, NULL, ram_seek, ram_close_read);
    }
    return NULL;
}

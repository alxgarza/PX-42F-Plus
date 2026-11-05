/* The HP-41 Time Module's numbers (timemodule.c) and the clock's display
 * (rtc_format_clock()). tests/run.sh builds it with AddressSanitizer. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "rtc.h"
#include "settings.h"
#include "alarms.h"
#include "timemodule.h"
#include "stopwatch.h"

px_settings_t px_settings;
static int failures;

#define CHECK(cond, ...)                 \
    do {                                 \
        if (!(cond)) {                   \
            printf("FAIL: " __VA_ARGS__); \
            printf("\n");                \
            failures++;                  \
        }                                \
    } while (0)

static void check_digits(int64_t digits, int64_t expected) {
    int64_t ms = -1;
    CHECK(time_digits_to_ms(digits, &ms) && ms == expected, "%lld: %lld, not %lld", (long long) digits, (long long) ms, (long long) expected);
}

static void check_clock(bool with_date, bool h24, int format, const char *expected) {
    rtc_datetime_t t = {2026, 1, 21, 15, 15, 7, 0, 3};
    char buf[32];
    px_settings.clock_24h = h24;
    px_settings.date_format = (uint8_t) format;
    rtc_format_clock(buf, sizeof(buf), &t, with_date);
    CHECK(!strcmp(buf, expected), "clock: \"%s\", not \"%s\"", buf, expected);
}

#define DAY 86400000ULL
#define T0 (20000 * DAY)
#define ADD(at, rep, s) alarms_add(at, rep, s, (int) sizeof(s) - 1, T0)

static void test_alarm_sounds(void) {
    char path[ALARM_SOUND_PATH_MAX];
    alarms_clear();
    alarms_add(T0 + 100, 1000, "A", 1, T0);
    alarms_add(T0 + 200, 0, "B", 1, T0);
    CHECK(alarms_get(0)->sound == ALARM_SOUND_PLUS42, "a new alarm has Plus42's");
    CHECK(!alarm_sound_path(alarms_get(0), path, sizeof(path)), "Plus42's has no file");

    CHECK(alarms_set_sound(0, ALARM_SOUND_MIDI, "Canon"), "set a song");
    CHECK(alarm_sound_path(alarms_get(0), path, sizeof(path)) && !strcmp(path, "/MIDI/Canon.mid"), "%s", path);
    CHECK(alarms_set_sound(1, ALARM_SOUND_WAV, "1234567890123456789012345678"), "the longest name");
    CHECK(alarm_sound_path(alarms_get(1), path, sizeof(path))
          && !strcmp(path, "/SOUNDS/1234567890123456789012345678.wav"), "%s", path);
    CHECK(!alarms_set_sound(1, ALARM_SOUND_WAV, "12345678901234567890123456789"), "too long");
    CHECK(!strcmp(alarms_get(1)->sound_name, "1234567890123456789012345678"), "too long changes nothing");

    alarms_done(0, T0 + 150); /* repeats: moves past B, keeps its sound */
    CHECK(alarms_get(1)->text[0] == 'A' && alarms_get(1)->sound == ALARM_SOUND_MIDI
          && !strcmp(alarms_get(1)->sound_name, "Canon"), "a repeat keeps its sound");

    static unsigned char copy[sizeof(uint32_t) * 2 + ALARMS_MAX * sizeof(alarm_t)];
    size_t size;
    const void *image = alarms_image(&size);
    memcpy(copy, image, size);
    alarm_t *second = (alarm_t *) (copy + sizeof(uint32_t) * 2) + 1;
    CHECK(alarms_load(copy, size) && alarms_count() == 2 && alarms_get(1)->sound == ALARM_SOUND_MIDI
          && !strcmp(alarms_get(1)->sound_name, "Canon"), "sound survives the file");
    second->sound = ALARM_SOUND_MIDI + 1;
    CHECK(!alarms_load(copy, size) && alarms_count() == 0, "bad sound kind refused");
    second->sound = ALARM_SOUND_MIDI;
    memset(second->sound_name, 'x', sizeof(second->sound_name));
    CHECK(!alarms_load(copy, size) && alarms_count() == 0, "unterminated name refused");
    uint32_t old_magic = 0x314D4C41u; /* "ALM1", the layout without a sound */
    memcpy(copy, &old_magic, sizeof(old_magic));
    CHECK(!alarms_load(copy, size), "older file refused");
}

static void test_alarms(void) {
    alarms_clear();
    /* Earliest first, then as set. */
    CHECK(ADD(T0 + 300, 0, "C"), "add");
    CHECK(ADD(T0 + 100, 0, "A"), "add");
    CHECK(ADD(T0 + 300, 0, "D"), "add");
    CHECK(ADD(T0 + 200, 0, "B"), "add");
    CHECK(alarms_count() == 4 && alarms_get(0)->text[0] == 'A' && alarms_get(1)->text[0] == 'B'
          && alarms_get(2)->text[0] == 'C' && alarms_get(3)->text[0] == 'D', "order");
    CHECK(alarms_next_at() == T0 + 100, "next");
    CHECK(alarms_due(T0 + 99) == -1, "not yet");
    CHECK(alarms_due(T0 + 100) == 0, "due");
    alarms_done(0, T0 + 100);
    CHECK(alarms_count() == 3 && alarms_get(0)->text[0] == 'B', "once: gone");
    alarms_mark_past_due(0);
    CHECK(alarms_due(T0 + 1000) == 1, "past due isn't due again");
    CHECK(alarms_next_at() == T0 + 300, "past due isn't next");
    CHECK(alarms_any_past_due(), "any past due");

    /* A repeat counts from its own time, not from now, and lands after now. */
    alarms_clear();
    ADD(T0 + 1000, 500, "R");
    alarms_done(0, T0 + 2600);
    CHECK(alarms_get(0)->at_ms == T0 + 3000, "repeat: %llu", (unsigned long long) (alarms_get(0)->at_ms - T0));
    alarms_done(0, T0 + 2000);
    CHECK(alarms_get(0)->at_ms == T0 + 3000, "a repeat not yet due stays");
    alarms_mark_past_due(0);
    alarms_done(0, T0 + 9000);
    CHECK(!alarms_get(0)->past_due && alarms_get(0)->at_ms == T0 + 9500, "past due reset to the future");

    /* Set for the past: past due at once, and doesn't fire. */
    alarms_clear();
    ADD(T0 - 1, 0, "OLD");
    CHECK(alarms_get(0)->past_due && alarms_due(T0) == -1 && alarms_next_at() == 0, "past");

    /* The kinds, from the arrows. */
    alarms_clear();
    ADD(T0 + 1, 0, "^^PROG");
    ADD(T0 + 2, 0, "^COND");
    ADD(T0 + 3, 0, "MSG");
    ADD(T0 + 4, 0, "^^");
    ADD(T0 + 5, 0, "");
    int n;
    CHECK(alarm_kind(alarms_get(0)) == ALARM_CONTROL && !memcmp(alarm_label(alarms_get(0), &n), "PROG", 4) && n == 4, "control");
    CHECK(alarm_kind(alarms_get(1)) == ALARM_CONDITIONAL && !memcmp(alarm_label(alarms_get(1), &n), "COND", 4) && n == 4, "conditional");
    CHECK(alarm_kind(alarms_get(2)) == ALARM_MESSAGE, "message");
    alarm_label(alarms_get(3), &n);
    CHECK(alarm_kind(alarms_get(3)) == ALARM_CONTROL && n == 0, "control, current line");
    CHECK(alarm_kind(alarms_get(4)) == ALARM_MESSAGE && alarms_get(4)->length == 0, "empty is a message");
    CHECK(alarms_past_due_program() == -1, "none past due");
    alarms_mark_past_due(1);
    alarms_mark_past_due(2);
    CHECK(alarms_past_due_program() == 1, "ALMNOW's");
    CHECK(alarms_find_text("MSG", 3) == 2 && alarms_find_text("", 0) == 4 && alarms_find_text("X", 1) == -1, "find");
    alarms_remove(1);
    CHECK(alarms_count() == 4 && alarms_get(1)->text[0] == 'M', "remove");

    /* Full. */
    alarms_clear();
    for (int i = 0; i < ALARMS_MAX; i++)
        CHECK(ADD(T0 + 1 + (uint64_t) i, 0, "x"), "room for %d", i);
    CHECK(!ADD(T0 + 1, 0, "x"), "full");
    CHECK(alarms_count() == ALARMS_MAX, "full count");

    /* The file, back again, cut short at every length, and damaged. */
    alarms_clear();
    ADD(T0 + 5, 7000, "HELLO");
    ADD(T0 + 6, 0, "^^P");
    alarms_mark_past_due(0);
    size_t size;
    static unsigned char copy[sizeof(alarm_t) * ALARMS_MAX + 16];
    memcpy(copy, alarms_image(&size), size);
    CHECK(size <= sizeof(copy), "image");
    alarms_clear();
    CHECK(alarms_load(copy, size) && alarms_count() == 2 && alarms_get(0)->repeat_ms == 7000
          && alarms_get(0)->past_due && !memcmp(alarms_get(0)->text, "HELLO", 5) && !alarms_dirty(), "saved and read");
    for (size_t cut = 0; cut < size; cut++)
        CHECK(!alarms_load(copy, cut) && alarms_count() == 0, "cut at %zu", cut);
    unsigned char bad[sizeof(copy)];
    memcpy(bad, copy, size);
    bad[0] ^= 1;
    CHECK(!alarms_load(bad, size) && alarms_count() == 0, "bad magic");
    memcpy(bad, copy, size);
    bad[4] = 200;
    CHECK(!alarms_load(bad, size) && alarms_count() == 0, "bad count");
    memcpy(bad, copy, size);
    bad[8 + offsetof(alarm_t, length)] = 200;
    CHECK(!alarms_load(bad, size) && alarms_count() == 0, "bad length");
    for (size_t at = 0; at < size; at++)
        for (int v = 0; v < 256; v += 51) {
            memcpy(bad, copy, size);
            bad[at] = (unsigned char) v;
            alarms_load(bad, size);
            CHECK(alarms_count() <= ALARMS_MAX, "damaged at %zu", at);
        }
    alarms_clear();
}

static void test_alarm_numbers(void) {
    int64_t ms = -1, days = -1;
    CHECK(alarm_time_to_ms(1010000, false, &ms) && ms == 3600000 + 60000, "1.01");
    CHECK(alarm_time_to_ms(10000000, true, &ms) && ms == 22LL * 3600000, "-10 is 10 p.m.");
    CHECK(alarm_time_to_ms(23595900, false, &ms) && ms == 86399000, "23.5959");
    CHECK(!alarm_time_to_ms(24000000, false, &ms), "24");
    CHECK(!alarm_time_to_ms(12000000, true, &ms), "-12");
    CHECK(!alarm_time_to_ms(0, true, &ms), "-0");
    CHECK(!alarm_time_to_ms(1600000, false, &ms), "1.60");

    int64_t oct9 = time_days_from_civil(2026, 10, 9);
    CHECK(oct9 == 20735, "%lld", (long long) oct9);
    CHECK(alarm_date_to_days(10092026, TIME_MDY, &days) && days == oct9, "10.092026");
    CHECK(alarm_date_to_days(9102026, TIME_DMY, &days) && days == oct9, "9.102026");
    CHECK(alarm_date_to_days(2026100900, TIME_YMD, &days) && days == oct9, "2026.1009");
    CHECK(alarm_days_to_digits(oct9, TIME_MDY) == 10092026, "back, MDY");
    CHECK(alarm_days_to_digits(oct9, TIME_DMY) == 9102026, "back, DMY");
    CHECK(alarm_days_to_digits(oct9, TIME_YMD) == 2026100900, "back, YMD");
    CHECK(!alarm_date_to_days(2292026, TIME_MDY, &days), "Feb 29 2026");
    CHECK(alarm_date_to_days(2292028, TIME_MDY, &days), "Feb 29 2028");
    CHECK(!alarm_date_to_days(13012026, TIME_MDY, &days), "month 13");
    CHECK(!alarm_date_to_days(1011969, TIME_MDY, &days), "before 1970");
    CHECK(!alarm_date_to_days(1012200, TIME_MDY, &days), "after 2199");

    CHECK(time_ms_to_digits(3600000 + 2 * 60000 + 3000 + 40) == 1020304, "ms to digits");
    CHECK(time_ms_to_digits(9999ULL * 3600000) == 9999000000LL, "long repeat");

    rtc_datetime_t t;
    time_ms_to_datetime((uint64_t) oct9 * DAY + 15 * 3600000 + 43 * 60000 + 12345, &t);
    CHECK(t.year == 2026 && t.month == 10 && t.day == 9 && t.hour == 15 && t.min == 43 && t.sec == 12 && t.ms == 345
          && t.weekday == 5, "datetime");
}

/* The stopwatch, key by key: the manual's section 17. */
#define SW_REGS 5
static int64_t sw_reg[SW_REGS];
static bool sw_reg_bad[SW_REGS];
static int sw_size(void) { return SW_REGS; }
static stopwatch_reg_t sw_get(int n, int64_t *digits) {
    if (n >= SW_REGS)
        return STOPWATCH_REG_MISSING;
    if (sw_reg_bad[n])
        return STOPWATCH_REG_NOT_TIME;
    *digits = sw_reg[n];
    return STOPWATCH_REG_OK;
}
static bool sw_put(int n, int64_t digits) {
    if (n >= SW_REGS)
        return false;
    sw_reg[n] = digits;
    return true;
}
static const stopwatch_regs_t sw_regs = {sw_size, sw_get, sw_put};

static void check_sw(uint64_t now, const char *expected) {
    char text[STOPWATCH_TEXT_MAX];
    stopwatch_text(text, sizeof(text), now, &sw_regs);
    CHECK(!strcmp(text, expected), "stopwatch: \"%s\", not \"%s\"", text, expected);
}

#define SW_T0 1000000ULL

static int sw_tap(stopwatch_key_t key, uint64_t now) {
    int result = stopwatch_key_down(key, now, &sw_regs);
    return result | stopwatch_key_up(&sw_regs);
}

static void test_stopwatch(void) {
    uint64_t t = SW_T0;
    stopwatch_reset();
    memset(sw_reg, 0, sizeof(sw_reg));
    memset(sw_reg_bad, 0, sizeof(sw_reg_bad));
    stopwatch_keyboard_open(0, 0, false);
    check_sw(t, "00:00:00.00>R00");

    /* Hundredths only when it stands still. */
    sw_tap(STOPWATCH_KEY_RUN, t);
    check_sw(t + 12340, "00:00:12.3 >R00");
    CHECK(stopwatch_ticking(), "running: ticking");

    /* A split shows, frozen with the time going on, and its register; the
     * pointer moves when the key comes up. */
    CHECK(stopwatch_key_down(STOPWATCH_KEY_SPLIT, t + 12340, &sw_regs) == 0, "split");
    CHECK(!stopwatch_ticking(), "split: static");
    check_sw(t + 15000, "00:00:12.34>R00");
    CHECK(sw_reg[0] == 1234, "split stored: %lld", (long long) sw_reg[0]);
    stopwatch_key_up(&sw_regs);
    check_sw(t + 15000, "00:00:15.0 >R01");
    CHECK(stopwatch_ms(t + 15000) == 15000, "the split didn't stop it");

    /* Another key freezes it while it's down. */
    stopwatch_key_down(STOPWATCH_KEY_OTHER, t + 16000, &sw_regs);
    check_sw(t + 17000, "00:00:16.00>R01");
    stopwatch_key_up(&sw_regs);
    check_sw(t + 17000, "00:00:17.0 >R01");

    /* Delta: the split held shows the difference, the running display doesn't. */
    sw_tap(STOPWATCH_KEY_DELTA, t);
    check_sw(t + 20000, "00:00:20.0 >D01");
    stopwatch_key_down(STOPWATCH_KEY_SPLIT, t + 20000, &sw_regs);
    check_sw(t + 20000, "00:00:07.66>D01");
    CHECK(sw_reg[1] == 2000, "delta stores the plain split: %lld", (long long) sw_reg[1]);
    stopwatch_key_up(&sw_regs);

    /* CLEAR doesn't touch a running one. */
    sw_tap(STOPWATCH_KEY_CLEAR, t + 21000);
    check_sw(t + 21000, "00:00:21.0 >D02");

    /* Recall has its own pointer; the storing one waits. */
    sw_tap(STOPWATCH_KEY_DELTA, t);
    sw_tap(STOPWATCH_KEY_RECALL, t);
    check_sw(t + 22000, "00:00:12.34=R00");
    sw_tap(STOPWATCH_KEY_SST, t);
    check_sw(t + 22000, "00:00:20.00=R01");
    sw_tap(STOPWATCH_KEY_DELTA, t);
    check_sw(t + 22000, "00:00:07.66=D01");
    sw_tap(STOPWATCH_KEY_BST, t);
    check_sw(t + 22000, "00:00:12.34=D00"); /* no register before it: itself */
    sw_tap(STOPWATCH_KEY_DELTA, t);

    /* A later split not larger: an error in recalling. A value too big too. */
    sw_reg[2] = 1000;
    sw_reg[3] = 500;
    sw_tap((stopwatch_key_t) (STOPWATCH_KEY_0 + 0), t);
    check_sw(t + 22000, "00:00:12.34=R0_");
    sw_tap((stopwatch_key_t) (STOPWATCH_KEY_0 + 3), t);
    sw_tap(STOPWATCH_KEY_DELTA, t);
    check_sw(t + 22000, "ERROR =D03");
    sw_tap(STOPWATCH_KEY_DELTA, t);
    sw_reg[3] = 100000000; /* 100 hours */
    check_sw(t + 22000, "ERROR =R03");
    sw_reg_bad[3] = true;
    check_sw(t + 22000, "ERROR =R03");
    sw_reg_bad[3] = false;
    sw_reg[3] = 750000; /* minutes over 59 show */
    check_sw(t + 22000, "00:75:00.00=R03");

    /* The last register beeps, past it ends the keyboard in Recall. */
    CHECK(sw_tap(STOPWATCH_KEY_SST, t) == STOPWATCH_BEEP, "last register beeps");
    CHECK(sw_tap(STOPWATCH_KEY_BST, t) == 0, "BST doesn't");
    sw_tap(STOPWATCH_KEY_SST, t);
    CHECK(sw_tap(STOPWATCH_KEY_SST, t) == (STOPWATCH_LEAVE | STOPWATCH_NONEXISTENT), "recall past the end");

    /* R/S leaves Recall too, and stops; the storing pointer is where it was. */
    stopwatch_keyboard_open(2, 0, false);
    sw_tap(STOPWATCH_KEY_RECALL, t);
    sw_tap(STOPWATCH_KEY_RUN, t + 30000);
    CHECK(!stopwatch_running(), "stopped");
    check_sw(t + 99000, "00:00:30.00>R02");
    sw_tap(STOPWATCH_KEY_CLEAR, t + 99000);
    check_sw(t + 99000, "00:00:00.00>R02");

    /* Storing past the end ends the keyboard; the split held doesn't beep twice. */
    stopwatch_keyboard_open(4, 0, false);
    CHECK(stopwatch_key_down(STOPWATCH_KEY_SPLIT, t, &sw_regs) == 0, "last split is stored");
    CHECK(stopwatch_key_up(&sw_regs) == STOPWATCH_BEEP, "at the end: a beep");
    CHECK(stopwatch_key_down(STOPWATCH_KEY_SPLIT, t, &sw_regs) == (STOPWATCH_LEAVE | STOPWATCH_NONEXISTENT), "no register");

    /* Three digits, by Rnnn or by reaching 100; back to two drops the first. */
    stopwatch_keyboard_open(0, 0, false);
    sw_tap(STOPWATCH_KEY_RNNN, t);
    sw_tap((stopwatch_key_t) (STOPWATCH_KEY_0 + 1), t);
    check_sw(t, "00:00:00.00>R1__");
    sw_tap((stopwatch_key_t) (STOPWATCH_KEY_0 + 2), t);
    check_sw(t, "00:00:00.00>R12_");
    sw_tap((stopwatch_key_t) (STOPWATCH_KEY_0 + 3), t);
    check_sw(t, "00:00:00.00>R123");
    sw_tap(STOPWATCH_KEY_RNNN, t);
    check_sw(t, "00:00:00.00>R23");
    sw_tap(STOPWATCH_KEY_REG, t);
    check_sw(t, "00:00:00.00");
    sw_tap(STOPWATCH_KEY_REG, t);

    /* SWPT's X: a pointer half typed comes back as 0. */
    int store, recall;
    bool delta;
    stopwatch_keyboard_open(12, 34, true);
    stopwatch_pointers(&store, &recall, &delta);
    CHECK(store == 12 && recall == 34 && delta, "pointers");
    sw_tap((stopwatch_key_t) (STOPWATCH_KEY_0 + 5), t);
    stopwatch_pointers(&store, &recall, &delta);
    CHECK(store == 0 && recall == 34, "half typed: %d", store);

    /* It wraps after 99:59:59.99. */
    stopwatch_keyboard_open(0, 0, false);
    stopwatch_set(359999000, t);
    stopwatch_run(t);
    check_sw(t + 500, "99:59:59.5 >R00");
    check_sw(t + 1500, "00:00:00.5 >R00");
    stopwatch_stop(t + 1500);

    /* The timer: below zero it counts up, comes due once, and goes on. */
    stopwatch_set(-5000, t);
    CHECK(stopwatch_timer_at() == 0, "stopped: no timer");
    stopwatch_run(t);
    CHECK(stopwatch_timer_at() == t + 5000, "due at %llu", (unsigned long long) stopwatch_timer_at());
    check_sw(t + 1000, "-00:00:04.0 >R00");
    /* Setting the clock moves neither the time nor when it's due. */
    stopwatch_clock_moved(3600000);
    CHECK(stopwatch_ms(t + 3600000 + 1000) == -4000, "clock moved: %lld", (long long) stopwatch_ms(t + 3600000 + 1000));
    CHECK(stopwatch_timer_at() == t + 3600000 + 5000, "timer moved with the clock");
    stopwatch_clock_moved(-3600000);
    stopwatch_timer_done(t + 5200);
    CHECK(stopwatch_timer_at() == 0, "once only");
    CHECK(stopwatch_ms(t + 6200) == 1200, "goes on: %lld", (long long) stopwatch_ms(t + 6200));

    /* Through OFF: the image, and a damaged one refused. */
    stopwatch_set(1000, t);
    size_t size;
    char image[64];
    const void *saved = stopwatch_image(&size);
    CHECK(size <= sizeof(image), "image size");
    memcpy(image, saved, size);
    stopwatch_reset();
    CHECK(stopwatch_is_zero(), "reset");
    CHECK(!stopwatch_load(image, size - 1), "short image");
    image[0] ^= 1;
    CHECK(!stopwatch_load(image, size), "bad magic");
    CHECK(stopwatch_is_zero(), "bad image left it alone");
    image[0] ^= 1;
    CHECK(stopwatch_load(image, size), "image");
    CHECK(stopwatch_ms(t + 2000) == 3000, "runs on: %lld", (long long) stopwatch_ms(t + 2000));
    CHECK(!stopwatch_dirty(), "loaded: nothing to save");
    stopwatch_reset();
}

int main(void) {
    test_alarms();
    test_alarm_sounds();
    test_stopwatch();
    test_alarm_numbers();
    check_digits(0, 0);
    check_digits(1305200, 1 * 3600000 + 30 * 60000 + 52000);
    check_digits(30000001, 30LL * 3600000 + 10);
    check_digits(1000000, 3600000);
    check_digits(99995959LL * 100 + 99, 9999LL * 3600000 + 59 * 60000 + 59990);
    int64_t ms;
    CHECK(!time_digits_to_ms(1600000, &ms), "60 minutes");
    CHECK(!time_digits_to_ms(1006000, &ms), "60 seconds");

    /* SETAF's rules: the manual's appendix F. */
    CHECK(af_tenths_from_milli(99950) == 0, "99.95 clears it");
    CHECK(af_tenths_from_milli(500000) == 0, "500 clears it");
    CHECK(af_tenths_from_milli(99949) == 999, "99.949 is 99.9");
    CHECK(af_tenths_from_milli(1) == 1, "under 0.1 is 0.1");
    CHECK(af_tenths_from_milli(99) == 1, "under 0.1 is 0.1");
    CHECK(af_tenths_from_milli(100) == 1, "0.1");
    CHECK(af_tenths_from_milli(10500) == 105, "10.5");
    CHECK(af_tenths_from_milli(10549) == 105, "10.549 is 10.5");
    CHECK(af_tenths_from_milli(10550) == 106, "10.55 is 10.6");

    /* One pulse of 1/10240 s every 0.1 s: 84.375 s/day. */
    CHECK(af_tenths_to_trim(1) == 844, "%d", af_tenths_to_trim(1));
    CHECK(af_tenths_to_trim(-105) == -8, "%d", af_tenths_to_trim(-105));
    CHECK(af_tenths_to_trim(999) == 1, "%d", af_tenths_to_trim(999));
    CHECK(af_tenths_to_trim(0) == 0, "%d", af_tenths_to_trim(0));
    CHECK(trim_to_af_tenths(0) == 0, "%d", trim_to_af_tenths(0));
    CHECK(trim_to_af_tenths(844) == 1, "%d", trim_to_af_tenths(844));
    CHECK(trim_to_af_tenths(1) == 844, "%d", trim_to_af_tenths(1));
    CHECK(trim_to_af_tenths(-8) == -105, "%d", trim_to_af_tenths(-8));
    /* More than AF can say: still the fastest correction, not none. */
    CHECK(trim_to_af_tenths(9999) == 1, "%d", trim_to_af_tenths(9999));
    CHECK(trim_to_af_tenths(-9999) == -1, "%d", trim_to_af_tenths(-9999));

    /* The manual's own displays, section 15. */
    check_clock(false, false, RTC_MDY, "3:15:07 PM");
    check_clock(false, true, RTC_MDY, "15:15:07");
    check_clock(true, false, RTC_MDY, "3:15 PM 01/21");
    check_clock(true, true, RTC_MDY, "15:15 01/21");
    check_clock(true, true, RTC_DMY, "15:15 21.01");

    if (failures)
        return 1;
    printf("Time Module: all passed\n");
    return 0;
}

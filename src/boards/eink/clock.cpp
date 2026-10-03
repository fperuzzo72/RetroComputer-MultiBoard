/* clock.cpp - the board's real-time clock as the system's time of day.
 *
 * Both boards carry one on the touch panel's I2C bus (BoardConfig's sensors:
 * the PaperS3's BM8563 at 0x51), driven by freeink-sdk's Rtc. It keeps time
 * with the power off, **in UTC**: CrossPoint sets it that way (measured
 * 2026-10-03, three hours ahead of the Mac in Brasilia). At boot it is read
 * into the ESP32's clock, and the zone is set to Brasilia's, so localtime()
 * gives local time; the PC's BIOS asks localtime() for its INT 1Ah date and
 * its tick count, and DOS comes up with both.
 *
 * Before this, every machine started at the epoch and DOS dated its files
 * 1980.
 */
#include "clock.h"

#include <Arduino.h>
#include <Rtc.h>
#include <sys/time.h>
#include <time.h>

/* Brasilia, UTC-3, no summer time since 2019. POSIX spells offsets west
 * of Greenwich positive. */
#define LOCAL_TZ "<-03>3"

static Rtc rtc;
static bool have_rtc;

/* The chip's UTC into the system clock. mktime() reads a struct tm as
 * local time, so it runs with the zone at UTC for a moment. */
static bool to_system(const Rtc::DateTime &d)
{
    setenv("TZ", "UTC0", 1);
    tzset();
    struct tm t = {};
    t.tm_year = d.year - 1900;
    t.tm_mon = d.month - 1;
    t.tm_mday = d.day;
    t.tm_hour = d.hour;
    t.tm_min = d.minute;
    t.tm_sec = d.second;
    const time_t s = mktime(&t);
    setenv("TZ", LOCAL_TZ, 1);
    tzset();
    if (s < 0) return false;
    const struct timeval tv = { s, 0 };
    return settimeofday(&tv, nullptr) == 0;
}

static void report(const char *what)
{
    const time_t now = time(nullptr);
    struct tm l;
    localtime_r(&now, &l);
    struct tm u;
    gmtime_r(&now, &u);
    Serial.printf("clock: %s%04d-%02d-%02d %02d:%02d:%02d local (UTC-3), %02d:%02d UTC\n", what,
                  l.tm_year + 1900, l.tm_mon + 1, l.tm_mday, l.tm_hour, l.tm_min, l.tm_sec,
                  u.tm_hour, u.tm_min);
}

void clock_begin(void)
{
    setenv("TZ", LOCAL_TZ, 1);
    tzset();
    have_rtc = rtc.begin();
    if (!have_rtc) {
        Serial.println("clock: no real-time clock on this board");
        return;
    }
    Rtc::DateTime d;
    if (!rtc.now(d)) {
        Serial.println("clock: the real-time clock has stopped or was never set; `rtc YYYY-MM-DD HH:MM:SS` sets it");
        return;
    }
    if (to_system(d)) report("from the real-time clock, ");
}

void clock_command(const char *args, SemaphoreHandle_t i2c_lock)
{
    if (!have_rtc) { Serial.println("clock: no real-time clock on this board"); return; }
    unsigned y, mo, da, h, mi, s;
    Rtc::DateTime d;
    if (i2c_lock) xSemaphoreTake(i2c_lock, portMAX_DELAY);
    bool ok;
    if (sscanf(args, "%u-%u-%u %u:%u:%u", &y, &mo, &da, &h, &mi, &s) == 6) {
        /* given in local time, kept in UTC */
        struct tm t = {};
        t.tm_year = (int)y - 1900; t.tm_mon = (int)mo - 1; t.tm_mday = (int)da;
        t.tm_hour = (int)h; t.tm_min = (int)mi; t.tm_sec = (int)s;
        const time_t when = mktime(&t);
        struct tm u;
        gmtime_r(&when, &u);
        d.year = (uint16_t)(u.tm_year + 1900); d.month = (uint8_t)(u.tm_mon + 1);
        d.day = (uint8_t)u.tm_mday; d.hour = (uint8_t)u.tm_hour; d.minute = (uint8_t)u.tm_min;
        d.second = (uint8_t)u.tm_sec; d.weekday = (uint8_t)u.tm_wday;
        ok = rtc.set(d) && rtc.now(d);
        if (ok) to_system(d);
    } else {
        ok = rtc.now(d);
        if (ok) to_system(d);
    }
    if (i2c_lock) xSemaphoreGive(i2c_lock);
    if (ok) report("");
    else Serial.println("clock: the real-time clock did not answer, or has stopped");
}

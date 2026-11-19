/* made by medhatk – the date and the time that psp5 shows (see datetime.h) */
#include <psprtc.h>
#include <psputility.h>
#include <stdio.h>
#include <string.h>
#include "datetime.h"

unsigned char g_dt_dst = DT_DST_KEEP;
unsigned char g_dt_date = DT_DATE_YMD;
unsigned char g_dt_time = DT_TIME_24;

#define FIRST_YEAR 1980
#define LAST_YEAR  2043 /* the year has six bits in a packed time */

/* Lokal tid med reservväg för PSP-1000 / ARK-4 när sceRtcGetCurrentClockLocalTime misslyckas. */
static int read_local_clock(ScePspDateTime *now)
{
    if (sceRtcGetCurrentClockLocalTime(now) >= 0)
        return 1;
    int tz = 0, dst = 0;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_TIMEZONE, &tz);
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_DAYLIGHTSAVINGS, &dst);
    if (sceRtcGetCurrentClock(now, tz + (dst ? 60 : 0)) >= 0)
        return 1;
    u64 tick;
    return sceRtcGetCurrentTick(&tick) >= 0 && sceRtcSetTick(now, &tick) >= 0;
}

u32 dt_pack(int year, int month, int day, int hour, int minute, int second)
{
    if (year < FIRST_YEAR)
        year = FIRST_YEAR;
    if (year > LAST_YEAR)
        year = LAST_YEAR;
    return ((u32)(year - FIRST_YEAR) << 26) | ((u32)(month & 15) << 22) | ((u32)(day & 31) << 17) | ((u32)(hour & 31) << 12) | ((u32)(minute & 63) << 6) | (u32)(second & 63);
}

void dt_unpack(u32 t, int *year, int *month, int *day, int *hour, int *minute, int *second)
{
    if (year)
        *year = (int)(t >> 26) + FIRST_YEAR;
    if (month)
        *month = (int)((t >> 22) & 15);
    if (day)
        *day = (int)((t >> 17) & 31);
    if (hour)
        *hour = (int)((t >> 12) & 31);
    if (minute)
        *minute = (int)((t >> 6) & 63);
    if (second)
        *second = (int)(t & 63);
}

/* Days since 1970-01-01 of a date, and back (the proleptic Gregorian calendar). */
static long long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    long long era = (y >= 0 ? y : y - 399) / 400;
    long long yoe = y - era * 400;
    long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void civil_from_days(long long z, int *y, int *m, int *d)
{
    z += 719468;
    long long era = (z >= 0 ? z : z - 146096) / 146097;
    long long doe = z - era * 146097;
    long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    long long mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

/* the minutes from 1970-01-01 00:00 to the time (the seconds do not count) */
static long long minutes_of(u32 t)
{
    int y, mo, d, h, mi;
    dt_unpack(t, &y, &mo, &d, &h, &mi, NULL);
    if (mo < 1) mo = 1;
    if (mo > 12) mo = 12;
    if (d < 1) d = 1;
    if (d > 31) d = 31;
    if (h > 23) h = 23;
    if (mi > 59) mi = 59;
    return days_from_civil(y, mo, d) * 1440 + h * 60 + mi;
}

static int dst_minutes(void)
{
    return g_dt_dst == DT_DST_REMOVE ? -60 : g_dt_dst == DT_DST_ADD ? 60 : 0;
}

int dt_shift_minutes(void)
{
    return dst_minutes();
}

u32 dt_shift(u32 t, int minutes)
{
    if (!t || !minutes)
        return t;
    long long total = minutes_of(t) + minutes;
    const long long lo = days_from_civil(FIRST_YEAR, 1, 1) * 1440;
    const long long hi = days_from_civil(LAST_YEAR, 12, 31) * 1440 + 23 * 60 + 59;
    if (total < lo)
        total = lo;
    if (total > hi)
        total = hi;
    long long days = total / 1440, rest = total % 1440; /* (total is above 0: the first minute of 1980 is) */
    int y, mo, d, second = (int)(t & 63);
    civil_from_days(days, &y, &mo, &d);
    return dt_pack(y, mo, d, (int)(rest / 60), (int)(rest % 60), second > 59 ? 59 : second);
}

u32 dt_shown(u32 t)
{
    return dt_shift(t, dt_shift_minutes());
}

u32 dt_psp_now(void)
{
    ScePspDateTime now;
    if (!read_local_clock(&now))
        return 0;
    if (now.year < FIRST_YEAR || now.year > LAST_YEAR || now.month < 1 || now.month > 12 || now.day < 1 || now.day > 31)
        return 0;
    return dt_pack(now.year, now.month, now.day, now.hour, now.minute, now.second);
}

u32 dt_now(void)
{
    return dt_shown(dt_psp_now());
}

/* --- texts --- */

void dt_date_text(char *out, int size, u32 shown)
{
    int y, mo, d;
    if (!shown) {
        out[0] = 0;
        return;
    }
    dt_unpack(shown, &y, &mo, &d, NULL, NULL, NULL);
    if (g_dt_date == DT_DATE_MDY)
        snprintf(out, size, "%02d.%02d.%04d", mo, d, y);
    else if (g_dt_date == DT_DATE_DMY)
        snprintf(out, size, "%02d.%02d.%04d", d, mo, y);
    else
        snprintf(out, size, "%04d.%02d.%02d", y, mo, d);
}

/* the time of day: with the 12-hour clock "2:05 PM" (12:05 AM after midnight, 12:05 PM at noon), else "14:05" */
static void time_of_day(char *out, int size, int hour, int minute)
{
    if (g_dt_time == DT_TIME_12)
        snprintf(out, size, "%d:%02d %s", hour % 12 ? hour % 12 : 12, minute, hour < 12 ? "AM" : "PM");
    else
        snprintf(out, size, "%02d:%02d", hour, minute);
}

void dt_time_text(char *out, int size, u32 shown)
{
    int h, mi;
    if (!shown) {
        out[0] = 0;
        return;
    }
    dt_unpack(shown, NULL, NULL, NULL, &h, &mi, NULL);
    time_of_day(out, size, h, mi);
}

void dt_text(char *out, int size, u32 shown)
{
    char date[16], time[16];
    if (!shown) {
        out[0] = 0;
        return;
    }
    dt_date_text(date, sizeof(date), shown);
    dt_time_text(time, sizeof(time), shown);
    snprintf(out, size, "%s %s", date, time);
}

void dt_text_stored(char *out, int size, u32 stored)
{
    dt_text(out, size, dt_shown(stored));
}

void dt_clock_text(char *out, int size)
{
    ScePspDateTime now;
    out[0] = 0;
    if (!read_local_clock(&now))
        return;
    /* only the time of day is wanted, so the date of the PSP does not matter (an RTC that says another year than 1980 to 2043 still gives a clock) */
    int minutes = (now.hour % 24) * 60 + now.minute % 60 + dt_shift_minutes();
    minutes %= 1440;
    if (minutes < 0)
        minutes += 1440;
    time_of_day(out, size, minutes / 60, minutes % 60);
}

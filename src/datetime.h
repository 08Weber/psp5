/* made by medhatk – the date and the time that psp5 shows */
#ifndef DATETIME_H
#define DATETIME_H

#include <psptypes.h>

/* The date and the time that psp5 shows (the clock at the top, Date & Time in Settings) are the PSP's own, with what the player
 * chose in Settings -> System -> Date and Time: Daylight Saving (one hour less, as it is, one hour more), the date format and the time format.
 * This only changes what psp5 SHOWS: the PSP's clock and its settings are never written.
 *
 * A time here is a packed local time: (year - 1980) << 26 | month << 22 | day << 17 | hour << 12 |
 * minute << 6 | second, so that a later time is a bigger number. 0 means "no time". The years that fit are 1980 to 2043. */

enum { DT_DST_REMOVE, DT_DST_KEEP, DT_DST_ADD };  /* Daylight Saving: one hour less, as it is, one hour more */
enum { DT_DATE_YMD, DT_DATE_MDY, DT_DATE_DMY };   /* Date Format: YYYY.MM.DD, MM.DD.YYYY, DD.MM.YYYY */
enum { DT_TIME_24, DT_TIME_12 };                  /* Time Format: 24-Hour Clock, 12-Hour Clock */

extern unsigned char g_dt_dst;   /* the default is DT_DST_KEEP */
extern unsigned char g_dt_date;  /* the default is DT_DATE_YMD */
extern unsigned char g_dt_time;  /* the default is DT_TIME_24 */

/* How many minutes the shown time is from the PSP's: -60, 0 or 60 (Daylight Saving). */
int dt_shift_minutes(void);

/* `t` moved by `minutes` (0 = no time stays 0; before 1980 or after 2043 stays at the first or last minute; the seconds stay). */
u32 dt_shift(u32 t, int minutes);

/* The time as psp5 shows it: `t` moved by dt_shift_minutes(). With the standard settings it is `t` itself, whatever it is. */
u32 dt_shown(u32 t);

/* The PSP's own clock (local time) as a packed time, 0 when it cannot be read or is not a date that fits. Not moved. */
u32 dt_psp_now(void);

/* The time that psp5 shows now: dt_shown(dt_psp_now()). */
u32 dt_now(void);

/* Texts. "2026.10.28", "10.28.2026" or "28.10.2026"; "14:05" or "2:05 PM"; the two with a space between. An empty text for 0. */
void dt_date_text(char *out, int size, u32 shown);
void dt_time_text(char *out, int size, u32 shown);
void dt_text(char *out, int size, u32 shown);
void dt_text_stored(char *out, int size, u32 stored); /* dt_text(dt_shown(stored)) */
void dt_clock_text(char *out, int size);              /* the time of day now, for the top bar: "17:54" or "5:54 PM"; empty when the clock cannot be read */

/* Packing, for whoever needs a time. */
u32 dt_pack(int year, int month, int day, int hour, int minute, int second);
void dt_unpack(u32 t, int *year, int *month, int *day, int *hour, int *minute, int *second);

#endif

/* psp5 authentic – one log for each part of the program: PSP5/logs/Music.log, Video.log, Picture.log, File Manager.log, FTP.log and System.log.
 *
 * What goes in them is what went wrong: a failure (a file that cannot be played or shown, a copy that stopped, a decoder that refused a stream ...) and a stop (the PSP crashed, froze or lost power
 * while a step that is known to be able to do that was running). Nothing is written while all goes well, and what is written is cheap: the steps leading up to a failure are kept in memory (logs_note)
 * and only join the entry when one happens, an entry is written by a thread of its own (the caller does not wait for the card), and a log never grows past LOGS_FILE_MAX: the oldest entries are dropped.
 * No file name or other personal thing is written: sizes, kinds and the numbers of the libraries are.
 *
 * A stop leaves nothing to write with, so a step that can stop the PSP says so before it begins (logs_step): a small file <Area>.run is put on the card with the step's name and the notes so far, and
 * taken away when the step has ended well (logs_done). When psp5 starts and finds one, it turns it into an entry ("the PSP stopped while: ...") and remembers the step (logs_stopped). */
#ifndef LOGS_H
#define LOGS_H

typedef enum { LOG_MUSIC, LOG_VIDEO, LOG_PICTURE, LOG_FILES, LOG_FTP, LOG_SYSTEM, LOG_AREAS } LogArea;

#define LOGS_FILE_MAX 16384 /* bytes: a log that would grow past this loses its oldest entries (the newest LOGS_KEEP bytes stay) */
#define LOGS_KEEP 8192
#define LOGS_PER_RUN 12     /* entries of one area in one run of psp5: more are not written (a failure that repeats in a loop must not fill the card or slow the PSP) */

/* once at the start: makes the folder, turns the .run files of an earlier run into entries and starts the thread that writes */
void logs_init(void);

/* a step in memory (no card, no thread, any number of threads): what the entry of a failure shows after it as "steps before it". Short, one line. */
void logs_note(LogArea a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* something went wrong: an entry (the date, the area, the message, the notes since the last entry) is put in the area's file by the thread. The same message twice in a row is written once. */
void logs_fail(LogArea a, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* a step that has stopped a PSP is about to begin: when this returns, the card has the marker (logs_step waits for it; logs_step_later does not and is for a refresh while it goes on) */
void logs_step(LogArea a, const char *what);
void logs_step_later(LogArea a, const char *what);

/* the step ended well: the marker is taken away (by the thread) */
void logs_done(LogArea a);

/* waits (3 s at most) until everything given to the thread is on the card: before psp5 ends or a game starts */
void logs_flush(void);

/* the step an earlier run of psp5 was in when the PSP stopped, as logs_step was given it, or "" (known from logs_init on; logs_forget_stop makes it "") */
const char *logs_stopped(LogArea a);
void logs_forget_stop(LogArea a);

/* the file of an area, for the tests and the docs ("File Manager" for LOG_FILES) */
const char *logs_name(LogArea a);

#endif

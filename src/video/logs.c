/* psp5 authentic – one log for each part of the program (see logs.h) */
#include "logs.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pspkernel.h>
#include <psprtc.h>

#include "system.h"

#define NOTES 36       /* steps kept in memory for each area */
#define NOTE_LEN 80
#define ENTRY_MAX 2200 /* an entry or a marker: the date, the message and the steps before it */
#define QUEUE 8

static const char *const NAME[LOG_AREAS] = { "Music", "Video", "Picture", "File Manager", "FTP", "System" };

/* The steps in memory. Any thread may write one (a note is a line of 80 characters at most: two threads at the very same moment can garble a line of the trail, never more, and no atomic instruction is
 * used: `head` and `clear` are only ever moved up). */
static char s_notes[LOG_AREAS][NOTES][NOTE_LEN];
static volatile unsigned s_head[LOG_AREAS]; /* notes made */
static volatile unsigned s_clear[LOG_AREAS]; /* the notes before this one went into an entry already */
static char s_stopped[LOG_AREAS][64];
static unsigned s_count[LOG_AREAS];    /* entries made in this run */
static unsigned s_lasthash[LOG_AREAS]; /* of the last message (0: none yet) */

/* What the thread has to do, in the order it was given. Several threads give (a semaphore keeps the places), one takes. */
enum { J_ENTRY, J_MARK, J_UNMARK };
typedef struct {
    unsigned char kind, area;
    char text[ENTRY_MAX];
} Job;
static Job s_q[QUEUE];
static volatile unsigned s_qhead, s_qtail; /* `head`: given, `tail`: done (moved by the thread after the card has it) */
static SceUID s_sema = -1, s_thread = -1;

static void dir_path(char *out, int n) { snprintf(out, (size_t)n, "%s/logs", PSP5_DATA_DIR); }
static void file_path(int a, const char *ext, char *out, int n) { snprintf(out, (size_t)n, "%s/logs/%s.%s", PSP5_DATA_DIR, NAME[a], ext); }

const char *logs_name(LogArea a) { return a >= 0 && a < LOG_AREAS ? NAME[a] : ""; }
const char *logs_stopped(LogArea a) { return a >= 0 && a < LOG_AREAS ? s_stopped[a] : ""; }
void logs_forget_stop(LogArea a)
{
    if (a >= 0 && a < LOG_AREAS)
        s_stopped[a][0] = 0;
}

static unsigned hash(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; s++)
        h = (h ^ (unsigned char)*s) * 16777619u;
    return h ? h : 1;
}

/* ---------------------------------------------------------------------- */
/* the steps in memory */

void logs_note(LogArea a, const char *fmt, ...)
{
    if (a < 0 || a >= LOG_AREAS)
        return;
    unsigned h = s_head[a];
    char *slot = s_notes[a][h % NOTES];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(slot, NOTE_LEN, fmt, ap);
    va_end(ap);
    slot[NOTE_LEN - 1] = 0;
    s_head[a] = h + 1;
}

/* the steps since the last entry, oldest first, each on a line of its own and two spaces in */
static int notes_text(int a, char *out, int cap)
{
    unsigned h = s_head[a], from = s_clear[a];
    if (h - from > NOTES)
        from = h - NOTES;
    int n = 0;
    for (unsigned i = from; i != h && n < cap - 2; i++) {
        char line[NOTE_LEN];
        memcpy(line, s_notes[a][i % NOTES], NOTE_LEN);
        line[NOTE_LEN - 1] = 0;
        n += snprintf(out + n, (size_t)(cap - n), "  %s\n", line);
        if (n >= cap)
            n = cap - 1;
    }
    return n;
}

static void stamp(char *out, int n)
{
    ScePspDateTime t;
    if (sceRtcGetCurrentClockLocalTime(&t) == 0)
        snprintf(out, (size_t)n, "%04d-%02d-%02d %02d:%02d", t.year, t.month, t.day, t.hour, t.minute);
    else
        snprintf(out, (size_t)n, "date unknown");
}

/* an entry: a line that starts with "=== " (the date and the area), the message, and the steps before it */
static int compose(int a, const char *msg, char *out, int cap)
{
    char when[24];
    stamp(when, sizeof(when));
    int n = snprintf(out, (size_t)cap, "=== %s | %s ===\n%s\n", when, NAME[a], msg);
    if (n >= cap)
        n = cap - 1;
    char steps[ENTRY_MAX];
    int m = notes_text(a, steps, (int)sizeof(steps));
    if (m > 0 && n < cap - 24) {
        n += snprintf(out + n, (size_t)(cap - n), "steps before it:\n");
        if (n >= cap)
            n = cap - 1;
        int room = cap - n - 2;
        const char *p = steps;
        if (m > room) { /* the oldest steps go first (a whole line is cut at a time) */
            p = steps + (m - room);
            const char *nl = strchr(p, '\n');
            p = nl ? nl + 1 : p + (m - (int)(p - steps));
        }
        int len = (int)strlen(p);
        if (len > room)
            len = room;
        memcpy(out + n, p, (size_t)len);
        n += len;
    }
    if (n < cap - 1)
        out[n++] = '\n';
    out[n] = 0;
    return n;
}

/* ---------------------------------------------------------------------- */
/* the card (the thread's, or the caller's when there is no thread) */

/* a file that has grown past LOGS_FILE_MAX keeps its newest LOGS_KEEP bytes, from the first entry that starts in them */
static void trim(const char *path, int size)
{
    int keep = size < LOGS_KEEP ? size : LOGS_KEEP;
    char *buf = malloc((size_t)keep + 1);
    if (!buf)
        return;
    int n = 0;
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd >= 0) {
        sceIoLseek32(fd, size - keep, PSP_SEEK_SET);
        n = sceIoRead(fd, buf, (unsigned)keep);
        sceIoClose(fd);
    }
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    const char *start = NULL;
    if (n > 0) {
        buf[n] = 0;
        const char *p = strstr(buf, "\n=== ");
        if (p)
            start = p + 1;
    }
    int len = start ? n - (int)(start - buf) : 0;
    SceUID out = sceIoOpen(tmp, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (out >= 0) {
        if (len > 0)
            sceIoWrite(out, start, (unsigned)len);
        sceIoClose(out);
        sceIoRemove(path);
        if (sceIoRename(tmp, path) < 0)
            sceIoRemove(tmp);
    }
    free(buf);
}

static void append_entry(int a, const char *text)
{
    char path[160];
    file_path(a, "log", path, sizeof(path));
    int len = (int)strlen(text);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
    if (fd < 0) {
        char dir[96];
        dir_path(dir, sizeof(dir));
        sceIoMkdir(dir, 0777); /* (somebody took the folder away) */
        fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
        if (fd < 0)
            return;
    }
    int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    if (size > 0 && size + len > LOGS_FILE_MAX) {
        sceIoClose(fd);
        trim(path, size);
        fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
        if (fd < 0)
            return;
    }
    sceIoWrite(fd, text, (unsigned)len);
    sceIoClose(fd);
}

static void run_job(int kind, int a, const char *text)
{
    char path[160];
    if (kind == J_ENTRY) {
        append_entry(a, text);
    } else if (kind == J_MARK) {
        file_path(a, "run", path, sizeof(path));
        SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
        if (fd >= 0) {
            sceIoWrite(fd, text, (unsigned)strlen(text));
            sceIoClose(fd);
        }
    } else {
        file_path(a, "run", path, sizeof(path));
        sceIoRemove(path);
    }
}

static int writer(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    for (;;) {
        while (s_qtail != s_qhead) {
            Job *j = &s_q[s_qtail % QUEUE]; /* (the place is not given again before the tail has moved) */
            run_job(j->kind, j->area, j->text);
            __sync_synchronize();
            s_qtail++;
        }
        sceKernelSleepThread(); /* (a wake-up that came before this is counted: nothing is lost) */
    }
    return 0;
}

/* gives the thread a job; 0 when there is no place (the job is dropped: a log is not worth a wait) */
static int give(int kind, int a, const char *text)
{
    if (s_thread < 0) { /* no thread: here, as it would have been done */
        run_job(kind, a, text);
        return 1;
    }
    sceKernelWaitSema(s_sema, 1, NULL);
    int ok = s_qhead - s_qtail < QUEUE;
    if (ok) {
        Job *j = &s_q[s_qhead % QUEUE];
        j->kind = (unsigned char)kind;
        j->area = (unsigned char)a;
        snprintf(j->text, sizeof(j->text), "%s", text);
        __sync_synchronize();
        s_qhead++;
    }
    sceKernelSignalSema(s_sema, 1);
    if (ok)
        sceKernelWakeupThread(s_thread);
    return ok;
}

void logs_flush(void)
{
    for (int i = 0; i < 3000 && s_qtail != s_qhead; i++) /* (3 s at most: the thread is alive, it only waits for the card) */
        sceKernelDelayThread(1000);
}

/* ---------------------------------------------------------------------- */

void logs_fail(LogArea a, const char *fmt, ...)
{
    if (a < 0 || a >= LOG_AREAS)
        return;
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    unsigned h = hash(msg);
    if (h == s_lasthash[a] || s_count[a] >= LOGS_PER_RUN) /* (the same again, or too many for one run) */
        return;
    char text[ENTRY_MAX];
    compose(a, msg, text, (int)sizeof(text));
    if (!give(J_ENTRY, a, text)) /* (no place in the queue: nothing was written and nothing is counted) */
        return;
    s_lasthash[a] = h;
    s_count[a]++;
    s_clear[a] = s_head[a]; /* (the steps are in the entry now: the next one starts from here) */
}

static void marker_text(int a, const char *what, char *out, int cap)
{
    int n = snprintf(out, (size_t)cap, "%s\n", what);
    if (n >= cap)
        n = cap - 1;
    notes_text(a, out + n, cap - n);
}

void logs_step(LogArea a, const char *what)
{
    if (a < 0 || a >= LOG_AREAS)
        return;
    char text[ENTRY_MAX];
    marker_text(a, what, text, (int)sizeof(text));
    if (give(J_MARK, a, text))
        logs_flush();
}

void logs_step_later(LogArea a, const char *what)
{
    if (a < 0 || a >= LOG_AREAS)
        return;
    char text[ENTRY_MAX];
    marker_text(a, what, text, (int)sizeof(text));
    give(J_MARK, a, text);
}

void logs_done(LogArea a)
{
    if (a < 0 || a >= LOG_AREAS)
        return;
    give(J_UNMARK, a, "");
}

/* the markers of a run that did not end: an entry each */
static void recover(int a)
{
    char path[160];
    file_path(a, "run", path, sizeof(path));
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return;
    char buf[ENTRY_MAX];
    int n = sceIoRead(fd, buf, sizeof(buf) - 1);
    sceIoClose(fd);
    sceIoRemove(path);
    if (n <= 0)
        return;
    buf[n] = 0;
    char *nl = strchr(buf, '\n');
    const char *steps = "";
    if (nl) {
        *nl = 0;
        steps = nl + 1;
    }
    snprintf(s_stopped[a], sizeof(s_stopped[a]), "%s", buf);
    char msg[ENTRY_MAX];
    int m = snprintf(msg, sizeof(msg), "The PSP stopped (crashed, froze or lost power) while: %s\n", buf);
    if (*steps && m < (int)sizeof(msg) - 24)
        snprintf(msg + m, sizeof(msg) - (size_t)m, "steps before it:\n%s", steps);
    char when[24];
    stamp(when, sizeof(when));
    char text[ENTRY_MAX + 80];
    snprintf(text, sizeof(text), "=== %s | %s ===\n%s\n", when, NAME[a], msg);
    append_entry(a, text);
}

void logs_init(void)
{
    char dir[96];
    dir_path(dir, sizeof(dir));
    sceIoMkdir(dir, 0777); /* (it is there after the first start: the call then fails at once) */
    for (int a = 0; a < LOG_AREAS; a++)
        recover(a);
    if (s_thread >= 0) /* (called again: the thread is there) */
        return;
    s_sema = sceKernelCreateSema("psp5_logs", 0, 1, 1, NULL);
    if (s_sema < 0)
        return;
    SceUID th = sceKernelCreateThread("psp5_logs", writer, 0x26, 0x2000, 0, NULL);
    if (th >= 0 && sceKernelStartThread(th, 0, NULL) >= 0)
        s_thread = th;
    else if (th >= 0)
        sceKernelDeleteThread(th);
}

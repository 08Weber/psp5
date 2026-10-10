/* Läser/skriver ARK:s SETTINGS.TXT. Logiken följer arkMenu så att filen blir
 * exakt som ARK förväntar sig. Rader som inte känns igen (kommentarer,
 * spelspecifika rader, okända nycklar) behålls oförändrade. */
#include <pspkernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stddef.h>
#include "arkcfg.h"

#define MAX_EXTRA 64
#define LINE_LEN 160

CfwConf g_cfw;
static char s_extra[MAX_EXTRA][LINE_LEN];
static int s_extra_count;

static const char *CLOCK_KEYS[CLOCK_COUNT] = { "", "133", "222", "333", "383", "403", "423", "443" };

static void set_defaults(void)
{
    /* samma som arkMenu: resetCfwSettings() */
    memset(&g_cfw, 0, sizeof(g_cfw));
    g_cfw.usbcharge = 1;
    g_cfw.clock_game = g_cfw.clock_vsh = CLOCK_333;
    g_cfw.highmem = 2;
    g_cfw.wpa2 = 1;
    g_cfw.mscache = 1;
    g_cfw.infernocache = 1;
    g_cfw.oldplugin = 1;
    g_cfw.hibblock = 1;
    g_cfw.hidemac = 1;
    g_cfw.hidedlc = 1;
    g_cfw.vitamute = 1;
    g_cfw.qaflags = 1;
    s_extra_count = 0;
}

static void keep_line(const char *line)
{
    if (s_extra_count < MAX_EXTRA)
        snprintf(s_extra[s_extra_count++], LINE_LEN, "%s", line);
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

static int runlevel_value(const char *rl, const char *en)
{
    int on = !strcasecmp(en, "on") || !strcmp(en, "1") || !strcasecmp(en, "enabled") || !strcasecmp(en, "true");
    int level;
    if (!strcasecmp(rl, "always") || !strcasecmp(rl, "all")) level = CFG_ALWAYS;
    else if (!strcasecmp(rl, "game")) level = CFG_GAME;
    else if (!strcasecmp(rl, "umd") || !strcasecmp(rl, "psp")) level = CFG_UMD;
    else if (!strcasecmp(rl, "homebrew")) level = CFG_HOMEBREW;
    else if (!strcasecmp(rl, "pops") || !strcasecmp(rl, "psx") || !strcasecmp(rl, "ps1")) level = CFG_POPS;
    else if (!strcasecmp(rl, "vsh") || !strcasecmp(rl, "xmb")) level = CFG_VSH;
    else if (!strcasecmp(rl, "launcher")) level = CFG_LAUNCHER;
    else return -1; /* t.ex. ett spel-ID – behålls som egen rad */
    return on ? level : CFG_OFF;
}

static void set_clock(int level, int value)
{
    if (level == CFG_VSH) {
        g_cfw.clock_vsh = value;
    } else if (level == CFG_ALWAYS) {
        g_cfw.clock_game = g_cfw.clock_vsh = value;
    } else if (level != CFG_OFF) {
        g_cfw.clock_game = value;
    }
}

/* nyckel med valfritt ":tillägg" → pekare till variabeln, eller NULL */
static unsigned char *bool_key(const char *k)
{
    static const struct { const char *name; size_t off; } keys[] = {
        { "usbcharge", offsetof(CfwConf, usbcharge) },     { "wpa2", offsetof(CfwConf, wpa2) },
        { "launcher", offsetof(CfwConf, launcher) },       { "disablepause", offsetof(CfwConf, disablepause) },
        { "oldplugin", offsetof(CfwConf, oldplugin) },     { "hibblock", offsetof(CfwConf, hibblock) },
        { "hidemac", offsetof(CfwConf, hidemac) },         { "hidedlc", offsetof(CfwConf, hidedlc) },
        { "noled", offsetof(CfwConf, noled) },             { "noumd", offsetof(CfwConf, noumd) },
        { "deadef", offsetof(CfwConf, deadef) },           { "noanalog", offsetof(CfwConf, noanalog) },
        { "vitamute", offsetof(CfwConf, vitamute) },       { "qaflags", offsetof(CfwConf, qaflags) },
    };
    for (unsigned i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (!strcasecmp(k, keys[i].name))
            return (unsigned char *)&g_cfw + keys[i].off;
    return NULL;
}

/* Tolkar en "runlevel, nyckel, on/off"-rad. Returnerar 0 om raden ska behållas som den är. */
static int parse_setting(const char *rl, const char *key, const char *en)
{
    int level = runlevel_value(rl, en);
    if (level < 0)
        return 0;
    int on = level != CFG_OFF;
    const char *opt = strchr(key, ':');
    opt = opt ? opt + 1 : "";
    unsigned char *b;

    if ((b = bool_key(key))) {
        *b = level;
    } else if (!strncasecmp(key, "cpuclock", 8)) {
        for (int c = 1; c < CLOCK_COUNT; c++)
            if (!strcasecmp(opt, CLOCK_KEYS[c]))
                set_clock(level, c);
    } else if (!strcasecmp(key, "overclock")) {
        set_clock(level, CLOCK_333);
    } else if (!strcasecmp(key, "powersave")) {
        set_clock(level, CLOCK_133);
    } else if (!strcasecmp(key, "defaultclock")) {
        set_clock(level, CLOCK_222);
    } else if (!strncasecmp(key, "highmem", 7)) {
        g_cfw.highmem = !on ? 0 : !strcasecmp(opt, "auto") ? 2 : !strcasecmp(opt, "force16") ? 3
                      : !strcasecmp(opt, "forcemax") ? 4 : 1;
    } else if (!strncasecmp(key, "mscache", 7)) {
        g_cfw.mscache = !on ? 0 : !strcasecmp(opt, "8k") ? 2 : !strcasecmp(opt, "16k") ? 3 : 1;
    } else if (!strncasecmp(key, "infernocache", 12)) {
        g_cfw.infernocache = !on ? 0 : !strcasecmp(opt, "rr") ? 2 : 1;
    } else if (!strncasecmp(key, "skiplogos", 9)) {
        g_cfw.skiplogos = !on ? 0 : !strcasecmp(opt, "gameboot") ? 2 : !strcasecmp(opt, "coldboot") ? 3 : 1;
    } else if (!strncasecmp(key, "hidepics", 8)) {
        g_cfw.hidepics = !on ? 0 : !strcasecmp(opt, "pic0") ? 2 : !strcasecmp(opt, "pic1") ? 3 : 1;
    } else if (!strncasecmp(key, "region_", 7)) {
        if (on)
            g_cfw.regionchange = !strcasecmp(key + 7, "jp") ? 1 : !strcasecmp(key + 7, "us") ? 2
                               : !strcasecmp(key + 7, "eu") ? 3 : 0;
    } else if (!strncasecmp(key, "fakeregion_", 11)) {
        if (on)
            g_cfw.vshregion = atoi(key + 11);
    } else {
        return 0; /* okänd nyckel – behåll raden */
    }
    return 1;
}

int arkcfg_load(const char *path)
{
    set_defaults();
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd < 0)
        return 0;
    int size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    sceIoLseek32(fd, 0, PSP_SEEK_SET);
    if (size <= 0 || size > 64 * 1024) {
        sceIoClose(fd);
        return 0;
    }
    char *buf = malloc(size + 1);
    if (!buf) {
        sceIoClose(fd);
        return 0;
    }
    buf[sceIoRead(fd, buf, size) > 0 ? size : 0] = 0;
    sceIoClose(fd);

    /* filen finns: börja från noll som arkMenu gör (inte från standardvärden) */
    memset(&g_cfw, 0, sizeof(g_cfw));
    char *lsave = NULL;
    for (char *line = strtok_r(buf, "\n", &lsave); line; line = strtok_r(NULL, "\n", &lsave)) {
        char raw[LINE_LEN];
        snprintf(raw, sizeof(raw), "%s", line);
        char *cr = strchr(raw, '\r');
        if (cr)
            *cr = 0;
        char *t = trim(raw);
        if (!*t)
            continue; /* tomma rader skrivs om av save */

        char tmp[LINE_LEN];
        snprintf(tmp, sizeof(tmp), "%s", t);
        char *parts[4] = { 0 }, *save = NULL;
        int n = 0;
        for (char *p = strtok_r(tmp, ",", &save); p && n < 4; p = strtok_r(NULL, ",", &save))
            parts[n++] = trim(p);
        int comment = t[0] == '#' || t[0] == ';' || (t[0] == '/' && t[1] == '/');
        if (comment || n != 3 || !parse_setting(parts[0], parts[1], parts[2]))
            keep_line(t);
    }
    free(buf);

    unsigned char *bools[] = { &g_cfw.usbcharge, &g_cfw.launcher, &g_cfw.disablepause, &g_cfw.oldplugin,
                               &g_cfw.hibblock, &g_cfw.hidemac, &g_cfw.hidedlc, &g_cfw.noumd, &g_cfw.noanalog,
                               &g_cfw.deadef, &g_cfw.noled, &g_cfw.qaflags, &g_cfw.wpa2, &g_cfw.vitamute };
    for (unsigned i = 0; i < sizeof(bools) / sizeof(bools[0]); i++)
        *bools[i] = *bools[i] ? 1 : 0;
    return 1;
}

/* hela filen byggs i minnet och skrivs med ett anrop */
typedef struct {
    char data[8 * 1024];
    int len;
} OutBuf;

static void put(OutBuf *fd, const char *s)
{
    int n = strlen(s);
    if (fd->len + n < (int)sizeof(fd->data)) {
        memcpy(fd->data + fd->len, s, n);
        fd->len += n;
    }
}

static void put_setting(OutBuf *fd, const char *name, int on)
{
    char line[LINE_LEN];
    snprintf(line, sizeof(line), "always, %s, %s\n", name, on ? "on" : "off");
    put(fd, line);
}

static void put_clock(OutBuf *fd, const char *runlevel, int clock)
{
    char line[LINE_LEN];
    if (!clock)
        return; /* Auto = ingen rad */
    snprintf(line, sizeof(line), "%s, cpuclock:%s, on\n", runlevel, CLOCK_KEYS[clock]);
    put(fd, line);
}

int arkcfg_save(const char *path)
{
    static OutBuf out;
    OutBuf *fd = &out;
    out.len = 0;

    put_setting(fd, "usbcharge", g_cfw.usbcharge);
    if (g_cfw.clock_game == g_cfw.clock_vsh) {
        put_clock(fd, "always", g_cfw.clock_game);
    } else {
        put_clock(fd, "game", g_cfw.clock_game);
        put_clock(fd, "vsh", g_cfw.clock_vsh);
    }
    put_setting(fd, "wpa2", g_cfw.wpa2);
    put_setting(fd, "launcher", g_cfw.launcher);

    static const char *highmem[] = { "highmem", "highmem", "highmem:auto", "highmem:force16", "highmem:forcemax" };
    static const char *mscache[] = { "mscache", "mscache:4k", "mscache:8k", "mscache:16k" };
    static const char *inferno[] = { "infernocache", "infernocache:lru", "infernocache:rr" };
    static const char *logos[] = { "skiplogos", "skiplogos", "skiplogos:gameboot", "skiplogos:coldboot" };
    static const char *pics[] = { "hidepics", "hidepics", "hidepics:pic0", "hidepics:pic1" };
    put_setting(fd, highmem[g_cfw.highmem % 5], g_cfw.highmem != 0);
    put_setting(fd, mscache[g_cfw.mscache % 4], g_cfw.mscache != 0);
    put_setting(fd, inferno[g_cfw.infernocache % 3], g_cfw.infernocache != 0);
    put_setting(fd, "disablepause", g_cfw.disablepause);
    put_setting(fd, "oldplugin", g_cfw.oldplugin);
    put_setting(fd, "hibblock", g_cfw.hibblock);
    put_setting(fd, logos[g_cfw.skiplogos % 4], g_cfw.skiplogos != 0);
    put_setting(fd, pics[g_cfw.hidepics % 4], g_cfw.hidepics != 0);
    put_setting(fd, "hidemac", g_cfw.hidemac);
    put_setting(fd, "hidedlc", g_cfw.hidedlc);
    put_setting(fd, "noled", g_cfw.noled);
    put_setting(fd, "noumd", g_cfw.noumd);
    put_setting(fd, "deadef", g_cfw.deadef);
    put_setting(fd, "noanalog", g_cfw.noanalog);
    put_setting(fd, "vitamute", g_cfw.vitamute);
    put_setting(fd, "qaflags", g_cfw.qaflags);

    static const char *regions[] = { "", "jp", "us", "eu" };
    char line[LINE_LEN];
    if (g_cfw.regionchange >= 1 && g_cfw.regionchange <= 3) {
        snprintf(line, sizeof(line), "vsh, region_%s, on\n", regions[g_cfw.regionchange]);
        put(fd, line);
    }
    if (g_cfw.vshregion > 0) {
        snprintf(line, sizeof(line), "vsh, fakeregion_%d, on\n", g_cfw.vshregion);
        put(fd, line);
    }

    put(fd, "\n");
    for (int i = 0; i < s_extra_count; i++) {
        put(fd, s_extra[i]);
        put(fd, "\n");
    }

    SceUID f = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (f < 0)
        return 0;
    int ok = sceIoWrite(f, out.data, out.len) == out.len;
    sceIoClose(f);
    return ok;
}

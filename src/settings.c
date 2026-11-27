/* psp5 – made by 08Weber */
/* Inställningar i samma anda som PS5:s: mörk yta med ett strålkastarljus,
 * kategorier till vänster, alternativ till höger med tunna skiljelinjer
 * och en tunn ljus ram runt det valda. All grafik ritas här. */
#include <pspkernel.h>
#include <pspctrl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "settings.h"
#include "arkcfg.h"
#include "plugins.h"
#include "system.h"
#include "sound.h"
#include "lang.h"
#include "config.h"
#include "theme.h"
#include "osd.h"
#include "datetime.h"
#include "ui.h" /* bilderna i PICTURE */

#define TITLE_X     16.0f
#define TITLE_Y     50.0f
#define LEFT_X      16.0f
#define LEFT_W      142.0f
#define CAT_STEP    28.0f
#define CAT_H       25.0f
#define PANEL_X     168.0f
#define PANEL_Y     56.0f
#define PANEL_W     296.0f
#define PANEL_H     182.0f
#define RIGHT_X     180.0f
#define RIGHT_W     272.0f
#define ROW_H       25.0f
#define LIST_TOP    64.0f
#define LIST_BOTTOM 230.0f
#define MAX_ITEMS   (MAX_PLUGINS + 4)
#define CONFIRM_TIME 3.0f

#define TEXT_MAIN RGBA(238, 238, 242, 255)
#define TEXT_DIM  RGBA(160, 162, 172, 255)
#define LIGHT     RGBA(236, 236, 240, 255)

typedef enum { IT_TOGGLE, IT_CHOICE, IT_ACTION, IT_INFO, IT_THEME, IT_SUBPAGE, IT_DATETIME, IT_STORAGE_BAR } ItemType;

typedef struct {
    const char *label;
    ItemType type;
    unsigned char *value;        /* TOGGLE/CHOICE */
    const char *const *opts;     /* CHOICE */
    int nopts;
    int action;                  /* ACTION: SA_*, THEME: temats index, SUBPAGE: SUB_* */
    int confirm;                 /* ACTION: kräver ✕ två gånger */
    char info[48];               /* INFO-värde / undertext */
    float bar_fill;              /* IT_STORAGE_BAR: 0..1 */
    int plugin;                  /* >0: plugin nr (1-baserat) */
    int own;                     /* psp5:s egen inställning (psp5.cfg) */
    int immediate;               /* systeminställning som gäller direkt (inget att spara) */
    void (*set)(int opt);        /* valfri setter för IT_CHOICE */
} Item;

typedef struct {
    const char *name;
    const char *desc;
    Item items[MAX_ITEMS];
    int count;
} Category;

enum { CAT_PSP5, CAT_THEME, CAT_PERF, CAT_SYSTEM, CAT_PLUGINS, CAT_POWER, CAT_COUNT };
enum { SUB_NONE, SUB_DATETIME, SUB_INFO, SUB_STORAGE, SUB_COUNT };

static Category s_cats[CAT_COUNT];
static Category s_sub[SUB_COUNT - 1]; /* SUB_DATETIME, SUB_INFO, SUB_STORAGE */
static unsigned char s_plugin_on[MAX_PLUGINS];
/* Radbrytningen mäter texten många gånger – gör den bara när texten byts, inte varje bildruta */
static const char *s_wrapped; /* texten som s_desc håller, NULL = inget */
static int s_wrapped_n;

static struct {
    int cat, item[CAT_COUNT];
    int subpage, sub_item[SUB_COUNT - 1];
    int focus;          /* 0 = kategorier, 1 = alternativ */
    float scroll, sub_scroll, open, time;
    int confirm_item;   /* -1 = ingen */
    float confirm_time;
    int cfw_dirty, plugins_dirty, own_dirty, toast_pending;
    float save_timer;
    int repeat_dir;
    float repeat_timer;
    char toast[96];
    float toast_time;
} S;

/* valtexter byggs om när språket ändras */
static const char *const OPT_CLOCK[] = { "Auto", "133 MHz", "222 MHz", "333 MHz", "383 MHz", "403 MHz", "423 MHz", "443 MHz" };
static const char *OPT_HIGHMEM[5], *OPT_MSCACHE[4], *OPT_INFERNO[3], *OPT_LOGOS[4], *OPT_PICS[4];
static const char *OPT_UMDREG[4], *OPT_VSHREG[14], *OPT_LANG[LANG_COUNT], *OPT_BATTERY[3];
static const char *OPT_SCREEN_OFF[5], *OPT_AUTO_SLEEP[5]; /* minuter: se config.h */
static const char *OPT_PSP_CONFIRM[2], *OPT_DST[3], *OPT_TIMEFMT[2];
static const char *const OPT_DATEFMT[] = { "YYYY.MM.DD", "MM.DD.YYYY", "DD.MM.YYYY" };

/* Bakgrundsbilder att välja bland: bilderna i PICTURE (som XMB:s Foto), en nivå undermappar.
 * Val 0 = ingen (temats bakgrund). Läses när inställningarna öppnas. */
#define MAX_WALLS 48
static char s_wall_path[MAX_WALLS][sizeof(g_wallpaper)];
static char s_wall_name[MAX_WALLS][26];
static const char *OPT_WALL[MAX_WALLS + 1];
static int s_nwall;
static unsigned char s_wall_sel;

static void build_options(void)
{
    const char *off = L(S_OFF), *all = L(S_ALL), *def = L(S_STANDARD);
    const char *highmem[] = { off, def, L(S_AUTO), "+16 MiB", "Max" };
    const char *mscache[] = { off, "4 KiB", "8 KiB", "16 KiB" };
    const char *inferno[] = { off, "LRU", "RR" };
    const char *logos[] = { off, all, L(S_LOGOS_GAME), L(S_LOGOS_BOOT) };
    const char *pics[] = { off, all, "PIC0", "PIC1" };
    const char *umd[] = { def, L(S_R_JAPAN), L(S_R_AMERICA), L(S_R_EUROPE) };
    const char *vsh[] = { def, L(S_R_JAPAN), L(S_R_AMERICA), L(S_R_EUROPE), L(S_R_KOREA), L(S_R_UK),
                          L(S_R_LATAM), L(S_R_AUSTRALIA), L(S_R_HONGKONG), L(S_R_TAIWAN), L(S_R_RUSSIA),
                          L(S_R_CHINA), "Debug I", "Debug II" };
    memcpy(OPT_HIGHMEM, highmem, sizeof(highmem));
    memcpy(OPT_MSCACHE, mscache, sizeof(mscache));
    memcpy(OPT_INFERNO, inferno, sizeof(inferno));
    memcpy(OPT_LOGOS, logos, sizeof(logos));
    memcpy(OPT_PICS, pics, sizeof(pics));
    memcpy(OPT_UMDREG, umd, sizeof(umd));
    memcpy(OPT_VSHREG, vsh, sizeof(vsh));
    for (int i = 0; i < LANG_COUNT; i++)
        OPT_LANG[i] = lang_name(i);
    OPT_BATTERY[0] = L(S_BAT_ICON);
    OPT_BATTERY[1] = L(S_BAT_PERCENT);
    OPT_BATTERY[2] = L(S_BAT_TIME);
    static const char *const screen_off[] = { "", "1 min", "3 min", "5 min", "10 min" };
    static const char *const auto_sleep[] = { "", "5 min", "10 min", "15 min", "30 min" };
    for (int i = 0; i < 5; i++) {
        OPT_SCREEN_OFF[i] = i ? screen_off[i] : off;
        OPT_AUTO_SLEEP[i] = i ? auto_sleep[i] : off;
    }
    OPT_PSP_CONFIRM[0] = L(S_BTN_CROSS);
    OPT_PSP_CONFIRM[1] = L(S_BTN_CIRCLE);
    OPT_DST[DT_DST_REMOVE] = L(S_DT_DST_REMOVE);
    OPT_DST[DT_DST_KEEP] = L(S_DT_DST_KEEP);
    OPT_DST[DT_DST_ADD] = L(S_DT_DST_ADD);
    OPT_TIMEFMT[DT_TIME_24] = L(S_DT_24H);
    OPT_TIMEFMT[DT_TIME_12] = L(S_DT_12H);
    OPT_WALL[0] = off;
    for (int i = 0; i < s_nwall; i++)
        OPT_WALL[i + 1] = s_wall_name[i];
}

static void add_wall(const char *path)
{
    if (s_nwall >= MAX_WALLS)
        return;
    snprintf(s_wall_path[s_nwall], sizeof(s_wall_path[0]), "%s", path);
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    if (strlen(name) < sizeof(s_wall_name[0]))
        snprintf(s_wall_name[s_nwall], sizeof(s_wall_name[0]), "%s", name);
    else /* för långt för raden: början och "..." */
        snprintf(s_wall_name[s_nwall], sizeof(s_wall_name[0]), "%.*s...", (int)sizeof(s_wall_name[0]) - 4, name);
    s_nwall++;
}

/* Bilderna i PICTURE och vilken som är vald nu. Listan kommer från Bilder-fliken, som läses in i
 * bakgrunden – en egen katalogläsning här fick inställningarna att hänga på PSP Go (issue #41). */
static void load_walls(void)
{
    s_nwall = 0;
    for (int i = 0; i < ui_pic_count() && s_nwall < MAX_WALLS; i++)
        if (strstr(ui_pic_path(i), "/PICTURE/"))
            add_wall(ui_pic_path(i));
    s_wall_sel = 0;
    for (int i = 0; i < s_nwall; i++)
        if (!strcmp(s_wall_path[i], g_wallpaper))
            s_wall_sel = i + 1;
    if (!s_wall_sel && g_wallpaper[0] && s_nwall < MAX_WALLS) { /* vald bild utanför listan: behåll den */
        add_wall(g_wallpaper);
        s_wall_sel = s_nwall;
    }
}

/* ARK:s runlevel i PLUGINS.TXT → läsbar text */
static const char *runlevel_name(const char *rl)
{
    if (!strcasecmp(rl, "always") || !strcasecmp(rl, "all")) return L(S_ALL);
    if (!strcasecmp(rl, "game")) return L(S_TAB_GAMES);
    if (!strcasecmp(rl, "umd") || !strcasecmp(rl, "psp")) return L(S_RL_PSP);
    if (!strcasecmp(rl, "homebrew")) return L(S_TAB_APPS);
    if (!strcasecmp(rl, "pops") || !strcasecmp(rl, "psx") || !strcasecmp(rl, "ps1")) return "PS1";
    if (!strcasecmp(rl, "vsh") || !strcasecmp(rl, "xmb")) return "XMB";
    if (!strcasecmp(rl, "launcher")) return "Launcher";
    return rl; /* spel-ID */
}

#define N(a) (int)(sizeof(a) / sizeof((a)[0]))

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float approach(float cur, float target, float speed, float dt)
{
    return cur + (target - cur) * (1.0f - expf(-speed * dt));
}
static u32 fade(u32 c, float a)
{
    return COLOR_ALPHA(c, (int)(((c >> 24) & 0xFF) * clampf(a, 0, 1)));
}

/* --- menyn --- */

static Item *add(Category *c, const char *label, ItemType type)
{
    Item *it = &c->items[c->count++];
    memset(it, 0, sizeof(*it));
    it->label = label;
    it->type = type;
    return it;
}

static void toggle(Category *c, const char *label, unsigned char *v)
{
    add(c, label, IT_TOGGLE)->value = v;
}

static void choice(Category *c, const char *label, unsigned char *v, const char *const *opts, int n)
{
    Item *it = add(c, label, IT_CHOICE);
    it->value = v;
    it->opts = opts;
    it->nopts = n;
    if (*v >= n)
        *v = 0;
}

static void action(Category *c, const char *label, int act, int confirm)
{
    Item *it = add(c, label, IT_ACTION);
    it->action = act;
    it->confirm = confirm;
}

static void info(Category *c, const char *label, const char *value)
{
    snprintf(add(c, label, IT_INFO)->info, sizeof(((Item *)0)->info), "%s", value);
}

static void subpage(Category *c, const char *label, int page, const char *preview)
{
    Item *it = add(c, label, IT_SUBPAGE);
    it->action = page;
    if (preview)
        snprintf(it->info, sizeof(it->info), "%s", preview);
}

/* --- made by medhatk - System Settings helpers --- */

#define VOLUME_MAX    30
#define BRIGHT_MAX    100
#define BRIGHT_LEVELS 4

static unsigned char s_psp_circle;
static int s_confirm_circle;
static unsigned char s_volume, s_bright;
static char s_vol_text[VOLUME_MAX + 1][4];
static const char *s_vol_opts[VOLUME_MAX + 1];
static int s_br_levels[6], s_br_n;
static char s_br_text[6][12];
static const char *s_br_opts[6];
static Item *s_br_item;

static void br_build(int now)
{
    int n = 0;
    for (int i = 0; i < BRIGHT_LEVELS; i++)
        s_br_levels[n++] = i;
    if (now >= BRIGHT_LEVELS)
        s_br_levels[n++] = now;
    s_br_n = n;
    for (int i = 0; i < n; i++) {
        snprintf(s_br_text[i], sizeof(s_br_text[i]), "%d", s_br_levels[i] + 1);
        s_br_opts[i] = s_br_text[i];
        if (s_br_levels[i] == now)
            s_bright = i;
    }
    if (s_br_item) {
        s_br_item->opts = s_br_opts;
        s_br_item->nopts = n;
    }
}

static void set_brightness(int idx)
{
    int now = sys_set_brightness(s_br_levels[idx]);
    br_build(now >= 0 && now <= BRIGHT_MAX ? now : s_br_levels[s_bright]);
}

static void set_volume(int level)
{
    int now = sys_set_volume(level);
    if (now >= 0 && now <= VOLUME_MAX)
        s_volume = now;
}

static void set_button_assign(int opt)
{
    s_psp_circle = sys_set_confirm_is_circle(opt) ? 1 : 0;
    s_confirm_circle = s_psp_circle;
}

static const char *model_name(void)
{
    int m = sys_model();
    return m == 0 ? "PSP-1000" : m == 1 ? "PSP-2000"
         : (m == 2 || m == 3 || m == 5 || m == 6 || m == 7 || m == 8) ? "PSP-3000"
         : m == 4 ? "PSP Go" : m == 10 ? "PSP E1000" : "PSP";
}

static void fmt_bytes(char *out, int size, unsigned long long b)
{
    if (b >= 1024ull * 1024 * 1024)
        snprintf(out, size, "%.1f GB", (float)(b / (1024.0f * 1024.0f)) / 1024.0f);
    else if (b >= 1024 * 1024)
        snprintf(out, size, "%.1f MB", (float)b / (1024.0f * 1024.0f));
    else
        snprintf(out, size, "%u KB", (unsigned)(b / 1024));
}

static void fill_about(Category *c)
{
    SysAbout a;
    char v[48];
    c->count = 0;
    sys_about(&a);
    if (a.nickname[0])
        info(c, L(S_ABOUT_NICKNAME), a.nickname);
    info(c, L(S_ABOUT_MODEL), model_name());
    if (a.devkit > 0) {
        snprintf(v, sizeof(v), "%u.%u%u", a.devkit >> 24, (a.devkit >> 16) & 0xFF, (a.devkit >> 8) & 0xFF);
        info(c, L(S_ABOUT_SYSTEM), v);
    } else {
        info(c, L(S_ABOUT_SYSTEM), "-");
    }
    if (sys_ark_version()) {
        static char cfw[24];
        snprintf(cfw, sizeof(cfw), "CFW (ARK-%d)", sys_ark_version());
        info(c, cfw, sys_ark_full_version()[0] ? sys_ark_full_version() : "-");
    }
#ifdef PSP5_ARK4
    info(c, "PSP5 REMAKE", "1.0.3 (ARK-4)");
#else
    info(c, "PSP5 REMAKE", "1.0.3 (ARK-5)");
#endif
    if (a.has_mac) {
        snprintf(v, sizeof(v), "%02X:%02X:%02X:%02X:%02X:%02X", a.mac[0], a.mac[1], a.mac[2], a.mac[3], a.mac[4], a.mac[5]);
        info(c, L(S_ABOUT_MAC), v);
    }
    info(c, L(S_ABOUT_IP), a.last_ip[0] ? a.last_ip : L(S_ABOUT_NO_IP));
    fmt_bytes(v, sizeof(v), a.ram_free);
    info(c, L(S_ABOUT_RAM_FREE), v);
    if (a.battery) {
        if (a.percent >= 0) {
            snprintf(v, sizeof(v), a.charging ? "%d %%, %s" : "%d %%", a.percent, L(S_ABOUT_CHARGING));
            info(c, L(S_ABOUT_BATTERY), v);
        }
        if (a.mv > 0) {
            snprintf(v, sizeof(v), "%d.%02d V", a.mv / 1000, (a.mv % 1000) / 10);
            info(c, L(S_ABOUT_VOLTAGE), v);
        }
        if (a.celsius >= 0) {
            snprintf(v, sizeof(v), "%d °C", a.celsius);
            info(c, L(S_ABOUT_TEMP), v);
        }
        if (a.mah_full > 0 && a.mah_left >= 0) {
            snprintf(v, sizeof(v), "%d / %d mAh", a.mah_left, a.mah_full);
            info(c, L(S_ABOUT_CAPACITY), v);
        }
    }
}

static void fill_storage_dev(Category *c, const char *name, const SysStorageDev *d, StrId missing_id)
{
    if (!d->present) {
        info(c, name, L(missing_id));
        return;
    }
    Item *bar = add(c, name, IT_STORAGE_BAR);
    u64 pct = d->total_bytes ? (d->used_bytes * 100ULL) / d->total_bytes : 0;
    if (pct > 100)
        pct = 100;
    snprintf(bar->info, sizeof(bar->info), "%u %%", (unsigned)pct);
    bar->bar_fill = d->total_bytes ? clampf((float)((double)d->used_bytes / (double)d->total_bytes), 0.0f, 1.0f) : 0.0f;
    char v[48];
    fmt_bytes(v, sizeof(v), d->total_bytes);
    info(c, L(S_STORAGE_TOTAL), v);
    fmt_bytes(v, sizeof(v), d->used_bytes);
    info(c, L(S_STORAGE_USED), v);
    fmt_bytes(v, sizeof(v), d->free_bytes);
    info(c, L(S_STORAGE_FREE), v);
}

static void fill_storage(Category *c)
{
    SysStorage st;
    c->count = 0;
    sys_storage(&st);
    if (st.has_internal)
        fill_storage_dev(c, L(S_F_INTERNAL), &st.internal, S_STORAGE_UNAVAILABLE);
    if (st.has_ms)
        fill_storage_dev(c, L(S_F_MS), &st.ms, S_STORAGE_NOT_INSERTED);
}

static void storage_preview(char *out, int size)
{
    SysStorage st;
    sys_storage(&st);
    const SysStorageDev *d = (st.has_internal && st.internal.present) ? &st.internal
                           : (st.has_ms && st.ms.present)            ? &st.ms
                                                                     : NULL;
    if (d) {
        char num[24];
        fmt_bytes(num, sizeof(num), d->free_bytes);
        snprintf(out, size, L(S_F_FREE), num);
    } else if (st.has_internal) {
        snprintf(out, size, "%s", L(S_STORAGE_UNAVAILABLE));
    } else {
        snprintf(out, size, "%s", L(S_STORAGE_NOT_INSERTED));
    }
}

/* "ARK-5 · PSP-2000" – vilken CFW och modell psp5 upptäckte */
static const char *system_name(void)
{
    static char buf[40];
    if (sys_ark_version())
        snprintf(buf, sizeof(buf), "ARK-%d  ·  %s", sys_ark_version(), model_name());
    else
        snprintf(buf, sizeof(buf), "-");
    return buf;
}

/* rebuild = 1: menyn byggs om (t.ex. nytt språk) utan att tappa osparade val */
static void build(int rebuild)
{
    build_options();
    memset(s_cats, 0, sizeof(s_cats));
    s_wrapped = NULL; /* nytt språk eller typsnitt: bryt om beskrivningarna */
    Category *c;

    c = &s_cats[CAT_PSP5];
    c->name = "PSP5 REMAKE"; /* [medhatk] Renamed project/fork category in Settings sidebar */
    c->desc = L(S_DESC_PSP5);
    toggle(c, L(S_BOOT_PSP5), &g_cfw.launcher);
    toggle(c, L(S_SOUNDS), &g_sound_on);
    c->items[c->count - 1].own = 1;
    toggle(c, L(S_BOOT_ANIM), &g_boot_anim);
    c->items[c->count - 1].own = 1;
    toggle(c, L(S_CONTROL_CENTER), &g_control_center); /* av: HOME går till systemet / Custom Home (issue #16) */
    c->items[c->count - 1].own = 1;
    choice(c, L(S_LANGUAGE), &g_lang_setting, OPT_LANG, LANG_COUNT);
    c->items[c->count - 1].own = 1;
    toggle(c, L(S_HIDE_DUPES), &g_hide_dupes);
    c->items[c->count - 1].own = 1;
    toggle(c, L(S_ALL_GAMES), &g_all_games); /* av: fästa + senast spelade, resten i biblioteket (issue #30) */
    c->items[c->count - 1].own = 1;
    choice(c, L(S_BATTERY), &g_battery, OPT_BATTERY, 3);
    c->items[c->count - 1].own = 1;
    toggle(c, L(S_PREVIEWS), &g_previews); /* ICON1.PMF och SND0.AT3, som i XMB */
    c->items[c->count - 1].own = 1;
    info(c, L(S_IS_LAUNCHER), L(sys_is_launcher() ? S_YES : S_NO));
    action(c, L(S_CLEAR_CACHE), SA_CLEAR_CACHE, 0);
#ifdef PSP5_ARK4
    info(c, L(S_VERSION), "1.0.3 (ARK-4)"); /* [medhatk] PSP5 REMAKE release version */
#else
    info(c, L(S_VERSION), "1.0.3 (ARK-5)"); /* [medhatk] PSP5 REMAKE release version */
#endif
    info(c, "CFW", system_name());

    c = &s_cats[CAT_THEME];
    c->name = L(S_CAT_THEME);
    c->desc = L(S_DESC_THEME);
    toggle(c, L(S_THEME_ART), &g_theme_art);
    c->items[c->count - 1].own = 1;
    choice(c, L(S_WALLPAPER), &s_wall_sel, OPT_WALL, s_nwall + 1); /* bild ur PICTURE i stället för temats yta */
    c->items[c->count - 1].own = 1;
    for (int i = 0; i < theme_count() && c->count < MAX_ITEMS; i++) {
        Item *it = add(c, theme_get(i)->name, IT_THEME);
        it->action = i;
        it->own = 1;
    }

    c = &s_cats[CAT_PERF];
    c->name = L(S_CAT_PERF);
    c->desc = L(S_DESC_PERF);
    /* ARK-4 har bara Auto/133/222/333 och på/av för extra minne och minneskortscache;
     * PSP-1000 har varken extra minne eller USB-laddning (som i arkMenu) */
    int ark4 = sys_ark_version() == 4, psp1000 = sys_is_psp1000(), go = sys_model() == 4;
    if (ark4) {
        if (g_cfw.clock_game > CLOCK_333) g_cfw.clock_game = CLOCK_333;
        if (g_cfw.clock_vsh > CLOCK_333) g_cfw.clock_vsh = CLOCK_333;
        if (g_cfw.highmem > 1) g_cfw.highmem = 1;
        if (g_cfw.mscache > 1) g_cfw.mscache = 1;
    }
    int nclock = ark4 ? CLOCK_333 + 1 : N(OPT_CLOCK);
    choice(c, L(S_CPU_GAME), &g_cfw.clock_game, OPT_CLOCK, nclock);
    choice(c, L(S_CPU_XMB), &g_cfw.clock_vsh, OPT_CLOCK, nclock);
    if (!psp1000) {
        if (ark4)
            toggle(c, L(S_EXTRA_MEM), &g_cfw.highmem);
        else
            choice(c, L(S_EXTRA_MEM), &g_cfw.highmem, OPT_HIGHMEM, N(OPT_HIGHMEM));
    }
    if (ark4)
        toggle(c, L(S_MS_CACHE), &g_cfw.mscache);
    else
        choice(c, L(S_MS_CACHE), &g_cfw.mscache, OPT_MSCACHE, N(OPT_MSCACHE));
    choice(c, L(S_ISO_CACHE), &g_cfw.infernocache, OPT_INFERNO, N(OPT_INFERNO));

    c = &s_cats[CAT_SYSTEM];
    c->name = L(S_CAT_SYSTEM);
    s_br_item = NULL;
    {
        int br = sys_brightness(), vol = sys_volume();
        if (br >= 0 && br <= BRIGHT_MAX) {
            br_build(br);
            choice(c, L(S_BRIGHTNESS), &s_bright, s_br_opts, s_br_n);
            Item *it = &c->items[c->count - 1];
            it->set = set_brightness;
            it->immediate = 1;
            s_br_item = it;
        }
        if (vol >= 0 && vol <= VOLUME_MAX) {
            for (int i = 0; i <= VOLUME_MAX; i++) {
                snprintf(s_vol_text[i], sizeof(s_vol_text[i]), "%d", i);
                s_vol_opts[i] = s_vol_text[i];
            }
            s_volume = vol;
            choice(c, L(S_VOLUME), &s_volume, s_vol_opts, VOLUME_MAX + 1);
            Item *it = &c->items[c->count - 1];
            it->set = set_volume;
            it->immediate = 1;
        }
    }
    s_psp_circle = sys_confirm_is_circle() ? 1 : 0;
    s_confirm_circle = s_psp_circle;
    choice(c, L(S_BUTTON_ASSIGN), &s_psp_circle, OPT_PSP_CONFIRM, N(OPT_PSP_CONFIRM));
    c->items[c->count - 1].set = set_button_assign;
    c->items[c->count - 1].immediate = 1;
    action(c, L(S_USB_MODE), SA_USB, 0);
    subpage(c, L(S_DATETIME), SUB_DATETIME, NULL);
    subpage(c, L(S_CAT_INFO), SUB_INFO, model_name());
    {
        char prev[48];
        storage_preview(prev, sizeof(prev));
        subpage(c, L(S_STORAGE), SUB_STORAGE, prev);
    }
    action(c, L(S_SAVEDATA), SA_SAVEDATA, 0);
    if (!psp1000)
        toggle(c, L(S_USB_CHARGE), &g_cfw.usbcharge);
    toggle(c, L(S_WPA2), &g_cfw.wpa2);
    choice(c, L(S_SKIP_LOGOS), &g_cfw.skiplogos, OPT_LOGOS, N(OPT_LOGOS));
    choice(c, L(S_HIDE_PICS), &g_cfw.hidepics, OPT_PICS, N(OPT_PICS));
    toggle(c, L(S_HIDE_MAC), &g_cfw.hidemac);
    toggle(c, L(S_HIDE_DLC), &g_cfw.hidedlc);
    toggle(c, L(S_NO_LED), &g_cfw.noled);
    if (go) {
        /* PSP Go: ingen UMD-läsare, men Pausa spel (som i arkMenu) */
        toggle(c, L(S_NO_PAUSE), &g_cfw.disablepause);
        toggle(c, L(S_HIB_BLOCK), &g_cfw.hibblock);
    } else {
        toggle(c, L(S_NO_UMD), &g_cfw.noumd);
    }
    toggle(c, L(S_NO_ANALOG), &g_cfw.noanalog);
    if (!ark4)
        toggle(c, L(S_VITA_MUTE), &g_cfw.vitamute);
    toggle(c, L(S_QA_FLAGS), &g_cfw.qaflags);
    choice(c, L(S_UMD_REGION), &g_cfw.regionchange, OPT_UMDREG, N(OPT_UMDREG));
    choice(c, L(S_XMB_REGION), &g_cfw.vshregion, OPT_VSHREG, N(OPT_VSHREG));

    c = &s_cats[CAT_PLUGINS];
    c->name = L(S_CAT_PLUGINS);
    c->desc = L(S_DESC_PLUGINS);
    for (int i = 0; i < plugins_count() && c->count < MAX_ITEMS; i++) {
        Plugin *p = plugins_get(i);
        if (!rebuild)
            s_plugin_on[i] = p->on;
        Item *it = add(c, p->name, IT_TOGGLE);
        it->value = &s_plugin_on[i];
        it->plugin = i + 1;
        snprintf(it->info, sizeof(it->info), "%s", runlevel_name(p->where));
    }
    if (!c->count)
        info(c, L(S_NO_PLUGINS), "");

    c = &s_cats[CAT_POWER];
    c->name = L(S_CAT_POWER);
    action(c, L(S_USB_MODE), SA_USB, 0);
    if (sys_usbnet_version() > 0) {
        int st = sys_usbnet_state();
        action(c, "USBNet", SA_USBNET, 0);
        snprintf(c->items[c->count - 1].info, sizeof(c->items[0].info), "%s",
                 L((st & 12) ? S_USB_CONNECTED : (st & 1) ? S_YES : S_USB_WAITING));
    }
    action(c, L(S_FILES), SA_FILES, 0);
    choice(c, L(S_SCREEN_OFF), &g_screen_off, OPT_SCREEN_OFF, 5);
    c->items[c->count - 1].own = 1;
    choice(c, L(S_AUTO_SLEEP), &g_auto_sleep, OPT_AUTO_SLEEP, 5); /* inte medan musik spelar */
    c->items[c->count - 1].own = 1;
    action(c, L(S_TO_XMB), SA_XMB, 0);
    action(c, L(S_SLEEP), SA_SUSPEND, 1);
    action(c, L(S_RESTART), SA_RESTART, 1);
    action(c, L(S_SHUTDOWN), SA_SHUTDOWN, 1);
    action(c, L(S_RECOVERY), SA_RECOVERY, 1);

    memset(s_sub, 0, sizeof(s_sub));
    c = &s_sub[SUB_DATETIME - 1];
    c->name = L(S_DATETIME);
    add(c, L(S_DT_CURRENT), IT_DATETIME);
    choice(c, L(S_DT_DST), &g_dt_dst, OPT_DST, N(OPT_DST));
    c->items[c->count - 1].own = 1;
    choice(c, L(S_DT_DATE_FORMAT), &g_dt_date, OPT_DATEFMT, N(OPT_DATEFMT));
    c->items[c->count - 1].own = 1;
    choice(c, L(S_DT_TIME_FORMAT), &g_dt_time, OPT_TIMEFMT, N(OPT_TIMEFMT));
    c->items[c->count - 1].own = 1;

    c = &s_sub[SUB_INFO - 1];
    c->name = L(S_CAT_INFO);
    fill_about(c);

    c = &s_sub[SUB_STORAGE - 1];
    c->name = L(S_STORAGE);
    fill_storage(c);
}

void settings_open(void)
{
    font_ensure_cjk(); /* språkens egna namn (Русский, 日本語) */
    char path[200];
    snprintf(path, sizeof(path), "%sSETTINGS.TXT", sys_ark_dir());
    arkcfg_load(path);
    plugins_load(sys_ark_dir());
    load_walls();
    build(0);
    memset(&S, 0, sizeof(S));
    S.confirm_item = -1;
}

static void flush_dirty(void)
{
    if (S.own_dirty) {
        snprintf(g_wallpaper, sizeof(g_wallpaper), "%s", s_wall_sel ? s_wall_path[s_wall_sel - 1] : "");
        config_save(PSP5_DATA_DIR); /* ljud och språk gäller direkt, ingen omstart */
    }
    if (S.cfw_dirty) {
        char path[200];
        snprintf(path, sizeof(path), "%sSETTINGS.TXT", sys_ark_dir());
        arkcfg_save(path);
    }
    if (S.plugins_dirty) {
        for (int i = 0; i < plugins_count(); i++)
            plugins_get(i)->on = s_plugin_on[i];
        plugins_save();
    }
    S.cfw_dirty = S.plugins_dirty = S.own_dirty = 0;
    S.save_timer = 0;
}

int settings_commit(void)
{
    flush_dirty();
    int changed = S.toast_pending;
    S.toast_pending = 0;
    return changed;
}

/* --- uppdatering --- */

static int vertical(const UiInput *in, float dt)
{
    int dir = 0;
    if (in->held & PSP_CTRL_UP) dir = -1;
    if (in->held & PSP_CTRL_DOWN) dir = 1;
    if (!dir) {
        S.repeat_dir = 0;
        return 0;
    }
    if (dir != S.repeat_dir) {
        S.repeat_dir = dir;
        S.repeat_timer = 0.32f;
        return dir;
    }
    S.repeat_timer -= dt;
    if (S.repeat_timer <= 0) {
        S.repeat_timer += 0.08f;
        return dir;
    }
    return 0;
}

static void mark_dirty(const Item *it)
{
    if (it->immediate)
        return;
    if (it->own) {
        S.own_dirty = 1;
    } else if (it->plugin) {
        S.plugins_dirty = 1;
        S.toast_pending = 1;
    } else {
        S.cfw_dirty = 1;
        S.toast_pending = 1;
    }
    S.save_timer = 0.35f; /* spara strax efter ändringen så att inget tappas vid direkt omstart (issue #23) */
}

static Category *active_cat(void)
{
    return S.subpage ? &s_sub[S.subpage - 1] : &s_cats[S.cat];
}

static int *active_sel(void)
{
    return S.subpage ? &S.sub_item[S.subpage - 1] : &S.item[S.cat];
}

/* Övre kanten för rad i i den högra listan (innan scroll). */
static float row_y(const Category *c, int i);

static float s_info_refresh;

int settings_update(const UiInput *in, float dt)
{
    S.open = clampf(S.open + dt * 5.0f, 0, 1);
    S.time += dt;
    if (S.toast_time > 0)
        S.toast_time -= dt;
    if (S.confirm_time > 0 && (S.confirm_time -= dt) <= 0)
        S.confirm_item = -1;
    if (S.save_timer > 0 && (S.save_timer -= dt) <= 0)
        flush_dirty();
    if ((S.subpage == SUB_INFO || S.subpage == SUB_STORAGE) && (s_info_refresh += dt) >= 2.0f) {
        s_info_refresh = 0;
        Category *sc = &s_sub[S.subpage - 1];
        if (S.subpage == SUB_INFO)
            fill_about(sc);
        else
            fill_storage(sc);
        if (S.sub_item[S.subpage - 1] >= sc->count)
            S.sub_item[S.subpage - 1] = sc->count > 0 ? sc->count - 1 : 0;
    }

    Category *c = active_cat();
    int *sel = active_sel();
    u32 p = in->pressed;
    int move = vertical(in, dt);
    u32 ok_btn = s_confirm_circle ? PSP_CTRL_CIRCLE : PSP_CTRL_CROSS;
    u32 back_btn = s_confirm_circle ? PSP_CTRL_CROSS : PSP_CTRL_CIRCLE;

    if (S.focus == 0) {
        if (move) {
            int cat = (S.cat + move + CAT_COUNT) % CAT_COUNT; /* gå runt i listan (issue #15) */
            if (cat != S.cat)
                sound_play(SND_MOVE);
            S.cat = cat;
            S.confirm_item = -1;
        }
        if ((p & (ok_btn | PSP_CTRL_RIGHT)) && c->count) {
            S.focus = 1;
            sound_play(SND_SELECT);
        }
        if (p & (back_btn | PSP_CTRL_TRIANGLE)) {
            sound_play(SND_SIDE_OUT);
            return SA_CLOSE;
        }
    } else {
        Item *it = &c->items[*sel];
        if (move && c->count > 0) {
            int next = (*sel + move + c->count) % c->count; /* gå runt i listan (issue #15) */
            if (next != *sel)
                sound_play(SND_MOVE);
            *sel = next;
            S.confirm_item = -1;
            it = &c->items[*sel];
        }
        int change = 0;
        if (it->type == IT_CHOICE) {
            if (p & (PSP_CTRL_RIGHT | ok_btn)) change = 1;
            if (p & PSP_CTRL_LEFT) change = -1;
        }
        if (change) {
            int nv = (*it->value + change + it->nopts) % it->nopts;
            if (it->set)
                it->set(nv);
            else
                *it->value = nv;
            mark_dirty(it);
            sound_play(change > 0 ? SND_SLIDER_UP : SND_SLIDER_DN);
            if (it->value == &g_lang_setting) {
                lang_apply();
                if (lang_needs_cjk())
                    font_ensure_cjk();
                build(1);
            }
        } else if ((p & ok_btn) && it->type == IT_THEME) {
            if (g_theme != it->action) {
                g_theme = it->action;
                mark_dirty(it);
            }
            sound_play(SND_SELECT);
        } else if ((p & ok_btn) && it->type == IT_TOGGLE) {
            *it->value = !*it->value;
            mark_dirty(it);
            sound_play(*it->value ? SND_TOGGLE : SND_TOGGLE_OFF);
        } else if ((p & ok_btn) && it->type == IT_SUBPAGE) {
            if (it->action == SUB_INFO) {
                s_info_refresh = 0;
                fill_about(&s_sub[SUB_INFO - 1]);
            } else if (it->action == SUB_STORAGE) {
                s_info_refresh = 0;
                fill_storage(&s_sub[SUB_STORAGE - 1]);
            }
            S.subpage = it->action;
            S.sub_item[S.subpage - 1] = 0;
            S.sub_scroll = 0;
            S.confirm_item = -1;
            sound_play(SND_DETAIL_IN);
        } else if ((p & ok_btn) && it->type == IT_ACTION) {
            if (it->action == SA_USBNET) {
                int r = sys_usbnet_probe(1200);
                int st = sys_usbnet_state();
                int ok = r == 0 || (st & 12);
                snprintf(it->info, sizeof(it->info), "%s",
                         L(ok ? S_USB_CONNECTED : (st & 1) ? S_YES : S_USB_WAITING));
                snprintf(S.toast, sizeof(S.toast), "USBNet: %s", it->info);
                S.toast_time = 2.4f;
                sound_play(ok ? SND_SELECT : SND_ERROR);
                return SA_NONE;
            }
            if (it->action == SA_SAVEDATA) {
                S.confirm_item = -1;
                sound_play(SND_DETAIL_IN);
                return SA_SAVEDATA;
            }
            if (it->confirm && S.confirm_item != *sel) {
                S.confirm_item = *sel;
                S.confirm_time = CONFIRM_TIME;
                sound_play(SND_MSG_TOAST);
            } else {
                S.confirm_item = -1;
                sound_play(SND_SELECT);
                sound_wait(400); /* låt ljudet höras innan något stängs */
                return it->action;
            }
        } else if ((p & back_btn) || ((p & PSP_CTRL_LEFT) && it->type != IT_CHOICE)) {
            int was_sub = S.subpage != SUB_NONE;
            if (S.subpage) {
                int prev_sub = S.subpage;
                S.subpage = SUB_NONE;
                c = active_cat();
                sel = active_sel();
                if (prev_sub == SUB_STORAGE && *sel >= 0 && *sel < c->count && c->items[*sel].action == SUB_STORAGE)
                    storage_preview(c->items[*sel].info, sizeof(c->items[*sel].info));
            } else {
                S.focus = 0;
            }
            S.confirm_item = -1;
            sound_play(was_sub ? SND_DETAIL_OUT : SND_BACK);
        } else if (p & PSP_CTRL_TRIANGLE) {
            sound_play(SND_SIDE_OUT);
            return SA_CLOSE;
        }
    }

    /* håll vald rad synlig */
    float target = 0;
    if (S.focus == 1 && c->count) {
        float y = row_y(c, *sel);
        float visible = LIST_BOTTOM - LIST_TOP - ROW_H;
        if (y > visible)
            target = y - visible;
    }
    if (S.subpage)
        S.sub_scroll = approach(S.sub_scroll, target, 14.0f, dt);
    else
        S.scroll = approach(S.scroll, target, 14.0f, dt);
    return SA_NONE;
}

/* --- ritning --- */

#define MAX_DESC_LINES 6
static char s_desc[MAX_DESC_LINES][160];

/* Radbryter beskrivningen till högerspaltens bredd. */
/* Längd i byte på UTF-8-tecknet som börjar i p */
static int utf8_len(const char *p)
{
    unsigned char c = (unsigned char)*p;
    return c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
}

/* Radbryter beskrivningen till högerspaltens bredd. Bryter vid mellanslag, och
 * mellan enskilda tecken när ett "ord" inte får plats (japanska har inga mellanslag). */
static int wrap_desc_now(const char *text)
{
    const float max_w = RIGHT_W - 38.0f;
    int lines = 0;
    char line[sizeof(s_desc[0])] = "";
    const char *p = text;
    while (*p && lines < MAX_DESC_LINES) {
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        /* nästa ord */
        const char *end = p;
        while (*end && *end != ' ')
            end += utf8_len(end);
        char test[sizeof(line) * 2];
        snprintf(test, sizeof(test), "%s%s%.*s", line, line[0] ? " " : "", (int)(end - p), p);
        if (text_width(0.50f, 0, test) <= max_w && strlen(test) < sizeof(line)) {
            snprintf(line, sizeof(line), "%s", test);
            p = end;
            continue;
        }
        if (line[0]) { /* ordet får inte plats – ny rad */
            snprintf(s_desc[lines++], sizeof(s_desc[0]), "%s", line);
            line[0] = 0;
            continue;
        }
        /* ordet är bredare än raden: ta så många tecken som får plats */
        int n = 0;
        while (p + n < end) {
            int l = utf8_len(p + n);
            snprintf(test, sizeof(test), "%.*s", n + l, p);
            if (n && (text_width(0.50f, 0, test) > max_w || n + l >= (int)sizeof(line)))
                break;
            n += l;
        }
        snprintf(s_desc[lines++], sizeof(s_desc[0]), "%.*s", n, p);
        p += n;
    }
    if (line[0] && lines < MAX_DESC_LINES)
        snprintf(s_desc[lines++], sizeof(s_desc[0]), "%s", line);
    return lines;
}

static int wrap_desc(const char *text)
{
    if (text != s_wrapped) {
        s_wrapped_n = wrap_desc_now(text);
        s_wrapped = text;
    }
    return s_wrapped_n;
}

static float desc_line_h(void)
{
    return lang_needs_cjk() ? 14.0f : 12.0f;
}

static float desc_box_h(const Category *c)
{
    return c->desc ? fmaxf(32.0f, wrap_desc(c->desc) * desc_line_h() + 10.0f) : 0.0f;
}

static float desc_height(const Category *c)
{
    return c->desc ? desc_box_h(c) + 6.0f : 0.0f;
}

static float row_y(const Category *c, int i)
{
    return desc_height(c) + i * ROW_H;
}

/* Temat som visas: det markerade i temalistan (förhandsvisning), annars det valda */
static const Theme *shown_theme(void)
{
    const Category *c = &s_cats[CAT_THEME];
    const Item *it = &c->items[S.item[CAT_THEME]];
    if (S.cat == CAT_THEME && S.focus == 1 && S.item[CAT_THEME] < c->count && it->type == IT_THEME)
        return theme_get(it->action);
    return theme_cur();
}

static void draw_background(const Theme *t)
{
    static const float neutral[3] = { 120, 120, 120 };
    ui_draw_calm_backdrop(t, S.time, neutral, 0);
}

static void draw_info_icon(float cx, float cy, u32 col)
{
    gfx_ring(cx - 6.5f, cy - 6.5f, 13.0f, 13.0f, 6.5f, col);
    gfx_circle(cx, cy - 2.6f, 0.95f, col);
    gfx_rect(cx - 0.8f, cy - 0.7f, 1.6f, 4.3f, col);
}

/* Kategoriikoner: förrenderade tunna konturer (tools/make_icons.ps1), samma stil som toppraden. */
static void draw_cat_icon(int cat, float cx, float cy, u32 col)
{
    if (cat >= 0 && cat < CAT_COUNT)
        ui_draw_mask_icon(UI_ICON_CAT_PSP5 + cat, cx, cy, col);
}

static void draw_toggle(float x, float y, int on, u32 on_color, const Theme *t, float a)
{
    u32 off_col = THEME_INK(t, toggle_off, RGBA(68, 79, 96, 245));
    u32 knob_on = THEME_INK(t, knob, LIGHT);
    u32 knob_off = THEME_INK(t, knob, RGBA(208, 214, 224, 255));
    gfx_round_rect(x, y, 26.0f, 14.0f, 7.0f, fade(on ? on_color : off_col, a));
    gfx_circle(on ? x + 19.0f : x + 7.0f, y + 7.0f, 5.0f, fade(on ? knob_on : knob_off, a));
}

static void draw_hints(const Theme *t, float a)
{
    const char *sel = L(S_SELECT), *back = L(S_BACK);
    float w = 13.0f + 5.0f + text_width(0.56f, 0, sel) + 14.0f + 13.0f + 5.0f + text_width(0.56f, 0, back);
    float x = SCREEN_W - 16.0f - w, y = 257.0f;
    u32 label_col = fade(THEME_INK(t, text, RGBA(218, 224, 235, 238)), a);
    u32 dark_mark = fade(RGBA(18, 22, 30, 255), a);
    theme_button(t, x + 6.5f, y, 6.5f, s_confirm_circle ? BTN_CIRCLE : BTN_CROSS, 0, fade(LIGHT, 0.96f * a), dark_mark);
    x += 18.0f;
    text_draw(x, y + 4.5f, 0.56f, 0, label_col, ALIGN_LEFT, sel);
    x += text_width(0.56f, 0, sel) + 14.0f;
    theme_button(t, x + 6.5f, y, 6.5f, s_confirm_circle ? BTN_CROSS : BTN_CIRCLE, 0, fade(LIGHT, 0.96f * a), dark_mark);
    x += 18.0f;
    text_draw(x, y + 4.5f, 0.56f, 0, label_col, ALIGN_LEFT, back);
}

static void draw_item(const Item *it, int index, float y, int selected, const Theme *shown, float a)
{
    float right = RIGHT_X + RIGHT_W - 10.0f;
    char value[64] = "";
    u32 on_col = shown == theme_get(0) ? RGBA(37, 99, 210, 255) : theme_toggle_on(shown);
    switch (it->type) {
    case IT_TOGGLE:
        draw_toggle(right - 26.0f, y + 5.5f, *it->value != 0, on_col, shown, a);
        right -= 34.0f;
        snprintf(value, sizeof(value), "%s", it->info); /* plugins: var den laddas */
        break;
    case IT_CHOICE:
        if (selected)
            snprintf(value, sizeof(value), "<   %s   >", it->opts[*it->value]);
        else
            snprintf(value, sizeof(value), "%s", it->opts[*it->value]);
        break;
    case IT_INFO:
        snprintf(value, sizeof(value), "%s", it->info);
        break;
    case IT_DATETIME:
        dt_text(value, sizeof(value), dt_now());
        break;
    case IT_SUBPAGE:
        if (it->action == SUB_DATETIME)
            dt_text(value, sizeof(value), dt_now());
        else if (it->info[0])
            snprintf(value, sizeof(value), "%s", it->info);
        break;
    case IT_STORAGE_BAR: {
        float bar_x = RIGHT_X + 10.0f;
        float bar_w = RIGHT_W - 20.0f;
        float bar_y = y + 16.5f;
        float bar_h = 4.5f;
        u32 lcol = selected ? THEME_INK(shown, sel_ink, TEXT_MAIN)
                            : THEME_INK(shown, text, RGBA(226, 232, 240, 240));
        u32 vcol = selected ? THEME_INK(shown, sel_ink, RGBA(240, 244, 252, 255))
                            : THEME_INK(shown, text_dim, RGBA(168, 180, 198, 235));
        float vs = text_fit(0.54f, 0.44f, 0, RIGHT_W * 0.35f, it->info);
        float vw = it->info[0] ? text_width(vs, 0, it->info) + 8.0f : 0.0f;
        float ls = text_fit(0.56f, 0.44f, 1, bar_w - vw, it->label);
        text_draw(bar_x, y + 12.0f, ls, 1, fade(lcol, a), ALIGN_LEFT, it->label);
        if (it->info[0])
            text_draw(right, y + 12.0f, vs, 0, fade(vcol, a), ALIGN_RIGHT, it->info);
        u32 track = THEME_INK(shown, panel_deep, RGBA(255, 255, 255, 42));
        gfx_round_rect(bar_x, bar_y, bar_w, bar_h, bar_h * 0.5f, fade(track, a));
        if (it->bar_fill > 0.0f) {
            float fw = bar_w * clampf(it->bar_fill, 0.02f, 1.0f);
            gfx_round_rect(bar_x, bar_y, fw, bar_h, bar_h * 0.5f, fade(on_col, a));
        }
        return;
    }
    case IT_THEME: {
        /* liten förhandsvisning, och en bock för temat som används */
        theme_swatch(theme_get(it->action), right - 40.0f, y + 4.5f, 40.0f, 16.0f, a);
        right -= 48.0f;
        if (it->action == g_theme) {
            float cx = right - 6.0f, cy = y + 12.5f;
            float xy[6] = { cx - 5.0f, cy, cx - 1.5f, cy + 3.5f, cx + 5.0f, cy - 4.0f };
            u32 chk = THEME_INK(shown, fg_hi, LIGHT);
            u32 col[3] = { fade(chk, a), fade(chk, a), fade(chk, a) };
            gfx_stroke(xy, col, 3, 1.8f);
            right -= 16.0f;
        }
        break;
    }
    case IT_ACTION:
        if (S.confirm_item == index)
            snprintf(value, sizeof(value), "%s", L(S_CONFIRM));
        else if (it->info[0])
            snprintf(value, sizeof(value), "%s", it->info);
        break;
    }
    float value_w = 0;
    if (value[0]) {
        float vs = text_fit(0.56f, 0.44f, 0, RIGHT_W * 0.55f, value);
        value_w = text_width(vs, 0, value) + 12.0f;
        u32 col = S.confirm_item == index ? THEME_INK(shown, warn, RGBA(255, 190, 120, 255))
                : selected                ? THEME_INK(shown, sel_ink, RGBA(240, 244, 252, 255))
                                          : THEME_INK(shown, text_dim, RGBA(168, 180, 198, 235));
        text_draw(right, y + 16.5f, vs, 0, fade(col, a), ALIGN_RIGHT, value);
    }
    /* långa etiketter (t.ex. tyska, japanska) krymper i stället för att krocka med värdet */
    float ls = text_fit(0.58f, 0.46f, 0, right - value_w - (RIGHT_X + 10.0f), it->label);
    u32 lcol = selected ? THEME_INK(shown, sel_ink, TEXT_MAIN)
                        : THEME_INK(shown, text, RGBA(226, 232, 240, 240));
    text_draw(RIGHT_X + 10.0f, y + 16.5f, ls, 0, fade(lcol, a), ALIGN_LEFT, it->label);
}

void settings_draw(void)
{
    float a = S.open;
    float slide = (1.0f - a) * 10.0f;
    const Category *c = active_cat();
    int cur_sel = *active_sel();
    float cur_scroll = S.subpage ? S.sub_scroll : S.scroll;
    const Theme *shown = shown_theme();

    gfx_begin(THEME_INK(shown, panel_alt, RGBA(9, 13, 19, 255)));
    draw_background(shown);
    ui_draw_top_bar_for(0, 1, a, S.time, shown, 0.0f);
    text_draw(TITLE_X + slide, TITLE_Y, 0.90f, 1, fade(THEME_INK(shown, text, TEXT_MAIN), a), ALIGN_LEFT,
              S.subpage ? c->name : L(S_SETTINGS));

    /* vänster kategorilista */
    for (int i = 0; i < CAT_COUNT; i++) {
        float y = LIST_TOP + i * CAT_STEP;
        int selected = i == S.cat;
        if (selected) {
            float pa = S.focus == 0 ? 0.96f : 0.90f;
            u32 dark_col;
            if (shown->ink) {
                if (S.focus == 0) {
                    theme_sel_box(shown, LEFT_X, y, LEFT_W, CAT_H, 7.0f, fade(LIGHT, a));
                    dark_col = fade(shown->ink->sel_ink, a);
                } else {
                    gfx_round_rect(LEFT_X, y, LEFT_W, CAT_H, 7.0f, fade(shown->ink->border, 0.92f * a));
                    gfx_round_rect(LEFT_X + 1.0f, y + 1.0f, LEFT_W - 2.0f, CAT_H - 2.0f, 6.2f,
                                   fade(shown->ink->panel, 0.92f * a));
                    dark_col = fade(shown->ink->text, a);
                }
            } else {
                gfx_round_rect(LEFT_X, y, LEFT_W, CAT_H, 7.0f, fade(LIGHT, pa * a));
                dark_col = fade(RGBA(18, 22, 30, 255), a);
            }
            draw_cat_icon(i, LEFT_X + 14.0f, y + CAT_H * 0.5f, dark_col);
            float cs = text_fit(0.60f, 0.48f, S.focus == 0, LEFT_W - 34.0f, s_cats[i].name);
            text_draw(LEFT_X + 26.0f + slide, y + 16.5f, cs, S.focus == 0, dark_col, ALIGN_LEFT, s_cats[i].name);
        } else {
            float dim_a = S.focus ? (shown->ink ? 0.72f : 0.58f) : (shown->ink ? 0.95f : 0.88f);
            u32 dim_col = fade(THEME_INK(shown, text_dim, RGBA(154, 166, 186, 235)), a * dim_a);
            draw_cat_icon(i, LEFT_X + 14.0f, y + CAT_H * 0.5f, dim_col);
            float cs = text_fit(0.60f, 0.48f, 0, LEFT_W - 34.0f, s_cats[i].name);
            text_draw(LEFT_X + 26.0f + slide, y + 16.5f, cs, 0, dim_col, ALIGN_LEFT, s_cats[i].name);
        }
    }

    /* höger inställningskort */
    int def_theme = shown == theme_get(0);
    u32 card_bg = shown->ink ? COLOR_ALPHA(shown->ink->panel, (int)(242 * a))
                : def_theme  ? RGBA(16, 22, 32, (int)(232 * a))
                             : RGBA(14, 18, 26, (int)(208 * a));
    u32 card_border = shown->ink ? COLOR_ALPHA(shown->ink->border, (int)(235 * a))
                    : def_theme  ? RGBA(56, 72, 98, (int)(195 * a))
                                 : COLOR_ALPHA(shown->accent, (int)(135 * a));
    gfx_round_rect(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 10.0f, card_bg);
    gfx_ring(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, 10.0f, card_border);

    gfx_clip(PANEL_X + 2.0f, LIST_TOP - 2.0f, PANEL_W - 4.0f, LIST_BOTTOM - LIST_TOP + 4.0f);
    float top = LIST_TOP - cur_scroll;
    if (c->desc) {
        int n = wrap_desc(c->desc);
        float bh = desc_box_h(c);
        float edge = clampf((top + bh - LIST_TOP) / (bh * 0.65f), 0.0f, 1.0f);
        float ba = a * edge;
        if (ba > 0.02f) {
            gfx_round_rect(RIGHT_X, top, RIGHT_W, bh, 7.0f,
                           shown->ink ? COLOR_ALPHA(shown->ink->panel_alt, (int)(225 * ba)) : RGBA(24, 34, 48, (int)(200 * ba)));
            gfx_ring(RIGHT_X, top, RIGHT_W, bh, 7.0f,
                     shown->ink ? COLOR_ALPHA(shown->ink->border, (int)(200 * ba)) : RGBA(70, 92, 124, (int)(125 * ba)));
            draw_info_icon(RIGHT_X + 15.0f, top + bh * 0.5f,
                           shown->ink ? COLOR_ALPHA(shown->ink->fg_hi, (int)(235 * ba)) : RGBA(168, 182, 204, (int)(235 * ba)));
            float text_top = top + (bh - n * desc_line_h()) * 0.5f + 9.0f;
            u32 desc_col = shown->ink ? COLOR_ALPHA(shown->ink->text_dim, (int)(250 * ba)) : RGBA(178, 190, 208, (int)(238 * ba));
            for (int l = 0; l < n; l++)
                text_draw(RIGHT_X + 29.0f + slide, text_top + l * desc_line_h(), 0.50f, 0,
                          desc_col, ALIGN_LEFT, s_desc[l]);
        }
    }
    for (int i = 0; i < c->count; i++) {
        float y = top + row_y(c, i);
        if (y < LIST_TOP - ROW_H || y > LIST_BOTTOM)
            continue;
        /* tona ut rader som glider ut ur kortet */
        float edge = clampf((y - (LIST_TOP - ROW_H * 0.5f)) / (ROW_H * 0.55f), 0.0f, 1.0f) *
                     clampf((LIST_BOTTOM - y) / (ROW_H * 0.55f), 0.0f, 1.0f);
        float ra = a * edge;
        int selected = S.focus == 1 && i == cur_sel;
        if (selected) {
            if (shown->ink) {
                theme_sel_box(shown, RIGHT_X, y, RIGHT_W, ROW_H, 7.0f, fade(LIGHT, ra));
            } else {
                gfx_round_rect(RIGHT_X, y, RIGHT_W, ROW_H, 7.0f, RGBA(30, 44, 66, (int)(175 * ra)));
                gfx_ring(RIGHT_X, y, RIGHT_W, ROW_H, 7.0f, fade(RGBA(236, 240, 246, 245), ra));
            }
        } else if (i < c->count - 1 && !(S.focus == 1 && i + 1 == cur_sel)) {
            u32 sep = shown->ink ? COLOR_ALPHA(shown->ink->border, (int)(160 * ra)) : RGBA(44, 56, 74, (int)(190 * ra));
            gfx_rect(RIGHT_X + 6.0f, y + ROW_H - 0.5f, RIGHT_W - 12.0f, 1.0f, sep);
        }
        draw_item(&c->items[i], i, y, selected, shown, ra);
    }
    gfx_clip_reset();

    /* Figma: inställningarna har ingen dockad fotlist, bara fria knapptips */
    draw_hints(shown, a);

    if (S.toast_time > 0) {
        float ta = clampf(S.toast_time / 0.4f, 0.0f, 1.0f);
        float w = text_width(0.6f, 0, S.toast) + 30.0f;
        gfx_round_rect((SCREEN_W - w) / 2.0f, 128.0f, w, 26.0f, 7.0f,
                       COLOR_ALPHA(THEME_INK(shown, graphite, RGBA(44, 45, 52, 255)), (int)(245 * ta)));
        text_draw(SCREEN_W / 2.0f, 145.0f, 0.6f, 0,
                  COLOR_ALPHA(THEME_INK(shown, text_inverse, RGBA(240, 240, 245, 255)), (int)(255 * ta)),
                  ALIGN_CENTER, S.toast);
    }
    osd_draw();
    /* in-toning */
    if (a < 1.0f)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * (1.0f - a))));
    gfx_end();
}

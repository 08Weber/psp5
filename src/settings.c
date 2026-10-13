/* psp5 – made by Mille Weber */
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

#define TITLE_Y     34.0f
#define LEFT_X      32.0f
#define LEFT_W      124.0f
#define RIGHT_X     176.0f
#define RIGHT_W     280.0f
#define ROW_H       26.0f
#define LIST_TOP    58.0f
#define LIST_BOTTOM 236.0f
#define MAX_ITEMS   (MAX_PLUGINS + 4)
#define CONFIRM_TIME 3.0f

#define TEXT_MAIN RGBA(238, 238, 242, 255)
#define TEXT_DIM  RGBA(160, 162, 172, 255)
#define LIGHT     RGBA(236, 236, 240, 255)

typedef enum { IT_TOGGLE, IT_CHOICE, IT_ACTION, IT_INFO, IT_THEME } ItemType;

typedef struct {
    const char *label;
    ItemType type;
    unsigned char *value;        /* TOGGLE/CHOICE */
    const char **opts;           /* CHOICE */
    int nopts;
    int action;                  /* ACTION: SA_*, THEME: temats index */
    int confirm;                 /* ACTION: kräver ✕ två gånger */
    char info[48];               /* INFO-värde / undertext */
    int plugin;                  /* >0: plugin nr (1-baserat) */
    int own;                     /* psp5:s egen inställning (psp5.cfg) */
} Item;

typedef struct {
    const char *name;
    const char *desc;
    Item items[MAX_ITEMS];
    int count;
} Category;

enum { CAT_PSP5, CAT_THEME, CAT_PERF, CAT_SYSTEM, CAT_PLUGINS, CAT_POWER, CAT_COUNT };

static Category s_cats[CAT_COUNT];
static unsigned char s_plugin_on[MAX_PLUGINS];
/* Radbrytningen mäter texten många gånger – gör den bara när texten byts, inte varje bildruta */
static const char *s_wrapped; /* texten som s_desc håller, NULL = inget */
static int s_wrapped_n;

static struct {
    int cat, item[CAT_COUNT];
    int focus;          /* 0 = kategorier, 1 = alternativ */
    float scroll, open, time;
    int confirm_item;   /* -1 = ingen */
    float confirm_time;
    int cfw_dirty, plugins_dirty, own_dirty;
    int repeat_dir;
    float repeat_timer;
    char toast[96];
    float toast_time;
} S;

/* valtexter byggs om när språket ändras */
static const char *const OPT_CLOCK[] = { "Auto", "133 MHz", "222 MHz", "333 MHz", "383 MHz", "403 MHz", "423 MHz", "443 MHz" };
static const char *OPT_HIGHMEM[5], *OPT_MSCACHE[4], *OPT_INFERNO[3], *OPT_LOGOS[4], *OPT_PICS[4];
static const char *OPT_UMDREG[4], *OPT_VSHREG[14], *OPT_LANG[LANG_COUNT];

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

static void choice(Category *c, const char *label, unsigned char *v, const char **opts, int n)
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

/* "ARK-5 · PSP-2000" – vilken CFW och modell psp5 upptäckte */
static const char *system_name(void)
{
    static char buf[40];
    int m = sys_model();
    const char *model = m == 0 ? "PSP-1000" : m == 1 ? "PSP-2000"
                      : (m == 2 || m == 3 || m == 5 || m == 6 || m == 7 || m == 8) ? "PSP-3000"
                      : m == 4 ? "PSP Go" : m == 10 ? "PSP E1000" : "PSP";
    if (sys_ark_version())
        snprintf(buf, sizeof(buf), "ARK-%d  ·  %s", sys_ark_version(), model);
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
    c->name = "psp5";
    c->desc = L(S_DESC_PSP5);
    toggle(c, L(S_BOOT_PSP5), &g_cfw.launcher);
    toggle(c, L(S_SOUNDS), &g_sound_on);
    c->items[c->count - 1].own = 1;
    toggle(c, L(S_BOOT_ANIM), &g_boot_anim);
    c->items[c->count - 1].own = 1;
    choice(c, L(S_LANGUAGE), &g_lang_setting, OPT_LANG, LANG_COUNT);
    c->items[c->count - 1].own = 1;
    info(c, L(S_IS_LAUNCHER), L(sys_is_launcher() ? S_YES : S_NO));
    action(c, L(S_CLEAR_CACHE), SA_CLEAR_CACHE, 0);
#ifdef PSP5_ARK4
    info(c, L(S_VERSION), "1.5.0 (ARK-4)");
#else
    info(c, L(S_VERSION), "1.5.0 (ARK-5)");
#endif
    info(c, "CFW", system_name());

    c = &s_cats[CAT_THEME];
    c->name = L(S_CAT_THEME);
    c->desc = L(S_DESC_THEME);
    toggle(c, L(S_THEME_ART), &g_theme_art);
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
    choice(c, L(S_CPU_GAME), &g_cfw.clock_game, (const char **)OPT_CLOCK, nclock);
    choice(c, L(S_CPU_XMB), &g_cfw.clock_vsh, (const char **)OPT_CLOCK, nclock);
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
    action(c, L(S_TO_XMB), SA_XMB, 0);
    action(c, L(S_SLEEP), SA_SUSPEND, 1);
    action(c, L(S_RESTART), SA_RESTART, 1);
    action(c, L(S_SHUTDOWN), SA_SHUTDOWN, 1);
    action(c, L(S_RECOVERY), SA_RECOVERY, 1);
}

void settings_open(void)
{
    font_ensure_cjk(); /* språkens egna namn (Русский, 日本語) */
    char path[200];
    snprintf(path, sizeof(path), "%sSETTINGS.TXT", sys_ark_dir());
    arkcfg_load(path);
    plugins_load(sys_ark_dir());
    build(0);
    memset(&S, 0, sizeof(S));
    S.confirm_item = -1;
}

int settings_commit(void)
{
    int changed = S.cfw_dirty || S.plugins_dirty;
    if (S.own_dirty)
        config_save(PSP5_DATA_DIR); /* ljud och språk gäller direkt, ingen omstart */
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
    if (it->own)
        S.own_dirty = 1;
    else if (it->plugin)
        S.plugins_dirty = 1;
    else
        S.cfw_dirty = 1;
}

/* Övre kanten för rad i i den högra listan (innan scroll). */
static float row_y(const Category *c, int i);

int settings_update(const UiInput *in, float dt)
{
    S.open = clampf(S.open + dt * 5.0f, 0, 1);
    S.time += dt;
    if (S.toast_time > 0)
        S.toast_time -= dt;
    if (S.confirm_time > 0 && (S.confirm_time -= dt) <= 0)
        S.confirm_item = -1;

    Category *c = &s_cats[S.cat];
    int *sel = &S.item[S.cat];
    u32 p = in->pressed;
    int move = vertical(in, dt);

    if (S.focus == 0) {
        if (move) {
            int cat = (int)clampf(S.cat + move, 0, CAT_COUNT - 1);
            if (cat != S.cat)
                sound_play(SND_MOVE);
            S.cat = cat;
            S.confirm_item = -1;
        }
        if ((p & (PSP_CTRL_CROSS | PSP_CTRL_RIGHT)) && c->count) {
            S.focus = 1;
            sound_play(SND_SELECT);
        }
        if (p & (PSP_CTRL_CIRCLE | PSP_CTRL_TRIANGLE)) {
            sound_play(SND_BACK);
            return SA_CLOSE;
        }
    } else {
        Item *it = &c->items[*sel];
        if (move) {
            int next = (int)clampf(*sel + move, 0, c->count - 1);
            if (next != *sel)
                sound_play(SND_MOVE);
            *sel = next;
            S.confirm_item = -1;
            it = &c->items[*sel];
        }
        int change = 0;
        if (it->type == IT_CHOICE) {
            if (p & (PSP_CTRL_RIGHT | PSP_CTRL_CROSS)) change = 1;
            if (p & PSP_CTRL_LEFT) change = -1;
        }
        if (change) {
            *it->value = (*it->value + change + it->nopts) % it->nopts;
            mark_dirty(it);
            sound_play(SND_TOGGLE);
            if (it->value == &g_lang_setting) {
                lang_apply();
                if (lang_needs_cjk())
                    font_ensure_cjk();
                build(1);
            }
        } else if ((p & PSP_CTRL_CROSS) && it->type == IT_THEME) {
            if (g_theme != it->action) {
                g_theme = it->action;
                mark_dirty(it);
            }
            sound_play(SND_TOGGLE);
        } else if ((p & PSP_CTRL_CROSS) && it->type == IT_TOGGLE) {
            *it->value = !*it->value;
            mark_dirty(it);
            sound_play(SND_TOGGLE);
        } else if ((p & PSP_CTRL_CROSS) && it->type == IT_ACTION) {
            sound_play(SND_SELECT);
            if (it->confirm && S.confirm_item != *sel) {
                S.confirm_item = *sel;
                S.confirm_time = CONFIRM_TIME;
            } else {
                S.confirm_item = -1;
                sound_wait(400); /* låt ljudet höras innan något stängs */
                return it->action;
            }
        } else if ((p & PSP_CTRL_CIRCLE) || ((p & PSP_CTRL_LEFT) && it->type != IT_CHOICE)) {
            S.focus = 0;
            S.confirm_item = -1;
            sound_play(SND_BACK);
        } else if (p & PSP_CTRL_TRIANGLE) {
            sound_play(SND_BACK);
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
    const float max_w = RIGHT_W - 8;
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
        if (text_width(0.56f, 0, test) <= max_w && strlen(test) < sizeof(line)) {
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
            if (n && (text_width(0.56f, 0, test) > max_w || n + l >= (int)sizeof(line)))
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
    return lang_needs_cjk() ? 15.0f : 13.0f;
}

static float desc_height(const Category *c)
{
    return c->desc ? wrap_desc(c->desc) * desc_line_h() + 10 : 0;
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
    theme_backdrop(t, S.time, neutral, 1, 1);
}

static void draw_toggle(float x, float y, int on, u32 on_color, float a)
{
    gfx_round_rect(x, y, 26, 13, 6.5f, fade(on ? on_color : RGBA(74, 76, 84, 255), a));
    gfx_circle(on ? x + 19.5f : x + 6.5f, y + 6.5f, 5, fade(on ? LIGHT : RGBA(196, 196, 202, 255), a));
}

static void draw_hints(float a)
{
    const char *sel = L(S_SELECT), *back = L(S_BACK);
    float w = 13 + 5 + text_width(0.58f, 0, sel) + 14 + 13 + 5 + text_width(0.58f, 0, back);
    float x = SCREEN_W - 22 - w, y = 252;
    gfx_round_rect(x - 9, y - 10, w + 18, 20, 10, RGBA(0, 0, 0, (int)(110 * a)));
    gfx_button(x + 6.5f, y, 6.5f, BTN_CROSS, fade(LIGHT, a), fade(RGBA(20, 20, 26, 255), a));
    x += 18;
    text_draw(x, y + 4.5f, 0.58f, 0, fade(TEXT_MAIN, a), ALIGN_LEFT, sel);
    x += text_width(0.58f, 0, sel) + 14;
    gfx_button(x + 6.5f, y, 6.5f, BTN_CIRCLE, fade(LIGHT, a), fade(RGBA(20, 20, 26, 255), a));
    x += 18;
    text_draw(x, y + 4.5f, 0.58f, 0, fade(TEXT_MAIN, a), ALIGN_LEFT, back);
}

static void draw_item(const Item *it, int index, float y, int selected, const Theme *shown, float a)
{
    float right = RIGHT_X + RIGHT_W - 8;
    char value[64] = "";
    switch (it->type) {
    case IT_TOGGLE:
        draw_toggle(right - 26, y + 6.5f, *it->value != 0, theme_toggle_on(shown), a);
        right -= 34;
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
    case IT_THEME: {
        /* liten förhandsvisning, och en bock för temat som används */
        theme_swatch(theme_get(it->action), right - 40, y + 5, 40, 16, a);
        right -= 48;
        if (it->action == g_theme) {
            float cx = right - 6, cy = y + 13;
            float xy[6] = { cx - 5, cy, cx - 1.5f, cy + 3.5f, cx + 5, cy - 4 };
            u32 col[3] = { fade(LIGHT, a), fade(LIGHT, a), fade(LIGHT, a) };
            gfx_stroke(xy, col, 3, 1.8f);
            right -= 16;
        }
        break;
    }
    case IT_ACTION:
        if (S.confirm_item == index)
            snprintf(value, sizeof(value), "%s", L(S_CONFIRM));
        break;
    }
    float value_w = 0;
    if (value[0]) {
        float vs = text_fit(0.58f, 0.46f, 0, RIGHT_W * 0.55f, value);
        value_w = text_width(vs, 0, value) + 12;
        u32 col = S.confirm_item == index ? RGBA(255, 190, 120, 255) : TEXT_DIM;
        text_draw(right, y + 17, vs, 0, fade(col, a), ALIGN_RIGHT, value);
    }
    /* långa etiketter (t.ex. tyska, japanska) krymper i stället för att krocka med värdet */
    float ls = text_fit(0.62f, 0.48f, 0, right - value_w - (RIGHT_X + 8), it->label);
    text_draw(RIGHT_X + 8, y + 17, ls, 0, fade(TEXT_MAIN, a), ALIGN_LEFT, it->label);
}

void settings_draw(void)
{
    float a = S.open;
    float slide = (1 - a) * 10;
    const Category *c = &s_cats[S.cat];

    gfx_begin(RGBA(20, 20, 24, 255));
    const Theme *shown = shown_theme();
    draw_background(shown);
    text_draw(LEFT_X - 4 + slide, TITLE_Y, 0.95f, 0, fade(TEXT_MAIN, a), ALIGN_LEFT, L(S_SETTINGS));

    /* kategorier */
    for (int i = 0; i < CAT_COUNT; i++) {
        float y = LIST_TOP + i * 30;
        int selected = i == S.cat;
        float ta = selected ? 1.0f : S.focus ? 0.42f : 0.8f;
        if (selected && S.focus == 0) {
            gfx_rect(LEFT_X, y, LEFT_W, ROW_H, RGBA(255, 255, 255, (int)(16 * a)));
            gfx_frame(LEFT_X, y, LEFT_W, ROW_H, 1.3f, fade(RGBA(232, 232, 236, 230), a));
        }
        float cs = text_fit(0.66f, 0.5f, 0, LEFT_W - 16, s_cats[i].name);
        text_draw(LEFT_X + 10 + slide, y + 17.5f, cs, 0, fade(TEXT_MAIN, a * ta), ALIGN_LEFT, s_cats[i].name);
    }

    /* alternativ */
    float top = LIST_TOP - S.scroll;
    if (c->desc) {
        int n = wrap_desc(c->desc);
        for (int l = 0; l < n; l++) {
            float y = top + l * desc_line_h() + 9;
            if (y > LIST_TOP - 4 && y < LIST_BOTTOM)
                text_draw(RIGHT_X + 8 + slide, y, 0.56f, 0, fade(TEXT_DIM, a), ALIGN_LEFT, s_desc[l]);
        }
    }
    for (int i = 0; i < c->count; i++) {
        float y = top + row_y(c, i);
        if (y < LIST_TOP - ROW_H || y > LIST_BOTTOM)
            continue;
        /* tona ut rader som glider ut ur listan */
        float edge = clampf((y - (LIST_TOP - ROW_H * 0.6f)) / (ROW_H * 0.6f), 0, 1) *
                     clampf((LIST_BOTTOM - y) / (ROW_H * 0.6f), 0, 1);
        float ra = a * edge;
        int selected = S.focus == 1 && i == S.item[S.cat];
        if (selected) {
            gfx_rect(RIGHT_X, y, RIGHT_W, ROW_H, RGBA(255, 255, 255, (int)(16 * ra)));
            gfx_frame(RIGHT_X, y, RIGHT_W, ROW_H, 1.3f, fade(RGBA(232, 232, 236, 230), ra));
        } else if (i < c->count - 1 && !(S.focus == 1 && i + 1 == S.item[S.cat])) {
            gfx_rect(RIGHT_X + 4, y + ROW_H - 0.5f, RIGHT_W - 8, 1, RGBA(255, 255, 255, (int)(26 * ra)));
        }
        draw_item(&c->items[i], i, y, selected, shown, ra);
    }

    draw_hints(a);

    if (S.toast_time > 0) {
        float ta = clampf(S.toast_time / 0.4f, 0, 1);
        float w = text_width(0.6f, 0, S.toast) + 30;
        gfx_round_rect((SCREEN_W - w) / 2, 128, w, 26, 7, RGBA(44, 45, 52, (int)(245 * ta)));
        text_draw(SCREEN_W / 2, 145, 0.6f, 0, RGBA(240, 240, 245, (int)(255 * ta)), ALIGN_CENTER, S.toast);
    }
    /* in-toning */
    if (a < 1)
        gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * (1 - a))));
    gfx_end();
}

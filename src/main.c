/* psp5 – made by 08Weber */
/* psp5 – egen launcher för PSP med ARK-5. Kan köras som vanlig app
 * (ms0:/PSP/GAME/PSP5) eller som ARK:s launcher (ARK_01234/VBOOT.PBP). */
#include <pspkernel.h>
#include <pspctrl.h>
#include <psputility.h>
#include <psppower.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "gfx.h"
#include "scan.h"
#include "art.h"
#include "loader.h"
#include "ui.h"
#include "launch.h"
#include "settings.h"
#include "system.h"
#include "testrun.h"
#include "sound.h"
#include "lang.h"
#include "config.h"
#include "boot.h"
#include "music.h"
#include "umd.h"
#include "theme.h"
#include "osd.h"
#include "files.h"
#include "preview.h"

PSP_MODULE_INFO("psp5", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
/* Lämna 7 MB fritt: ARK:s RECOVERY.PRX (4 MB egen heap), MP3-avkodarens moduler och
 * trådarnas stackar. Resten (~17 MB på PSP-1000) räcker gott – psp5 använder under 10 MB. */
PSP_HEAP_SIZE_KB(-7168);

/* psp5:s egna filer (omslag, cache) – på lagringen psp5 startades från (ms0: eller PSP Go:s ef0:) */
#define DATA_DIR PSP5_DATA_DIR
/* Finns medan ett spel körs: när spelet avslutas startar systemet om psp5
 * (som ARK:s launcher), och då ska startskärmen inte visas igen. */
#define INGAME_MARK s_ingame_mark
/* Startskydd: filen finns medan psp5 startar och tas bort när menyn har gått en stund.
 * Blir den kvar har psp5 hängt sig eller kraschat under starten. Då blir nästa start
 * felsäker, och som ARK:s launcher går psp5 efter två sådana starter i rad direkt
 * till XMB – så att PSP:n aldrig fastnar i en start som inte fungerar. */
#define START_MARK s_start_mark
#define START_OK_AFTER 5.0f /* sekunder i menyn innan starten räknas som klar */

static volatile int s_running = 1;
static int s_safe;     /* felsäker start: ingen startskärm, musik eller UMD, cachen tömd */
static int s_start_ok; /* starten är klar och startskyddet borttaget */
static volatile int s_sleeping; /* PSP:n går in i viloläge – skärmen hålls svart */
static volatile int s_resumed;  /* PSP:n har vaknat klart – visa startskärmen */
static char s_self[256];
static char s_list_cache[256];
static char s_pending_music[256]; /* MP3 som öppnades i filhanteraren */
static char s_ingame_mark[32], s_start_mark[32], s_cache_dir[32]; /* i DATA_DIR, sätts i main */
static u64 s_scan_start;

static int exit_callback(int arg1, int arg2, void *common)
{
    s_running = 0;
    return 0;
}

static int power_callback(int unknown, int flags, void *common)
{
    if (flags & PSP_POWER_CB_RESUME_COMPLETE)
        s_resumed = 1;
    else if (flags & PSP_POWER_CB_SUSPENDING) {
        sys_home_release(); /* som gpSP: HOME-dialogen på under viloläget, menyn stänger av den igen */
        s_sleeping = 1;
    }
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cb = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
    int pcb = sceKernelCreateCallback("Power Callback", power_callback, NULL);
    scePowerRegisterCallback(-1, pcb);
    sceKernelSleepThreadCB();
    return 0;
}

static void setup_callbacks(void)
{
    int th = sceKernelCreateThread("cb_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
    if (th >= 0)
        sceKernelStartThread(th, 0, NULL);
}

static void scan_progress(int done, const char *name)
{
    ui_draw_scanning(done, name, (sceKernelGetSystemTimeWide() - s_scan_start) / 1000000.0f);
}

static int has_non_ascii(const EntryList *list)
{
    for (int i = 0; i < list->count; i++)
        for (const char *p = list->items[i].title; *p; p++)
            if ((u8)*p >= 0x80)
                return 1;
    return 0;
}

static int confirm_is_circle(void)
{
    int v = 1;
    if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_BUTTON_SWAP, &v) < 0)
        return 0;
    return v == 0; /* 0 = ○ bekräftar (japansk inställning) */
}

static int take_ingame_mark(void)
{
    SceIoStat st;
    if (sceIoGetstat(INGAME_MARK, &st) < 0)
        return 0;
    sceIoRemove(INGAME_MARK);
    return 1;
}

static void set_ingame_mark(void)
{
    if (!sys_is_launcher())
        return; /* som vanlig app går systemet till XMB efter spelet */
    SceUID fd = sceIoOpen(INGAME_MARK, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0)
        sceIoClose(fd);
}

static void start_write(int count)
{
    char c = '0' + (count > 9 ? 9 : count);
    SceUID fd = sceIoOpen(START_MARK, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, &c, 1);
        sceIoClose(fd);
    }
}

/* Hur många starter i rad som inte blev klara. Räknar samtidigt upp för den här. */
static int start_begin(void)
{
    int failed = 0;
    SceUID fd = sceIoOpen(START_MARK, PSP_O_RDONLY, 0);
    if (fd >= 0) {
        char c = 0;
        failed = 1; /* filen finns: minst en start blev inte klar */
        if (sceIoRead(fd, &c, 1) == 1 && c >= '1' && c <= '9')
            failed = c - '0';
        sceIoClose(fd);
    }
    start_write(failed + 1);
    return failed;
}

/* psp5 har startat klart: ta bort startskyddet och slå på launcherläget igen om det var pausat. */
static void start_confirmed(void)
{
    if (s_start_ok)
        return;
    s_start_ok = 1;
    sceIoRemove(START_MARK);
    sys_resume_launcher();
}

/* Bygger upp allt: grafik, typsnitt, spellista, laddartråd. */
static void app_start(EntryList *list, Ui *ui, int boot)
{
    gfx_init();
    font_init(lang_needs_cjk());
    sound_init();
    if (!s_safe)
        umd_start(); /* skivan hinner snurra upp under startskärmen */
    if (boot && g_boot_anim && !s_safe)
        boot_run(&s_running); /* startskärmen, innan spelen läses in */
    s_scan_start = sceKernelGetSystemTimeWide();
    scan_all(list, s_self, s_list_cache, scan_progress);
    Entry disc;
    int has_disc;
    if (umd_poll(&disc, &has_disc) && has_disc)
        scan_insert_front(list, &disc); /* UMD-skivan först */
    if (has_non_ascii(list))
        font_ensure_cjk(); /* t.ex. japanska speltitlar */
    loader_start(list);
    if (!s_safe)
        music_init(); /* läser in ms0:/MUSIC i bakgrunden */
    ui_init(ui, list, confirm_is_circle(), launch_supported());
    if (s_pending_music[0]) {
        music_play_path(s_pending_music);
        s_pending_music[0] = 0;
        ui->tab = TAB_MUSIC;
    }
}

/* Viloläge med sidoknappen: när PSP:n somnar ritas skärmen svart, så att det är
 * svart som syns när den vaknar (inte menyn). Svart tills PSP:n har vaknat klart,
 * sedan startskärmen. Musiken pausas under tiden. */
#define WAKE_TIMEOUT 4.0f /* sekunder vaken utan "vaknat klart" – visa startskärmen ändå */

static void sleep_and_wake(Ui *ui, int in_settings)
{
    int music = music_state() == MUSIC_PLAYING;
    if (music)
        music_toggle_pause();
    sound_prepare_boot(); /* startljudet frigjordes efter förra startskärmen */
    preview_pause(1);

    /* Svart i båda bildbuffertarna, sedan vilar psp5 utan att rita: grafikkretsen ska
     * inte arbeta medan PSP:n somnar och vaknar. Har vi inte sett PSP:n somna (t.ex.
     * viloläge från inställningarna) blir det direkt startskärm. */
    if (s_sleeping) {
        for (int i = 0; i < 2; i++) {
            gfx_begin(RGBA(0, 0, 0, 255));
            gfx_end();
        }
    }
    u64 last = sceKernelGetSystemTimeWide();
    float awake = 0; /* systemtiden står still medan PSP:n sover */
    while (s_sleeping && s_running && !s_resumed && awake < WAKE_TIMEOUT) {
        sceKernelDelayThread(20000);
        u64 now = sceKernelGetSystemTimeWide();
        float dt = (now - last) / 1000000.0f;
        last = now;
        awake += dt < 0.1f ? dt : 0.1f;
        testrun_buttons(0); /* testbygget: skriptet går vidare */
        if (testrun_take_resume())
            s_resumed = 1;
    }
    s_sleeping = s_resumed = 0;

    boot_run(&s_running);
    if (music)
        music_toggle_pause();
    ui_replay_intro(ui);
    preview_pause(in_settings); /* i inställningarna förblir den pausad */
}

/* Frigör allt i omvänd ordning – inför spelstart, USB-läge eller avslut. */
static void app_stop(EntryList *list, Ui *ui)
{
    sys_home_release(); /* spelet, XMB och USB-läget ska ha systemets HOME-dialog */
    umd_stop();
    loader_stop(); /* före music_term: laddaren läser musiklistan (omslagen) */
    preview_stop(); /* före music_term: förhandsvisningen använder samma ljudmoduler */
    music_term();
    sound_term();
    ui_free(ui);
    font_term();
    gfx_term();
    scan_free(list);
}

/* UMD-skivan har satts i eller tagits ut: lägg in den först i listan eller ta bort den. */
static void umd_refresh(EntryList *list, Ui *ui)
{
    Entry disc;
    int has;
    if (!umd_poll(&disc, &has))
        return;
    int had = list->count > 0 && list->items[0].type == ENTRY_UMD;
    if (!had && !has)
        return;
    loader_stop(); /* laddarens platser följer listans index */
    if (had)
        scan_remove_front(list);
    if (has && !scan_insert_front(list, &disc))
        has = 0;
    loader_start(list);
    ui_relist(ui, had, has);
    if (has && has_non_ascii(list))
        font_ensure_cjk();
}

/* home = 1: HOME läses i kärnläge och går till psp5 (Control Center) i stället för systemet */
static void read_pad(UiInput *in, u32 *old, int home)
{
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    if (!home)
        sys_home_release(); /* Control Center avstängt: låt systemet / Custom Home ha HOME (issue #16) */
    /* i användarläge betyder HOME-biten bara att systemets dialog syns */
    pad.Buttons = (pad.Buttons & ~PSP_CTRL_HOME) | (home ? sys_home_buttons() : 0);
    pad.Buttons = testrun_buttons(pad.Buttons); /* bara i testbyggen */
    in->pressed = pad.Buttons & ~*old;
    in->held = pad.Buttons;
    in->lx = pad.Lx;
    in->ly = pad.Ly;
    /* ♪ (NOTE) eller L + R + START tar en skärmbild (PNG) i ms0:/PICTURE/PSP5 */
    u32 combo = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START;
    if ((in->pressed & PSP_CTRL_NOTE) || ((pad.Buttons & combo) == combo && (*old & combo) != combo)) {
        osd_request_screenshot();
        in->pressed &= ~(PSP_CTRL_NOTE | combo);
    }
    *old = pad.Buttons;
}

/* USB-läge: inget annat rör minneskortet medan datorn är ansluten. */
static void usb_mode(void)
{
    gfx_init();
    font_init(lang_needs_cjk());
    if (!sys_usb_start()) {
        gfx_term();
        font_term();
        return;
    }
    u32 old = ~0u; /* ignorera knappen som startade läget (✕ eller ○), issue #10 */
    float t = 0;
    while (s_running) {
        UiInput in;
        read_pad(&in, &old, 0);
        if (in.pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS))
            break;
        t += 1 / 60.0f;
        osd_update(1 / 60.0f);

        int state = sys_usb_state();
        gfx_begin(RGBA(20, 20, 24, 255));
        static const float neutral[3] = { 120, 120, 120 };
        theme_backdrop(theme_cur(), t, neutral, 1, 1); /* som inställningarna */
        text_draw(28, 34, 0.95f, 0, RGBA(238, 238, 242, 255), ALIGN_LEFT, L(S_USB_MODE));
        float pulse = 0.6f + 0.4f * sinf(t * 3);
        gfx_circle(SCREEN_W / 2, 120, 26, RGBA(255, 255, 255, state == 2 ? 40 : (int)(30 * pulse)));
        gfx_circle(SCREEN_W / 2, 120, 9, state == 2 ? RGBA(120, 220, 150, 255) : RGBA(236, 236, 240, (int)(255 * pulse)));
        text_draw(SCREEN_W / 2, 176, 0.75f, 0, RGBA(238, 238, 242, 255), ALIGN_CENTER,
                  L(state == 2 ? S_USB_CONNECTED : S_USB_WAITING));
        text_draw(SCREEN_W / 2, 196, 0.56f, 0, RGBA(160, 162, 172, 255), ALIGN_CENTER,
                  L(S_USB_EJECT));
        gfx_button(SCREEN_W / 2 - 40, 246, 6.5f, BTN_CIRCLE, RGBA(236, 236, 240, 255), RGBA(20, 20, 26, 255));
        text_draw(SCREEN_W / 2 - 28, 250.5f, 0.58f, 0, RGBA(238, 238, 242, 255), ALIGN_LEFT, L(S_USB_EXIT));
        osd_draw();
        if (t < 0.25f) /* tonas in från svart, som filhanteraren */
            gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * (1 - t / 0.25f))));
        gfx_end();
    }
    sys_usb_stop();
    s_sleeping = s_resumed = 0; /* viloläge under USB-läget: ingen startskärm efteråt */
    font_term();
    gfx_term();
}

/* Filhanteraren: allt annat är stängt, så inget annat läser eller skriver filer under tiden. */
static void files_mode(void)
{
    gfx_init();
    font_init(lang_needs_cjk());
    sound_init();
    files_open();
    u32 old = ~0u; /* ignorera knappen som öppnade den (✕ eller ○) */
    u64 last = sceKernelGetSystemTimeWide();
    while (s_running) {
        UiInput in;
        read_pad(&in, &old, 0);
        u64 now = sceKernelGetSystemTimeWide();
        float dt = (now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f)
            dt = 0.05f;
        osd_update(dt);
        if (files_update(&in, dt))
            break;
        files_draw();
    }
    files_close();
    sound_term();
    s_sleeping = s_resumed = 0;
    font_term();
    gfx_term();
}

static void delete_dir_files(const char *dir)
{
    SceUID d = sceIoDopen(dir);
    if (d < 0)
        return;
    SceIoDirent ent;
    char path[256];
    memset(&ent, 0, sizeof(ent));
    while (sceIoDread(d, &ent) > 0) {
        if (!FIO_S_ISDIR(ent.d_stat.st_mode)) {
            snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name);
            sceIoRemove(path);
        }
        memset(&ent, 0, sizeof(ent));
    }
    sceIoDclose(d);
}

/* Utför en åtgärd från inställningarna. Returnerar 1 om appen ska byggas upp igen. */
static int run_action(int action, EntryList *list, Ui *ui)
{
    /* psp5 fungerar uppenbarligen. Till XMB tas bara startskyddet bort: launcherläget
     * ska vara fortsatt pausat där. */
    if (action == SA_XMB)
        sceIoRemove(START_MARK);
    else
        start_confirmed();
    switch (action) {
    case SA_SUSPEND:
        sys_suspend();
        return 0;
    case SA_USB:
        app_stop(list, ui);
        usb_mode();
        return 1; /* filer kan ha ändrats – skanna om */
    case SA_FILES: {
        app_stop(list, ui);
        files_mode();
        char open_path[256];
        int open_act = files_take_open(open_path, sizeof(open_path));
        if (open_act == FO_GAME) {
            Entry e;
            memset(&e, 0, sizeof(e));
            snprintf(e.path, sizeof(e.path), "%s", open_path);
            const char *dot = strrchr(open_path, '.');
            if (dot && !strcasecmp(dot, ".pbp"))
                e.type = strstr(open_path, "/PSP/GAME/PSX/") ? ENTRY_POPS : ENTRY_HOMEBREW;
            else
                e.type = ENTRY_ISO;
            start_confirmed();
            set_ingame_mark();
            launch_entry(&e);
            sceIoRemove(INGAME_MARK);
        } else if (open_act == FO_MUSIC) {
            snprintf(s_pending_music, sizeof(s_pending_music), "%s", open_path);
        }
        return 1;
    }
    case SA_CLEAR_CACHE:
        app_stop(list, ui);
        delete_dir_files(s_cache_dir);
        return 1;
    }
    /* resten lämnar psp5 – frigör allt först */
    app_stop(list, ui);
    switch (action) {
    case SA_XMB:      sys_exit_xmb(); break;
    case SA_RESTART:  sys_restart(); break;
    case SA_SHUTDOWN: sys_shutdown(); break;
    case SA_RECOVERY: sys_recovery(); break;
    }
    for (;;)
        sceKernelDelayThread(100000); /* systemet tar över */
}

int main(int argc, char *argv[])
{
    setup_callbacks();
    snprintf(s_self, sizeof(s_self), "%s", argc > 0 && argv[0] ? argv[0] : "ms0:/PSP/GAME/PSP5/EBOOT.PBP");
    sys_set_root(s_self);
    snprintf(s_ingame_mark, sizeof(s_ingame_mark), "%s/ingame", DATA_DIR);
    snprintf(s_start_mark, sizeof(s_start_mark), "%s/starting", DATA_DIR);
    snprintf(s_cache_dir, sizeof(s_cache_dir), "%s/cache", DATA_DIR);
    snprintf(s_list_cache, sizeof(s_list_cache), "%s/cache/list.bin", DATA_DIR);
    sceIoMkdir(DATA_DIR, 0777);
    int failed = start_begin(); /* först av allt – före allt som kan hänga sig */
    art_init(DATA_DIR);
    config_load(DATA_DIR); /* ljud och språk */
    sys_init(s_self);
    osd_init(); /* volymindikatorn behöver ARK */
    sys_home_init(); /* HOME öppnar Control Center (behöver ARK) */
    testrun_init();

    if (failed >= 2 && sys_is_launcher() && sys_has_ark()) {
        /* Två starter i rad har inte blivit klara: lämna PSP:n till XMB. Nästa start
         * som launcher blir felsäker (inte XMB direkt igen). */
        start_write(1);
        sys_exit_xmb();
        sceKernelExitGame();
        return 0;
    }
    s_safe = failed > 0;
    if (s_safe) {
        delete_dir_files(s_cache_dir); /* en trasig cachefil ska inte kunna stoppa starten */
        sound_skip_boot();
    }

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    EntryList list = { 0 };
    Ui ui;
    app_start(&list, &ui, !take_ingame_mark()); /* ingen startskärm efter ett spel */
    if (s_safe)
        ui_toast(&ui, L(S_SAFE_START));
#ifdef PSP5_TEST
    sound_dump("ms0:/PSP5/shots/sounds"); /* testbygge: ljuden som WAV */
#endif

    s_sleeping = s_resumed = 0; /* viloläge under startskärmen räknas inte */
    int in_settings = 0;
    unsigned char hide_dupes = 0; /* hur flikarna byggdes när inställningarna öppnades */
    u32 old = 0;
    float up = 0; /* sekunder i menyn – för startskyddet */
    float idle = 0; /* sekunder utan knapptryck – för skärm av och viloläge */
    int dark = 0;   /* skärmen är släckt: 1 = av (ARK), 2 = bara svart bild */
    u64 last = sceKernelGetSystemTimeWide();
    while (s_running) {
        if (testrun_take_sleep())
            s_sleeping = 1;
        if (testrun_take_resume())
            s_resumed = 1;
        if (s_sleeping || s_resumed) {
            if (dark == 1) /* somnade med släckt skärm: vakna med den tänd */
                sys_display(1);
            dark = 0;
            idle = 0;
            if (g_boot_anim && ui.launch < 0 && !s_safe) {
                sleep_and_wake(&ui, in_settings);
                old = ~0u; /* knappen som hoppade över startskärmen räknas inte */
                last = sceKernelGetSystemTimeWide();
            } else {
                s_sleeping = s_resumed = 0;
            }
        }
        UiInput in;
        read_pad(&in, &old, g_control_center);

        u64 now = sceKernelGetSystemTimeWide();
        float dt = (now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f)
            dt = 0.05f;
        osd_update(dt);
        if (!s_start_ok) {
            up += dt;
            if (up >= START_OK_AFTER && !music_scanning())
                start_confirmed(); /* menyn och musiklistan fungerar */
        }

        /* skärm av och viloläge efter en stund utan knapptryck (Inställningar → Ström och USB).
         * Musiken spelar vidare med släckt skärm, och medan den spelar somnar PSP:n inte. */
        static const short SCREEN_OFF_S[] = { 0, 60, 180, 300, 600 }, SLEEP_S[] = { 0, 300, 600, 900, 1800 };
        int active = (in.held & ~PSP_CTRL_HOLD) || in.lx < 60 || in.lx > 196 || in.ly < 60 || in.ly > 196;
        idle = active ? 0 : idle + dt;
        if (dark && active) { /* knappen som tände skärmen gör inget annat */
            if (dark == 1)
                sys_display(1);
            dark = 0;
            continue;
        }
        if (!dark && g_screen_off && idle >= SCREEN_OFF_S[g_screen_off])
            dark = sys_display(0) ? 1 : 2;
        if (g_auto_sleep && idle >= SLEEP_S[g_auto_sleep] && music_state() != MUSIC_PLAYING) {
            if (dark == 1)
                sys_display(1);
            dark = 0;
            idle = 0;
            sys_suspend(); /* när PSP:n vaknar visas startskärmen som vanligt */
            continue;
        }
        if (dark) {
            if (dark == 2) { /* utan ARK: en svart bild i stället */
                gfx_begin(RGBA(0, 0, 0, 255));
                gfx_end();
            } else {
                sceKernelDelayThread(16000);
            }
            continue;
        }

        if (in_settings) {
            /* HOME eller SELECT stänger inställningarna och öppnar Control Center;
             * L och R stegar vidare längs toppraden (← Sök, → Spel) */
            int home = (in.pressed & (PSP_CTRL_HOME | PSP_CTRL_SELECT)) != 0;
            int lr = (in.pressed & (PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER));
            int action = (home || lr) ? SA_CLOSE : settings_update(&in, dt);
            if (action == SA_CLOSE) {
                in_settings = 0;
                preview_pause(0);
                if (settings_commit())
                    ui_toast(&ui, L(S_SAVED));
                if (g_hide_dupes != hide_dupes)
                    ui_relist(&ui, 0, 0); /* bygg om Spel och Spelbiblioteket */
                if (home) {
                    ui_open_cc(&ui);
                } else if (lr) {
                    sound_play(SND_SWIPE);
                    ui_set_tab(&ui, (lr & PSP_CTRL_LTRIGGER) ? TAB_SEARCH : TAB_GAMES);
                }
            } else if (action != SA_NONE) {
                settings_commit();
                if (run_action(action, &list, &ui)) {
                    app_start(&list, &ui, 0);
                    in_settings = 0;
                    preview_pause(0);
                }
                old = in.held;
                continue;
            }
            /* bilder och bakgrund fortsätter laddas i bakgrunden */
            UiInput none = { 0, 0, 128, 128 }; /* spaken i mitten – lx = 0 rullade raden åt vänster */
            ui_update(&ui, &none, dt);
            if (in_settings)
                settings_draw();
            else
                ui_draw(&ui);
            continue;
        }

        if (ui.launch < 0)
            umd_refresh(&list, &ui);
        int action = ui_update(&ui, &in, dt);
        if (action == UI_SETTINGS) {
            /* menyn fungerar: slå på ett pausat launcherläge innan inställningarna läser
             * det, annars visas "Boot psp5" som av tills första spelstarten (issue #8) */
            start_confirmed();
            hide_dupes = g_hide_dupes;
            preview_pause(1); /* spelets musik ska inte spela bakom inställningarna */
            settings_open();
            /* efter inläsningen (typsnitt från flash0, ARK:s filer): tung läsning medan ljudet
             * spelade gav ett skrapande ljud i ~0,1 s (issue #2) */
            sound_play(SND_OPEN);
            in_settings = 1;
            continue;
        }
        if (action == UI_ACTION) { /* Control Center: viloläge, USB, filer, XMB, omstart, stäng av */
            if (run_action(ui.cc_action, &list, &ui))
                app_start(&list, &ui, 0);
            /* viloläge: skärmen har tonats till svart – håll den svart tills PSP:n somnar,
             * annars syns menyn en kort stund innan (sleep_and_wake tar sedan över) */
            for (int k = 0; ui.cc_action == SA_SUSPEND && k < 120 && !s_sleeping && s_running; k++) {
                gfx_begin(RGBA(0, 0, 0, 255));
                gfx_end();
            }
            old = in.held;
            continue;
        }
        if (action == UI_LAUNCH && ui_selected(&ui) >= 0) {
            Entry chosen = list.items[ui_selected(&ui)];
            sound_wait(700);      /* låt startljudet klinga ut */
            app_stop(&list, &ui); /* frigör allt minne innan spelet laddas */
            start_confirmed();    /* ett spel som hänger sig räknas inte mot psp5 */
            set_ingame_mark();
            launch_entry(&chosen);
            /* hit kommer vi bara om starten misslyckades */
            sceIoRemove(INGAME_MARK);
            app_start(&list, &ui, 0);
            ui_toast(&ui, L(S_LAUNCH_FAILED));
            continue;
        }
        ui_draw(&ui);
    }

    /* HOME → Avsluta: till XMB (även när psp5 är ARK:s launcher) */
    if (in_settings)
        settings_commit();
    app_stop(&list, &ui);
    sceIoRemove(START_MARK);
    sys_exit_xmb();
    sceKernelExitGame();
    return 0;
}

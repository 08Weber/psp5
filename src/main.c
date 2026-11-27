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
#include "info.h"
#include "lastplayed.h"
#include "savedata.h"

PSP_MODULE_INFO("PSP5_REMAKE", PSP_MODULE_USER, 0, 1); /* [medhatk] Renamed module info for PSP5 REMAKE */
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

static int has_non_ascii(const EntryList *list)
{
    for (int i = 0; i < list->count; i++)
        for (const char *p = list->items[i].title; *p; p++)
            if ((u8)*p >= 0x80)
                return 1;
    return 0;
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

static struct {
    EntryList *list;
    volatile int done;
} s_scan_job;

static int scan_thread(SceSize args, void *argp)
{
    scan_all(s_scan_job.list, s_self, s_list_cache, NULL);
    s_scan_job.done = 1;
    return 0;
}

/* Bygger upp allt: grafik, typsnitt, spellista, laddartråd. */
static void app_start(EntryList *list, Ui *ui, int boot)
{
    gfx_init();
    font_init(lang_needs_cjk());
    sound_init();
    if (!s_safe)
        umd_start(); /* skivan hinner snurra upp under startskärmen */
    if (boot && g_boot_anim && !s_safe) {
        s_scan_job.list = list;
        s_scan_job.done = 0;
        SceUID th = sceKernelCreateThread("psp5_scan", scan_thread, 0x30, 0x10000, PSP_THREAD_ATTR_USER, NULL);
        if (th >= 0 && sceKernelStartThread(th, 0, NULL) >= 0) {
            boot_run(&s_running, &s_scan_job.done); /* startskärmen visas tills spelet skannats klart */
            sceKernelWaitThreadEnd(th, NULL);
            sceKernelDeleteThread(th);
        } else {
            if (th >= 0)
                sceKernelDeleteThread(th);
            boot_run(&s_running, NULL);
            scan_all(list, s_self, s_list_cache, NULL);
        }
    } else {
        scan_all(list, s_self, s_list_cache, NULL);
    }
    logo_free(); /* frigör startloggans textur efter skanningen */
    Entry disc;
    int has_disc;
    if (umd_poll(&disc, &has_disc) && has_disc)
        scan_insert_front(list, &disc); /* UMD-skivan först */
    if (has_non_ascii(list))
        font_ensure_cjk(); /* t.ex. japanska speltitlar */
    loader_start(list);
    if (!s_safe)
        music_init(); /* läser in ms0:/MUSIC i bakgrunden */
    ui_init(ui, list, sys_confirm_is_circle(), launch_supported());
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
    int wake_event = s_sleeping || s_resumed;
    int music = music_state() == MUSIC_PLAYING;
    if (music)
        music_toggle_pause();
    if (ui->video_player && s_sleeping)
        videoplay_sleep(ui->video_player, 1);
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

    boot_run(&s_running, NULL);
    if (music)
        music_toggle_pause();
    if (ui->video_player && wake_event)
        videoplay_woke(ui->video_player);
    ui_replay_intro(ui);
    preview_pause(in_settings); /* i inställningarna förblir den pausad */
}

/* Frigör allt i omvänd ordning – inför spelstart, USB-läge eller avslut. */
static void app_stop(EntryList *list, Ui *ui)
{
    sys_home_release(); /* spelet, XMB och USB-läget ska ha systemets HOME-dialog */
    ui_video_player_close(ui); /* release its audio lease and drain video jobs before preview/music/graphics teardown */
    umd_stop();
    loader_stop(); /* före music_term: laddaren läser musiklistan (omslagen) */
    preview_stop(); /* före music_term: förhandsvisningen använder samma ljudmoduler */
    music_term();
    sound_term();
    ui_free(ui);
    logo_free();
    theme_term();
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

static int s_usb_mode; /* 1 = datorn äger minneskortet */

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
    /* ♪ (NOTE) eller L + R + START tar en skärmbild (PNG) i ms0:/PICTURE/PSP5 – inte i USB-läget,
     * där datorn äger minnet och en skrivning från PSP:n kan förstöra FAT (issue #40) */
    u32 combo = PSP_CTRL_LTRIGGER | PSP_CTRL_RTRIGGER | PSP_CTRL_START;
    if (!s_usb_mode && ((in->pressed & PSP_CTRL_NOTE) || ((pad.Buttons & combo) == combo && (*old & combo) != combo))) {
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
    s_usb_mode = 1;
    while (s_running) {
        UiInput in;
        read_pad(&in, &old, 0);
        if (in.pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS))
            break;
        t += 1 / 60.0f;
        osd_update(1 / 60.0f);

        int state = sys_usb_state();
        const Theme *th = theme_cur();
        gfx_begin(THEME_INK(th, panel_alt, RGBA(20, 20, 24, 255)));
        static const float neutral[3] = { 120, 120, 120 };
        theme_backdrop(th, t, neutral, 1, 1); /* som inställningarna */
        u32 usb_main = THEME_INK(th, text, RGBA(238, 238, 242, 255));
        u32 usb_dim = THEME_INK(th, text_dim, RGBA(160, 162, 172, 255));
        text_draw(28, 34, 0.95f, 0, usb_main, ALIGN_LEFT, L(S_USB_MODE));
        float pulse = 0.6f + 0.4f * sinf(t * 3);
        u32 ring_base = th->ink && th->ink->dark ? th->ink->border_hi : THEME_INK(th, shadow, RGBA(255, 255, 255, 255));
        gfx_circle(SCREEN_W / 2, 120, 26, COLOR_ALPHA(ring_base, state == 2 ? (th->ink ? 70 : 40) : (int)((th->ink ? 55 : 30) * pulse)));
        u32 ok_col = THEME_INK(th, ok, RGBA(120, 220, 150, 255));
        u32 wait_col = THEME_INK(th, fg_hi, RGBA(236, 236, 240, 255));
        gfx_circle(SCREEN_W / 2, 120, 9, state == 2 ? ok_col : COLOR_ALPHA(wait_col, (int)(255 * pulse)));
        text_draw(SCREEN_W / 2, 176, 0.75f, 0, usb_main, ALIGN_CENTER,
                  L(state == 2 ? S_USB_CONNECTED : S_USB_WAITING));
        text_draw(SCREEN_W / 2, 196, 0.56f, 0, usb_dim, ALIGN_CENTER,
                  L(S_USB_EJECT));
        theme_button(th, SCREEN_W / 2 - 40, 246, 6.5f, BTN_CIRCLE, 0, RGBA(236, 236, 240, 255), RGBA(20, 20, 26, 255));
        text_draw(SCREEN_W / 2 - 28, 250.5f, 0.58f, 0, usb_main, ALIGN_LEFT, L(S_USB_EXIT));
        osd_draw();
        if (t < 0.25f) /* tonas in från svart, som filhanteraren */
            gfx_rect(0, 0, SCREEN_W, SCREEN_H, RGBA(0, 0, 0, (int)(255 * (1 - t / 0.25f))));
        gfx_end();
    }
    s_usb_mode = 0;
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
    if (action == SA_XMB || action == SA_UMD_VIDEO)
        sceIoRemove(START_MARK);
    else
        start_confirmed();
    if (action == SA_UMD_VIDEO) {
        /* UMD Video-ISO (issue #27): monteras i XMB. Går det inte står psp5 kvar. */
        char iso[sizeof(ui->umd_video_path)];
        snprintf(iso, sizeof(iso), "%s", ui->umd_video_path);
        if (launch_umd_video(iso) < 0) {
            start_confirmed(); /* psp5 fortsätter som vanligt */
            ui_toast(ui, L(S_VIDEO_FAILED));
            sound_play(SND_ERROR);
            return 0;
        }
    }
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
    case SA_UMD_VIDEO: sys_exit_xmb(); break; /* ISO:n är monterad – XMB startar med skivan */
    case SA_RESTART:  sys_restart(); break;
    case SA_SHUTDOWN: sys_shutdown(); break;
    case SA_RECOVERY: sys_recovery(); break;
    }
    for (;;)
        sceKernelDelayThread(100000); /* systemet tar över */
}

/* [medhatk] Standby Screen event loop:
 * - Preserves the current system/user display brightness (no brightness or CPU clock changes).
 * - Feeds sys_keep_awake() every ~1s and bypasses psp5's idle/sleep timers while open.
 * - Keeps read_pad() running so hardware HOME handling & watchdog stay active.
 * - Renders continuously using the normal launcher vblank cadence. */
static int standby_mode(EntryList *list, Ui *ui, u32 *old)
{
    (void)list;
    ui_standby_open(ui);
    preview_pause(1);
    sys_keep_awake();

    int result = SB_NONE;
    int leaving = 0;
    u64 last = sceKernelGetSystemTimeWide();
    u64 last_awake = last;
    UiInput in = { 0, *old, 128, 128 };

    while (s_running) {
        if (testrun_take_sleep())
            s_sleeping = 1;
        if (s_sleeping) {
            result = SB_NONE;
            break;
        }
        read_pad(&in, old, g_control_center);

        u64 now = sceKernelGetSystemTimeWide();
        float dt = (now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f)
            dt = 0.05f;
        osd_update(dt);
        if (now - last_awake >= 1000000ULL) {
            sys_keep_awake();
            last_awake = now;
        }

        /* [medhatk] Smooth 0.20s fade-to-black before exiting back to launcher or triggering Sleep/Shut down */
        if (leaving) {
            ui->time += dt;
            ui->sb_leave += dt / 0.20f;
            if (ui->sb_leave > 1.0f)
                ui->sb_leave = 1.0f;
            ui_draw_standby(ui);
            if (ui->sb_leave >= 1.0f)
                break;
            continue;
        }

        int r = ui_standby_update(ui, &in, dt);
        if (r != SB_NONE) {
            result = r;
            leaving = 1;
            ui->sb_leave = 0.001f;
            ui_draw_standby(ui);
            continue;
        }

        ui_draw_standby(ui);
    }

    preview_pause(0);
    *old = in.held;
    return result;
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
    lastplayed_load(DATA_DIR); /* when psp5 last started each game (Information) */
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
    if (s_safe) {
        ui_toast(&ui, L(S_SAFE_START));
        sound_play(SND_MSG_TOAST);
    }
#ifdef PSP5_TEST
    sound_dump("ms0:/PSP5/shots/sounds"); /* testbygge: ljuden som WAV */
#endif

    s_sleeping = s_resumed = 0; /* viloläge under startskärmen räknas inte */
    int in_settings = 0;
    int in_info = 0; /* the Game / App Information screen is open */
    int in_savedata = 0; /* the Saved Data Manager screen is open */
    unsigned char hide_dupes = 0, all_games = 0; /* hur flikarna byggdes när inställningarna öppnades */
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
                sleep_and_wake(&ui, in_settings || in_info || in_savedata);
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
        if (ui.video_player)
            active = 1; /* keep the video visible; playback itself does not generate pad activity */
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

        if (in_savedata) {
            UiInput none = { 0, 0, 128, 128 };
            int close = savedata_update(&in, dt);
            ui_update(&ui, &none, dt);
            if (close) {
                savedata_close();
                in_savedata = 0;
                if (!in_settings)
                    preview_pause(0);
                if (in_settings)
                    settings_draw();
                else
                    ui_draw(&ui);
            } else {
                savedata_draw();
            }
            continue;
        }
        if (in_settings) {
            /* HOME eller SELECT stänger inställningarna och öppnar Control Center */
            int home = (in.pressed & (PSP_CTRL_HOME | PSP_CTRL_SELECT)) != 0;
            int action = home ? SA_CLOSE : settings_update(&in, dt);
            if (action == SA_CLOSE) {
                in_settings = 0;
                preview_pause(0);
                if (settings_commit()) {
                    ui_toast(&ui, L(S_SAVED));
                    sound_play(SND_TOAST);
                }
                ui.confirm_circle = sys_confirm_is_circle();
                if (g_hide_dupes != hide_dupes || g_all_games != all_games)
                    ui_relist(&ui, 0, 0); /* bygg om Spel och Spelbiblioteket */
                if (home) {
                    ui_open_cc(&ui);
                }
            } else if (action == SA_SAVEDATA) {
                settings_commit();
                ui.confirm_circle = sys_confirm_is_circle();
                savedata_open(NULL, NULL, ui.confirm_circle);
                in_savedata = 1;
                old = in.held;
                continue;
            } else if (action != SA_NONE) {
                settings_commit();
                ui.confirm_circle = sys_confirm_is_circle();
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

        /* Game / App Information: the list stands still while it is open (no UMD refresh), pictures keep loading */
        if (in_info) {
            UiInput none = { 0, 0, 128, 128 };
            int close = info_update(&in, dt);
            ui_update(&ui, &none, dt);
            if (close) {
                info_close();
                preview_pause(0);
                in_info = 0;
                ui_draw(&ui);
            } else {
                info_draw();
            }
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
            all_games = g_all_games;
            preview_pause(1); /* spelets musik ska inte spela bakom inställningarna */
            settings_open();
            /* efter inläsningen (typsnitt från flash0, ARK:s filer): tung läsning medan ljudet
             * spelade gav ett skrapande ljud i ~0,1 s (issue #2) */
            sound_play(SND_SIDE_IN);
            in_settings = 1;
            continue;
        }
        /* [medhatk] Launch Standby Screen from Control Center and reset idle/dim timers upon unlock */
        if (action == UI_STANDBY) {
            start_confirmed();
            int r = standby_mode(&list, &ui, &old);
            last = sceKernelGetSystemTimeWide();
            idle = 0;
            dark = 0;
            if (r == SB_SLEEP) {
                sys_suspend();
                for (int k = 0; k < 120 && !s_sleeping && s_running; k++) {
                    gfx_begin(RGBA(0, 0, 0, 255));
                    gfx_end();
                }
            } else if (r == SB_SHUTDOWN) {
                run_action(SA_SHUTDOWN, &list, &ui);
            }
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
        if (action == UI_INFO && ui_selected(&ui) >= 0) {
            preview_pause(1);
            info_open(&list.items[ui_selected(&ui)], ui_selected(&ui), sys_confirm_is_circle());
            sound_play(SND_DETAIL_IN);
            in_info = 1;
            continue;
        }
        if (action == UI_SAVEDATA && ui_selected(&ui) >= 0) {
            const Entry *chosen = &list.items[ui_selected(&ui)];
            preview_pause(1);
            savedata_open(chosen->game_id, chosen->title, sys_confirm_is_circle());
            sound_play(SND_DETAIL_IN);
            in_savedata = 1;
            continue;
        }
        if (action == UI_LAUNCH && ui_selected(&ui) >= 0) {
            Entry chosen = list.items[ui_selected(&ui)];
            lastplayed_note_launch(&chosen); /* shown as "Last played" in Information */
            sound_wait(700);      /* låt startljudet klinga ut */
            app_stop(&list, &ui); /* frigör allt minne innan spelet laddas */
            start_confirmed();    /* ett spel som hänger sig räknas inte mot psp5 */
            set_ingame_mark();
            launch_entry(&chosen);
            /* hit kommer vi bara om starten misslyckades */
            sceIoRemove(INGAME_MARK);
            app_start(&list, &ui, 0);
            ui_toast(&ui, L(S_LAUNCH_FAILED));
            sound_play(SND_ERROR);
            continue;
        }
        ui_draw(&ui);
    }

    /* HOME → Avsluta: till XMB (även när psp5 är ARK:s launcher) */
    if (in_settings)
        settings_commit();
    if (in_savedata)
        savedata_close();
    if (in_info)
        info_close();
    app_stop(&list, &ui);
    sceIoRemove(START_MARK);
    sys_exit_xmb();
    sceKernelExitGame();
    return 0;
}

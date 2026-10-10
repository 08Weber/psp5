/* psp5 – egen launcher för PSP med ARK-5. Kan köras som vanlig app
 * (ms0:/PSP/GAME/PSP5) eller som ARK:s launcher (ARK_01234/VBOOT.PBP). */
#include <pspkernel.h>
#include <pspctrl.h>
#include <psputility.h>
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

PSP_MODULE_INFO("psp5", PSP_MODULE_USER, 0, 1);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

/* psp5:s egna filer (omslag, cache) – samma plats oavsett varifrån psp5 körs */
#define DATA_DIR "ms0:/PSP5"

static volatile int s_running = 1;
static char s_self[256];
static char s_list_cache[256];
static u64 s_scan_start;

static int exit_callback(int arg1, int arg2, void *common)
{
    s_running = 0;
    return 0;
}

static int callback_thread(SceSize args, void *argp)
{
    int cb = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(cb);
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

/* Bygger upp allt: grafik, typsnitt, spellista, laddartråd. */
static void app_start(EntryList *list, Ui *ui)
{
    gfx_init();
    font_init(0);
    s_scan_start = sceKernelGetSystemTimeWide();
    scan_all(list, s_self, s_list_cache, scan_progress);
    if (has_non_ascii(list)) {
        font_term();
        font_init(1);
    }
    loader_start(list);
    ui_init(ui, list, confirm_is_circle(), launch_supported());
}

/* Frigör allt i omvänd ordning – inför spelstart, USB-läge eller avslut. */
static void app_stop(EntryList *list, Ui *ui)
{
    loader_stop();
    ui_free(ui);
    font_term();
    gfx_term();
    scan_free(list);
}

static void read_pad(UiInput *in, u32 *old)
{
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    pad.Buttons = testrun_buttons(pad.Buttons); /* bara i testbyggen */
    in->pressed = pad.Buttons & ~*old;
    in->held = pad.Buttons;
    in->lx = pad.Lx;
    *old = pad.Buttons;
}

/* USB-läge: inget annat rör minneskortet medan datorn är ansluten. */
static void usb_mode(void)
{
    gfx_init();
    font_init(0);
    if (!sys_usb_start()) {
        gfx_term();
        font_term();
        return;
    }
    u32 old = PSP_CTRL_CROSS; /* ignorera knappen som startade läget */
    float t = 0;
    while (s_running) {
        UiInput in;
        read_pad(&in, &old);
        if (in.pressed & (PSP_CTRL_CIRCLE | PSP_CTRL_CROSS))
            break;
        t += 1 / 60.0f;

        int state = sys_usb_state();
        gfx_begin(RGBA(20, 20, 24, 255));
        gfx_gradient_v(0, 0, SCREEN_W, SCREEN_H, RGBA(48, 46, 50, 255), RGBA(22, 23, 29, 255));
        gfx_glow(70, -60, 60, 60, 120, RGBA(215, 198, 186, 80));
        text_draw(28, 34, 0.95f, 0, RGBA(238, 238, 242, 255), ALIGN_LEFT, "USB-läge");
        float pulse = 0.6f + 0.4f * sinf(t * 3);
        gfx_circle(SCREEN_W / 2, 120, 26, RGBA(255, 255, 255, state == 2 ? 40 : (int)(30 * pulse)));
        gfx_circle(SCREEN_W / 2, 120, 9, state == 2 ? RGBA(120, 220, 150, 255) : RGBA(236, 236, 240, (int)(255 * pulse)));
        text_draw(SCREEN_W / 2, 176, 0.75f, 0, RGBA(238, 238, 242, 255), ALIGN_CENTER,
                  state == 2 ? "Ansluten till datorn" : "Väntar på USB-kabel");
        text_draw(SCREEN_W / 2, 196, 0.56f, 0, RGBA(160, 162, 172, 255), ALIGN_CENTER,
                  "Mata ut PSP:n i datorn innan du avslutar");
        gfx_button(SCREEN_W / 2 - 40, 246, 6.5f, BTN_CIRCLE, RGBA(236, 236, 240, 255), RGBA(20, 20, 26, 255));
        text_draw(SCREEN_W / 2 - 28, 250.5f, 0.58f, 0, RGBA(238, 238, 242, 255), ALIGN_LEFT, "Avsluta USB");
        gfx_end();
    }
    sys_usb_stop();
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
    switch (action) {
    case SA_SUSPEND:
        sys_suspend();
        return 0;
    case SA_USB:
        app_stop(list, ui);
        usb_mode();
        return 1; /* filer kan ha ändrats – skanna om */
    case SA_CLEAR_CACHE:
        app_stop(list, ui);
        delete_dir_files(DATA_DIR "/cache");
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
    snprintf(s_list_cache, sizeof(s_list_cache), "%s/cache/list.bin", DATA_DIR);
    sceIoMkdir(DATA_DIR, 0777);
    art_init(DATA_DIR);
    sys_init(s_self);
    testrun_init();

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    EntryList list = { 0 };
    Ui ui;
    app_start(&list, &ui);

    int in_settings = 0;
    u32 old = 0;
    u64 last = sceKernelGetSystemTimeWide();
    while (s_running) {
        UiInput in;
        read_pad(&in, &old);

        u64 now = sceKernelGetSystemTimeWide();
        float dt = (now - last) / 1000000.0f;
        last = now;
        if (dt > 0.05f)
            dt = 0.05f;

        if (in_settings) {
            int action = settings_update(&in, dt);
            if (action == SA_CLOSE) {
                in_settings = 0;
                if (settings_commit())
                    ui_toast(&ui, "Sparat. Ändringarna gäller efter omstart");
            } else if (action != SA_NONE) {
                settings_commit();
                if (run_action(action, &list, &ui)) {
                    app_start(&list, &ui);
                    in_settings = 0;
                }
                old = in.held;
                continue;
            }
            /* bilder och bakgrund fortsätter laddas i bakgrunden */
            UiInput none = { 0 };
            ui_update(&ui, &none, dt);
            if (in_settings)
                settings_draw();
            else
                ui_draw(&ui);
            continue;
        }

        int action = ui_update(&ui, &in, dt);
        if (action == UI_SETTINGS) {
            settings_open();
            in_settings = 1;
            continue;
        }
        if (action == UI_LAUNCH && ui_selected(&ui) >= 0) {
            Entry chosen = list.items[ui_selected(&ui)];
            app_stop(&list, &ui); /* frigör allt minne innan spelet laddas */
            launch_entry(&chosen);
            /* hit kommer vi bara om starten misslyckades */
            app_start(&list, &ui);
            ui_toast(&ui, "Kunde inte starta spelet");
            continue;
        }
        ui_draw(&ui);
    }

    /* HOME → Avsluta: till XMB (även när psp5 är ARK:s launcher) */
    app_stop(&list, &ui);
    sys_exit_xmb();
    sceKernelExitGame();
    return 0;
}

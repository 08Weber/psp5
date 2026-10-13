/* psp5 – made by Mille Weber */
/* Volymknapparna går direkt till systemet, och en vanlig app kan inte läsa dem. Volymen
 * finns i kärnans sceImposeGetParam: ARK hittar funktionen (och översätter NID:en för
 * 6.60/6.61) och kubridge anropar den i kärnläge. Ändras värdet visas indikatorn. */
#include <pspkernel.h>
#include <pspimpose_driver.h>
#include <kubridge.h>
#include <systemctrl.h>
#include <math.h>
#include <stdio.h>
#include "osd.h"
#include "gfx.h"
#include "launch.h"
#include "testrun.h"

#define NID_GET_PARAM     0x531C9778 /* sceImposeGetParam (ARK översätter) */
#define NID_GET_PARAM_660 0xDC3BECFF /* samma i 6.60/6.61, om översättningen saknas */
#define MAX_VOLUME 30
#define SHOW_TIME  1.6f /* sekunder efter senaste ändringen */

static void *s_get_param; /* kärnadress, NULL = går inte att läsa (t.ex. PPSSPP) */
static int s_volume = -1, s_muted = -1;
static float s_shown;     /* tid kvar att visa */
static float s_anim;      /* 0→1 in-/uttoning */
static float s_level;     /* animerad stapel 0–1 */

void osd_init(void)
{
    s_get_param = NULL;
    s_volume = s_muted = -1;
    s_shown = s_anim = 0;
    if (!launch_supported())
        return;
    u32 f = sctrlHENFindFunction("sceImpose_Driver", "sceImpose_driver", NID_GET_PARAM);
    if (!f)
        f = sctrlHENFindFunction("sceImpose_Driver", "sceImpose_driver", NID_GET_PARAM_660);
    s_get_param = (void *)f;
}

static int get_param(int param)
{
    if (!s_get_param)
        return -1;
    KernelCallArg a = { 0 };
    a.arg1 = param;
    if (kuKernelCall(s_get_param, &a) < 0)
        return -1;
    return (int)a.ret1;
}

void osd_update(float dt)
{
    int vol = get_param(PSP_IMPOSE_MAIN_VOLUME), mute = get_param(PSP_IMPOSE_MUTE);
    if (testrun_volume() >= 0) { /* testskriptet låtsas trycka på volymknapparna */
        vol = testrun_volume() & 0xFF;
        mute = testrun_volume() >> 8;
    }
    if (vol >= 0 && vol <= MAX_VOLUME) {
        /* första avläsningen visas inte – bara ändringar */
        if (s_volume >= 0 && (vol != s_volume || (mute >= 0 && mute != s_muted)))
            s_shown = SHOW_TIME;
        s_volume = vol;
        s_muted = mute > 0;
    }
    if (s_shown > 0)
        s_shown -= dt;
    float target = s_shown > 0 ? 1 : 0;
    s_anim += (target - s_anim) * (1 - expf(-(target > s_anim ? 18.0f : 8.0f) * dt));
    float lv = s_muted ? 0 : s_volume / (float)MAX_VOLUME;
    s_level += (lv - s_level) * (1 - expf(-20.0f * dt));
}

/* Högtalare (låda + kon) med ljudvågor efter nivån, eller ett kryss när ljudet är av */
static void draw_speaker(float x, float y, float level, int muted, u32 c, u32 dim)
{
    gfx_rect(x, y - 3, 4, 6, c);
    float cone[8] = { x + 3.5f, y - 3, x + 9, y - 7.5f, x + 3.5f, y + 3, x + 9, y + 7.5f };
    u32 cc[4] = { c, c, c, c };
    gfx_quad(cone, cc);
    if (muted) {
        float a[4] = { x + 12, y - 4, x + 19, y + 4 }, b[4] = { x + 19, y - 4, x + 12, y + 4 };
        gfx_stroke(a, cc, 2, 1.6f);
        gfx_stroke(b, cc, 2, 1.6f);
        return;
    }
    for (int k = 0; k < 3; k++) { /* tre bågar, de som inte når upp är svaga */
        float r = 5 + k * 3.5f, pts[10];
        u32 col = level > (k + 0.5f) / 3.5f ? c : dim, cols[5] = { col, col, col, col, col };
        for (int i = 0; i < 5; i++) {
            float ang = (-40 + i * 20) * 3.14159265f / 180;
            pts[i * 2] = x + 9 + cosf(ang) * r;
            pts[i * 2 + 1] = y + sinf(ang) * r;
        }
        gfx_stroke(pts, cols, 5, 1.4f);
    }
}

void osd_draw(void)
{
    if (s_anim < 0.01f || s_volume < 0)
        return;
    float a = s_anim, w = 196, h = 34;
    float x = (SCREEN_W - w) / 2, y = 6 - (1 - a) * 10; /* glider ner uppifrån */
    gfx_glow(x, y + 2, w, h, 10, RGBA(0, 0, 0, (int)(130 * a)));
    gfx_round_rect(x, y, w, h, h / 2, RGBA(34, 35, 41, (int)(255 * a)));
    u32 light = RGBA(240, 240, 245, (int)(255 * a)), dim = RGBA(240, 240, 245, (int)(70 * a));
    draw_speaker(x + 16, y + h / 2, s_level, s_muted, light, dim);
    /* stapeln som på PS5: tunn ljus fyllning på en svag bana */
    float bx = x + 46, bw = w - 46 - 42, by = y + h / 2 - 2;
    gfx_round_rect(bx, by, bw, 4, 2, RGBA(255, 255, 255, (int)(45 * a)));
    if (s_level > 0.005f)
        gfx_round_rect(bx, by, fmaxf(4, bw * s_level), 4, 2, light);
    char num[8];
    snprintf(num, sizeof(num), "%d", s_muted ? 0 : s_volume);
    text_draw(x + w - 18, y + h / 2 + 5, 0.62f, 0, light, ALIGN_RIGHT, num);
}

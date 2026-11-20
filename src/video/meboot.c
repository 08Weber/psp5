/* psp5 authentic – the Media Engine's firmware image for the hardware video decoder (see meboot.h) */
#include "meboot.h"

#include <pspkernel.h>
#include <kubridge.h>
#include <systemctrl.h>

#include "launch.h"

static u32 s_func;
static unsigned s_nid;
static int s_tried;
static int s_cur;
static int s_devkit;
static unsigned s_mask;
static int s_mod;

/* sceMeBootStart is exported by the library sceMeCore_driver of the module sceMeCodecWrapper (me_wrapper.prx, found in ARK-4's contrib/psplibdoc_660.xml). Its NID changes with the firmware; the NID
 * for each is the table that PMPlayer Ultraviolet's bridge uses (and that works on PSPs): the one of the system's devkit version. The first run on a PSP-1000 ("not found") had the module under a guessed
 * name (sceMeCore_Driver), nothing else was wrong with it as far as is known. Only the NID of the system is called; the others are looked for only to be written to the log (see meboot_mask()). */
static const struct { int devkit; unsigned nid; } NID_OF[] = {
    { 0x03070000, 0x47DB48C2 }, /* below 3.70 */
    { 0x03080000, 0xC287AD90 }, /* 3.71 */
    { 0x03090500, 0xD857CF93 }, /* 3.80 */
    { 0x05000000, 0x8988AD49 }, /* 3.95 */
    { 0x06020000, 0x051C1601 }, /* 5.00 */
    { 0x06030500, 0x3A2E60BB }, /* 6.20 */
    { 0x06060000, 0x99E4DBFA }, /* 6.35 */
    { 0x7FFFFFFF, 0x5DFF5C50 }, /* 6.60 and later */
};
#define N_NID ((int)(sizeof(NID_OF) / sizeof(NID_OF[0])))
static const char *const MODS[] = { "sceMeCodecWrapper", "sceMeCore_Driver", "sceMeCore_driver" };

static void resolve(void)
{
    if (s_tried)
        return;
    s_tried = 1;
    if (!launch_supported())
        return;
    s_devkit = sceKernelDevkitVersion();
    unsigned want = NID_OF[N_NID - 1].nid;
    for (int i = 0; i < N_NID; i++)
        if (s_devkit < NID_OF[i].devkit) {
            want = NID_OF[i].nid;
            break;
        }
    for (int m = 0; m < (int)(sizeof(MODS) / sizeof(MODS[0])); m++) {
        for (int i = 0; i < N_NID; i++)
            if (sctrlHENFindFunction(MODS[m], "sceMeCore_driver", NID_OF[i].nid))
                s_mask |= 1u << i; /* (for the log only) */
        u32 f = sctrlHENFindFunction(MODS[m], "sceMeCore_driver", want);
        if (f) {
            s_func = f;
            s_nid = want;
            s_mod = m + 1;
            break;
        }
    }
}

int meboot_available(void)
{
    resolve();
#ifdef PSP5_HW_FAKE
    return 1; /* (the test build with a stand-in decoder, hwfake.inc) */
#endif
    return s_func != 0;
}

unsigned meboot_nid(void) { return s_nid; }
int meboot_devkit(void) { return s_devkit; }
unsigned meboot_mask(void) { return s_mask; }
int meboot_module(void) { return s_mod; }

int meboot_start(int type)
{
    resolve();
#if defined(PSP5_TEST) && (!defined(PSP5_VIDEO_REAL) || defined(PSP5_HW_FAKE)) /* (PPSSPP has no ARK and no Engine to start: the rest of the route is what its tests are for) */
    if (!s_func && type >= 0) {
        s_cur = type;
        return 0;
    }
#endif
    if (!s_func || type < 0)
        return -1;
    if (type == s_cur)
        return 0;
    KernelCallArg a = { 0 };
    a.arg1 = (u32)type;
    int r = kuKernelCall((void *)s_func, &a);
    if (r < 0)
        return r;
    if ((int)a.ret1 < 0)
        return (int)a.ret1;
    s_cur = type;
    return 0;
}

int meboot_current(void) { return s_cur; }

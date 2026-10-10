/* psp5 – made by Mille Weber */
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "sound.h"
#include "lang.h"

void config_load(const char *data_dir)
{
    char path[200], buf[256] = "";
    snprintf(path, sizeof(path), "%s/psp5.cfg", data_dir);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd >= 0) {
        int n = sceIoRead(fd, buf, sizeof(buf) - 1);
        sceIoClose(fd);
        buf[n > 0 ? n : 0] = 0;
        const char *p;
        if ((p = strstr(buf, "sound=")))
            g_sound_on = p[6] != '0';
        if ((p = strstr(buf, "lang=")))
            g_lang_setting = lang_from_code(p + 5);
    }
    lang_apply();
}

void config_save(const char *data_dir)
{
    char path[200], buf[64];
    snprintf(path, sizeof(path), "%s/psp5.cfg", data_dir);
    int n = snprintf(buf, sizeof(buf), "sound=%d\nlang=%s\n", g_sound_on ? 1 : 0, lang_code(g_lang_setting));
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, buf, n);
        sceIoClose(fd);
    }
}

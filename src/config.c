/* psp5 – made by Mille Weber */
#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "sound.h"
#include "lang.h"
#include "theme.h"

unsigned char g_boot_anim = 1;
unsigned char g_sort_xmb;
unsigned char g_hide_dupes = 1;
unsigned char g_battery;
unsigned char g_previews;
char g_wallpaper[192];

void config_load(const char *data_dir)
{
    char path[200], buf[512] = "";
    snprintf(path, sizeof(path), "%s/psp5.cfg", data_dir);
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if (fd >= 0) {
        int n = sceIoRead(fd, buf, sizeof(buf) - 1);
        sceIoClose(fd);
        buf[n > 0 ? n : 0] = 0;
        const char *p;
        if ((p = strstr(buf, "sound=")))
            g_sound_on = p[6] != '0';
        if ((p = strstr(buf, "bootanim=")))
            g_boot_anim = p[9] != '0';
        if ((p = strstr(buf, "lang=")))
            g_lang_setting = lang_from_code(p + 5);
        if ((p = strstr(buf, "theme=")))
            g_theme = theme_from_code(p + 6);
        if ((p = strstr(buf, "themeart=")))
            g_theme_art = p[9] != '0';
        if ((p = strstr(buf, "sortxmb=")))
            g_sort_xmb = p[8] != '0';
        if ((p = strstr(buf, "hidedupes=")))
            g_hide_dupes = p[10] != '0';
        if ((p = strstr(buf, "battery=")) && p[8] >= '0' && p[8] <= '2')
            g_battery = p[8] - '0';
        if ((p = strstr(buf, "previews=")))
            g_previews = p[9] != '0';
        if ((p = strstr(buf, "wallpaper="))) {
            p += 10;
            int len = strcspn(p, "\r\n");
            snprintf(g_wallpaper, sizeof(g_wallpaper), "%.*s", len, p);
        }
    }
    lang_apply();
}

void config_save(const char *data_dir)
{
    char path[200], buf[512];
    snprintf(path, sizeof(path), "%s/psp5.cfg", data_dir);
    int n = snprintf(buf, sizeof(buf),
                     "sound=%d\nbootanim=%d\nlang=%s\ntheme=%s\nthemeart=%d\nsortxmb=%d\nhidedupes=%d\nbattery=%d\n"
                     "previews=%d\nwallpaper=%s\n",
                     g_sound_on ? 1 : 0, g_boot_anim ? 1 : 0, lang_code(g_lang_setting), theme_cur()->code,
                     g_theme_art ? 1 : 0, g_sort_xmb ? 1 : 0, g_hide_dupes ? 1 : 0, g_battery, g_previews ? 1 : 0,
                     g_wallpaper);
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0) {
        sceIoWrite(fd, buf, n);
        sceIoClose(fd);
    }
}

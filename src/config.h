/* psp5 – made by Mille Weber */
#ifndef CONFIG_H
#define CONFIG_H

/* psp5:s egna inställningar (ljud, språk, tema) i <data_dir>/psp5.cfg:
 *   sound=1
 *   bootanim=1       (startskärmen)
 *   lang=auto        (auto, en, sv, de, fr, es, it, nl, pt, ru, ja)
 *   theme=ps5        (se theme.c)
 *   themeart=1       (spelens bilder bakom korten)
 *   sortxmb=0        (Spel och Appar: 0 = A–Ö, 1 = som XMB, nyaste mappen först)
 *   hidedupes=1      (samma spel-ID och titel två gånger: visa bara det första)
 *   battery=0        (bredvid batteriet: 0 = inget, 1 = procent, 2 = tid kvar)
 *   previews=0       (spelets video ICON1.PMF och musik SND0.AT3 på valt kort, som i XMB)
 *   wallpaper=       (egen bakgrundsbild, t.ex. ms0:/PICTURE/bild.jpg – tom = temats) */
extern unsigned char g_boot_anim;
extern unsigned char g_sort_xmb;
extern unsigned char g_hide_dupes;
extern unsigned char g_battery;
extern unsigned char g_previews;
extern char g_wallpaper[192];

void config_load(const char *data_dir);
void config_save(const char *data_dir);

#endif

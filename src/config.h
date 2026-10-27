/* psp5 – made by 08Weber */
#ifndef CONFIG_H
#define CONFIG_H

/* psp5:s egna inställningar (ljud, språk, tema) i <data_dir>/psp5.cfg:
 *   sound=1
 *   bootanim=1       (startskärmen)
 *   lang=auto        (auto, en, sv, de, fr, es, it, nl, pt, ru, ja)
 *   theme=ps5        (se theme.c)
 *   themeart=1       (spelens bilder bakom korten)
 *   sortxmb=0        (Spel och Appar: 0 = A–Ö, 1 = som XMB, 2 = senast spelat, 3 = egen ordning)
 *   hidedupes=1      (samma spel-ID och titel två gånger: visa bara det första)
 *   battery=0        (bredvid batteriet: 0 = inget, 1 = procent, 2 = tid kvar)
 *   previews=0       (spelets video ICON1.PMF och musik SND0.AT3 på valt kort, som i XMB)
 *   shuffle=0        (musiken spelar låtarna i slumpad ordning)
 *   screenoff=0      (skärmen släcks efter 0 = aldrig, 1/2/3/4 = 1/3/5/10 min utan knapptryck)
 *   autosleep=0      (viloläge efter 0 = aldrig, 1/2/3/4 = 5/10/15/30 min – inte medan musik spelar)
 *   control_center=1 (HOME öppnar psp5:s Control Center i stället för systemets/Custom Home)
 *   wallpaper=       (egen bakgrundsbild, t.ex. ms0:/PICTURE/bild.jpg – tom = temats) */
extern unsigned char g_shuffle;
extern unsigned char g_screen_off;
extern unsigned char g_auto_sleep;
extern unsigned char g_boot_anim;
extern unsigned char g_control_center;
/* Spel och Appar (sortxmb= i psp5.cfg, namnet från när det bara fanns två val).
 * Fästa spel står alltid först (ui.c). */
enum { SORT_NAME, SORT_XMB, SORT_PLAYED, SORT_CUSTOM, SORT_COUNT };
extern unsigned char g_sort;
extern unsigned char g_hide_dupes;
extern unsigned char g_battery;
extern unsigned char g_previews;
extern char g_wallpaper[192];

void config_load(const char *data_dir);
void config_save(const char *data_dir);

#endif

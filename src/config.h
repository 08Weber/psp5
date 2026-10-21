/* psp5 – made by Mille Weber */
#ifndef CONFIG_H
#define CONFIG_H

/* psp5:s egna inställningar (ljud, språk, tema) i <data_dir>/psp5.cfg:
 *   sound=1
 *   bootanim=1       (startskärmen)
 *   lang=auto        (auto, en, sv, de, fr, es, it, nl, pt, ru, ja)
 *   theme=ps5        (se theme.c)
 *   themeart=1       (spelens bilder bakom korten)
 *   sortxmb=0        (Spel och Appar: 0 = A–Ö, 1 = som XMB, nyaste mappen först) */
extern unsigned char g_boot_anim;
extern unsigned char g_sort_xmb;

void config_load(const char *data_dir);
void config_save(const char *data_dir);

#endif

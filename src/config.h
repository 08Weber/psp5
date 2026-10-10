/* psp5 – made by Mille Weber */
#ifndef CONFIG_H
#define CONFIG_H

/* psp5:s egna inställningar (ljud, språk) i <data_dir>/psp5.cfg:
 *   sound=1
 *   lang=auto        (auto, en, sv, de, fr, es, it, nl, pt, ru, ja) */
void config_load(const char *data_dir);
void config_save(const char *data_dir);

#endif

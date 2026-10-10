#ifndef PLUGINS_H
#define PLUGINS_H

#define MAX_PLUGINS 48

typedef struct {
    char name[48];     /* filnamn, t.ex. "leda.prx" */
    char where[24];    /* läsbar runlevel: "Alla", "Spel", "XMB", ... */
    unsigned char on;
    int file, line;    /* vilken fil/rad den kommer från */
} Plugin;

/* Läser ARK-mappens PLUGINS.TXT och ms0:/SEPLUGINS/PLUGINS.TXT. */
int plugins_load(const char *ark_dir);
int plugins_count(void);
Plugin *plugins_get(int i);
/* Skriver tillbaka ändrade filer – bara på/av-fältet ändras. */
int plugins_save(void);

#endif

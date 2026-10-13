/* psp5 – made by Mille Weber */
#ifndef ART_H
#define ART_H

#include "gfx.h"
#include "scan.h"

#define ICON_W 144
#define ICON_H 80
/* bakgrund: 2/3 av skärmen – liten fil, skalas upp mjukt */
#define BG_W 320
#define BG_H 182

/* `base_dir` = launcherns mapp, t.ex. "ms0:/PSP/GAME/PSP5". Skapar cache/ och covers/. */
void art_init(const char *base_dir);

/* Kortbild (8888, rundade hörn). `accent` får bildens medelfärg. NULL om ingen ikon finns. */
Texture *art_load_icon(const Entry *e, u32 *accent);

/* Bakgrund (5650, BG_W×BG_H): PIC1 skarp, annars ikonen kraftigt suddad. */
Texture *art_load_bg(const Entry *e);

#define COVER_SIZE 64
/* Omslag till en låt (8888, COVER_SIZE i kvadrat): ur ID3-taggen, annars cover.jpg/folder.jpg
 * m.fl. i albummappen. Sparas i cache/ per album. NULL om det inte finns något. */
Texture *art_load_cover(const char *mp3_path, const char *album);

#endif

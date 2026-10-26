/* psp5 – made by 08Weber */
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

/* Bakgrund (5650): PIC1 skarp i 480×272, annars ikonen kraftigt suddad (BG_W×BG_H). */
Texture *art_load_bg(const Entry *e);

/* Spelets logga (PIC0, 8888) utan genomskinliga kanter, högst LOGO_W×LOGO_H. NULL om den saknas. */
#define LOGO_W 200
#define LOGO_H 84
Texture *art_load_logo(const Entry *e);

#define COVER_SIZE 64
/* Omslag till en låt (8888, COVER_SIZE i kvadrat): ur ID3-taggen, annars cover.jpg/folder.jpg
 * m.fl. i albummappen. Sparas i cache/ per album. NULL om det inte finns något. */
Texture *art_load_cover(const char *mp3_path, const char *album);

/* Läser en fil ur spelet (offset/storlek i PBP eller ISO), eller PSP_GAME/<umd_name> på
 * UMD-skivan. Nyallokerad buffert (free), NULL om den saknas eller är större än max_size. */
void *art_read_part(const Entry *e, u32 offset, u32 size, const char *umd_name, u32 max_size);

/* Egen bakgrundsbild (JPEG/PNG, t.ex. ms0:/PICTURE/bild.jpg): beskuren och skalad till
 * skärmen (5650), sparad i cache/. NULL om filen saknas eller inte går att läsa. */
Texture *art_load_wallpaper(const char *file);

#endif

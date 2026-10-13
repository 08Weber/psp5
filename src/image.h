/* psp5 – made by Mille Weber */
#ifndef IMAGE_H
#define IMAGE_H

#include <psptypes.h>
#include "gfx.h"

/* RGBA8888-bild i vanligt minne (CPU-sidan, innan den blir textur). */
typedef struct {
    int w, h;
    u32 *px; /* ABGR, som GU */
} Image;

int img_decode_png(const void *data, u32 size, Image *out);
/* Omslag (JPEG eller PNG, efter innehållet). JPEG skalas ner i avkodaren men blir minst
 * min_side på den kortaste sidan. */
int img_decode_cover(const void *data, u32 size, Image *out, int min_side);
/* Beskär till en kvadrat runt mitten. */
int img_crop_square(Image *img);
void img_free(Image *img);
/* Skalar till dw×dh (ytmedelvärde vid förminskning, bilinjärt vid förstoring). */
int img_resize(const Image *src, int dw, int dh, Image *out);
void img_blur(Image *img, int radius);
void img_round_corners(Image *img, float radius);
u32 img_average(const Image *img);

/* Skapar textur (8888 eller 5650) från bilden. */
Texture *img_to_texture(const Image *img, int psm);

/* NTX: psp5:s egna råa texturfiler – laddas utan avkodning.
 * Header (16 byte): "NTX1", u16 w, u16 h, u16 psm, u16 stride, u32 reserv.
 * Därefter stride*h pixlar (4 byte för 8888, 2 för 5650). */
Texture *ntx_load(const char *path);
int ntx_save(const char *path, const Texture *t);

#endif

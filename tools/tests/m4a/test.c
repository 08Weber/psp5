/* psp5 – made by 08Weber */
/* Test av M4A-läsaren (src/m4a.c) på datorn – körs med tools/tests/m4a/run.sh.
 * Filerna kommer från gen.py: N rutor, ruta i är fsize(i) byte fylld med i & 255. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m4a.h"

#define N 500
static u32 fsize(u32 i) { return 100 + (i * 37) % 200; }

static SceUID open_file(const char *path, u32 *size)
{
    SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    assert(fd >= 0);
    *size = sceIoLseek32(fd, 0, PSP_SEEK_END);
    return fd;
}

/* rutan på off ska vara ruta i */
static void check_frame(SceUID fd, u32 i, u32 off, u32 len)
{
    unsigned char b[2];
    assert(len == fsize(i));
    sceIoLseek32(fd, off, PSP_SEEK_SET);
    assert(sceIoRead(fd, b, 1) == 1 && b[0] == (i & 255));
    sceIoLseek32(fd, off + len - 1, PSP_SEEK_SET);
    assert(sceIoRead(fd, b, 1) == 1 && b[0] == (i & 255));
}

static void check_frames(const char *path, u32 rate, int channels)
{
    u32 size, off, len;
    SceUID fd = open_file(path, &size);
    M4a m;
    assert(m4a_open(&m, fd, size) == 0);
    assert(m.rate == rate && m.channels == channels && m.nsamples == N);
    assert(m.duration > (float)N * 1024 / rate - 0.01f && m.duration < (float)N * 1024 / rate + 0.01f);
    for (u32 i = 0; i < N; i++) {
        assert(m4a_next(&m, &off, &len));
        check_frame(fd, i, off, len);
    }
    assert(!m4a_next(&m, &off, &len));
    for (u32 i = 0; i < N; i += 13) { /* hopp mitt i chunkar */
        m4a_seek(&m, i);
        assert(m4a_next(&m, &off, &len));
        check_frame(fd, i, off, len);
    }
    m4a_seek(&m, N);
    assert(!m4a_next(&m, &off, &len));
    m4a_seek(&m, N + 5); /* sista chunken påstår 13 rutor men har 4 (stsc) */
    assert(!m4a_next(&m, &off, &len));
    m4a_seek(&m, 0xFFFFFFFF);
    assert(!m4a_next(&m, &off, &len));
    m4a_free(&m);
    sceIoClose(fd);
}

static void check_tags(const char *path)
{
    u32 size, cover_size = 0;
    char title[96] = "", artist[64] = "", album[64] = "";
    int track = 0;
    SceUID fd = open_file(path, &size);
    assert(m4a_read_tags(fd, size, title, sizeof(title), artist, sizeof(artist), album, sizeof(album), &track));
    sceIoClose(fd);
    assert(!strcmp(title, "Låt åäö"));
    assert(!strcmp(artist, "Album Artist")); /* inget ©ART: albumartisten */
    assert(!strcmp(album, "Album"));         /* mellanslagen runt om bort */
    assert(track == 7);
    unsigned char *cover = m4a_cover(path, &cover_size);
    assert(cover && cover_size == 9002 && cover[0] == 0xFF && cover[1] == 0xD8);
    free(cover);
}

/* trasiga filer får inte krascha eller läsa utanför något (körs med AddressSanitizer) */
static void try_file(const unsigned char *data, u32 n)
{
    FILE *f = fopen("broken.m4a", "wb");
    fwrite(data, 1, n, f);
    fclose(f);
    u32 size, off, len;
    char title[96] = "", artist[64] = "", album[64] = "";
    int track = 0;
    SceUID fd = open_file("broken.m4a", &size);
    m4a_read_tags(fd, size, title, sizeof(title), artist, sizeof(artist), album, sizeof(album), &track);
    M4a m;
    if (m4a_open(&m, fd, size) == 0) {
        for (int k = 0; k < 100000 && m4a_next(&m, &off, &len); k++)
            ;
        m4a_seek(&m, 250);
        m4a_next(&m, &off, &len);
        m4a_free(&m);
    }
    sceIoClose(fd);
    free(m4a_cover("broken.m4a", &size));
}

static void check_broken(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    u32 n = ftell(f);
    unsigned char *data = malloc(n), *copy = malloc(n);
    fseek(f, 0, SEEK_SET);
    assert(fread(data, 1, n, f) == n);
    fclose(f);
    for (u32 cut = 0; cut < n; cut += 97)
        try_file(data, cut);
    srand(1);
    for (int k = 0; k < 300; k++) {
        memcpy(copy, data, n);
        for (int j = 0; j < 8; j++)
            copy[rand() % 12000] = rand(); /* ftyp och moov */
        try_file(copy, n);
    }
    free(data);
    free(copy);
}

int main(void)
{
    check_frames("front.m4a", 44100, 2);
    check_frames("tail.m4a", 44100, 2); /* moov sist, co64 */
    check_frames("notags.m4a", 22050, 1);
    check_tags("front.m4a");
    check_tags("tail.m4a");

    u32 size;
    char title[96] = "", artist[64] = "", album[64] = "";
    int track = 0;
    SceUID fd = open_file("notags.m4a", &size);
    assert(m4a_read_tags(fd, size, title, sizeof(title), artist, sizeof(artist), album, sizeof(album), &track));
    assert(!title[0] && !artist[0] && !album[0] && !track);
    sceIoClose(fd);
    assert(!m4a_cover("notags.m4a", &size));

    fd = open_file("alac.m4a", &size); /* ALAC: inte AAC, listas inte */
    M4a m;
    assert(!m4a_read_tags(fd, size, title, sizeof(title), artist, sizeof(artist), album, sizeof(album), &track));
    assert(m4a_open(&m, fd, size) < 0);
    sceIoClose(fd);

    check_broken("front.m4a");
    check_broken("tail.m4a");
    printf("m4a: alla tester gick igenom\n");
    return 0;
}

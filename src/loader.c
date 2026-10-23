/* psp5 – made by Mille Weber */
/* Bakgrundstråd som laddar bilder i prioritetsordning:
 * synliga ikoner → begärd bakgrund → ikoner lite längre bort.
 * Bara huvudtråden frigör, och bara platser i läget READY/MISSING. */
#include <pspkernel.h>
#include <stdlib.h>
#include <string.h>
#include "loader.h"
#include "art.h"
#include "music.h"

#define NEAR_RANGE 5  /* laddas före bakgrunden (raden visar ~6 kort) */
#define LOAD_RANGE 14 /* laddas i förväg (biblioteket visar ~17 kort) */
#define KEEP_RANGE 18 /* längre bort än så frigörs */

static const EntryList *s_list;
static IconSlot *s_slots;
static u8 *s_keep;
static SceUID s_thread = -1;
static volatile int s_quit;
static volatile int s_focus;
static const LoaderView *volatile s_view;

static volatile int s_bg_want = -1;
static volatile int s_bg_done; /* 1 = s_bg_tex/s_bg_index väntar på att hämtas */
static Texture *volatile s_bg_tex;
static Texture *volatile s_bg_logo; /* PIC0, laddas tillsammans med bakgrunden */
static volatile int s_bg_index = -1;

/* omslaget till låten som spelas (index i musiklistan) – samma mönster som bakgrunden */
static volatile int s_cov_want = -1;
static volatile int s_cov_done;
static Texture *volatile s_cov_tex;
static volatile int s_cov_index = -1;

static int load_icon_near(int range)
{
    const LoaderView *v = s_view; /* en ögonblicksbild – fliken kan bytas under tiden */
    int focus = s_focus;
    if (!v)
        return 0;
    for (int d = 0; d <= range; d++) {
        for (int sign = 0; sign < 2; sign++) {
            int p = sign ? focus - d : focus + d;
            if (p < 0 || p >= v->count)
                continue;
            int i = v->idx[p];
            if (i < 0 || i >= s_list->count)
                return 0; /* shortcut: fliken byggdes om under tiden (ui_relist) – fångar inte allt, ett lås behövs om det syns krascher */
            if (s_slots[i].state != SLOT_EMPTY)
                continue;
            s_slots[i].state = SLOT_LOADING;
            u32 accent = 0;
            Texture *t = art_load_icon(&s_list->items[i], &accent);
            s_slots[i].tex = t;
            s_slots[i].accent = accent;
            s_slots[i].shown = 0;
            s_slots[i].state = t ? SLOT_READY : SLOT_MISSING;
            return 1;
        }
    }
    return 0;
}

static int load_bg(void)
{
    if (s_bg_done) /* förra bilden har inte hämtats än – läs önskemålet först efter det */
        return 0;
    int want = s_bg_want;
    if (want < 0 || want >= s_list->count)
        return 0;
    s_bg_tex = art_load_bg(&s_list->items[want]);
    s_bg_logo = art_load_logo(&s_list->items[want]);
    s_bg_index = want;
    s_bg_done = 1;
    return 1;
}

static int load_cover(void)
{
    if (s_cov_done)
        return 0;
    int want = s_cov_want;
    const Track *t = want >= 0 ? music_track(want) : NULL;
    if (!t)
        return 0;
    s_cov_tex = art_load_cover(t->path, t->album);
    s_cov_index = want;
    s_cov_done = 1;
    return 1;
}

static int loader_thread(SceSize args, void *argp)
{
    while (!s_quit) {
        if (load_icon_near(NEAR_RANGE) || load_bg() || load_cover() || load_icon_near(LOAD_RANGE))
            continue;
        sceKernelDelayThread(8 * 1000);
    }
    return 0;
}

void loader_start(const EntryList *list)
{
    int n = list->count ? list->count : 1;
    s_list = list;
    s_slots = calloc(n, sizeof(IconSlot));
    s_keep = calloc(n, 1);
    s_view = NULL;
    s_quit = 0;
    s_focus = 0;
    s_bg_want = s_bg_index = -1;
    s_cov_want = s_cov_index = -1;
    s_cov_done = 0;
    s_cov_tex = NULL;
    s_bg_done = 0;
    s_bg_tex = s_bg_logo = NULL;
    /* lägre prioritet (högre siffra) än huvudtråden så att animationerna går först */
    s_thread = sceKernelCreateThread("psp5_loader", loader_thread, 0x30, 0x10000, PSP_THREAD_ATTR_USER, NULL);
    if (s_thread >= 0)
        sceKernelStartThread(s_thread, 0, NULL);
}

void loader_stop(void)
{
    if (s_thread >= 0) {
        s_quit = 1;
        sceKernelWaitThreadEnd(s_thread, NULL);
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }
    if (s_slots) {
        for (int i = 0; i < s_list->count; i++)
            tex_free(s_slots[i].tex);
        free(s_slots);
        s_slots = NULL;
    }
    free(s_keep);
    s_keep = NULL;
    s_view = NULL;
    if (s_bg_done) {
        tex_free(s_bg_tex);
        tex_free(s_bg_logo);
    }
    s_bg_tex = s_bg_logo = NULL;
    s_bg_done = 0;
    if (s_cov_done)
        tex_free(s_cov_tex);
    s_cov_tex = NULL;
    s_cov_done = 0;
}

void loader_set_view(const LoaderView *view)
{
    s_view = view;
}

void loader_focus(int view_pos)
{
    const LoaderView *v = s_view;
    s_focus = view_pos;
    if (!v || !s_slots)
        return;

    memset(s_keep, 0, s_list->count);
    for (int p = view_pos - KEEP_RANGE; p <= view_pos + KEEP_RANGE; p++)
        if (p >= 0 && p < v->count)
            s_keep[v->idx[p]] = 1;

    for (int i = 0; i < s_list->count; i++) {
        IconSlot *s = &s_slots[i];
        if (!s_keep[i] && (s->state == SLOT_READY || s->state == SLOT_MISSING)) {
            tex_free(s->tex);
            s->tex = NULL;
            s->state = SLOT_EMPTY;
        }
    }
}

IconSlot *loader_icon(int list_index)
{
    if (!s_slots || list_index < 0 || list_index >= s_list->count)
        return NULL;
    return &s_slots[list_index];
}

void loader_request_bg(int list_index)
{
    s_bg_want = list_index;
}

int loader_take_bg(int *list_index, Texture **tex, Texture **logo)
{
    if (!s_bg_done)
        return 0;
    *list_index = s_bg_index;
    *tex = s_bg_tex;
    *logo = s_bg_logo;
    s_bg_tex = s_bg_logo = NULL;
    /* önskemålet är uppfyllt. Kastar UI:t bilden (markeringen hann flytta) frågar det igen
     * när det vill ha den – tidigare blev en sådan bild aldrig laddad igen. Nollas före
     * s_bg_done, så att tråden inte hinner läsa det gamla önskemålet en gång till. */
    if (s_bg_want == s_bg_index)
        s_bg_want = -1;
    s_bg_done = 0;
    return 1;
}

void loader_request_cover(int track)
{
    s_cov_want = track;
}

int loader_take_cover(int *track, Texture **tex)
{
    if (!s_cov_done)
        return 0;
    *track = s_cov_index;
    *tex = s_cov_tex;
    s_cov_tex = NULL;
    if (s_cov_want == s_cov_index) /* uppfyllt – se loader_take_bg */
        s_cov_want = -1;
    s_cov_done = 0;
    return 1;
}

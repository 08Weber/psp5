/* psp5 authentic – the decisions of the PSP's own decoder's route (see hwpipe.h) */
#include "hwpipe.h"

#include <string.h>

void hwp_init(HwPipe *p, int nbuf)
{
    memset(p, 0, sizeof(*p));
    p->nbuf = nbuf;
}

void hwp_clear(HwPipe *p)
{
    p->npend = 0;
    p->nout = 0;
    p->flush = 0;
}

HwAct hwp_act(const HwPipe *p, double clock, int left)
{
    if (p->nout > 0)
        return p->out[0].pts <= clock + 0.004 ? HWP_PUBLISH : HWP_WAIT; /* (nothing is given while a picture waits) */
    if (left > 0)
        return HWP_FEED;
    if (p->npend > 0 && p->flush < HWP_FLUSH_MAX)
        return HWP_FLUSH;
    return HWP_END;
}

HwOut hwp_pop_out(HwPipe *p)
{
    HwOut o = p->out[0];
    for (int i = 1; i < p->nout; i++)
        p->out[i - 1] = p->out[i];
    p->nout--;
    return o;
}

/* the k-th smallest waiting time (0 = the smallest), or -1e9 */
static double kth(const HwPipe *p, int k)
{
    double sorted[HWP_PEND];
    int n = p->npend;
    for (int i = 0; i < n; i++)
        sorted[i] = p->pend[i];
    for (int i = 1; i < n; i++) { /* (a few numbers: insertion sort) */
        double t = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > t) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = t;
    }
    return k < n ? sorted[k] : -1e9;
}

static int take_min(HwPipe *p, double *t)
{
    if (p->npend <= 0)
        return 0;
    int m = 0;
    for (int i = 1; i < p->npend; i++)
        if (p->pend[i] < p->pend[m])
            m = i;
    *t = p->pend[m];
    p->pend[m] = p->pend[p->npend - 1];
    p->npend--;
    return 1;
}

static int is_unseen(double t, double skip_until) { return t + 0.034 < skip_until - 0.03; }

void hwp_plan(HwPipe *p, double pts_in, double skip_until, int front, int rdy, int dst[HWP_PIC])
{
    if (pts_in >= 0) {
        if (p->npend == HWP_PEND) { /* (more samples than a decoder is ever behind: the smallest time is dropped) */
            double dropped;
            take_min(p, &dropped);
        }
        p->pend[p->npend++] = pts_in;
    } else {
        p->flush++;
    }
    int used[HWP_OUT + 2 + HWP_PIC], nu = 0;
    for (int i = 0; i < p->nout; i++)
        used[nu++] = p->out[i].buf;
    if (front >= 0)
        used[nu++] = front;
    if (rdy >= 0)
        used[nu++] = rdy;
    for (int k = 0; k < HWP_PIC; k++) {
        dst[k] = -1;
        double t = kth(p, k);
        if (t < -1e8 || is_unseen(t, skip_until))
            continue;
        for (int b = 0; b < p->nbuf; b++) {
            int free_b = 1;
            for (int i = 0; i < nu; i++)
                free_b &= used[i] != b;
            if (free_b) {
                dst[k] = b;
                used[nu++] = b;
                break;
            }
        }
    }
}

void hwp_done(HwPipe *p, int pics, const int dst[HWP_PIC], double skip_until)
{
    for (int k = 0; k < pics; k++) {
        double t;
        if (!take_min(p, &t))
            break; /* (more pictures than samples waiting: what a key frame given again at the end makes) */
        if (k < HWP_PIC && dst[k] >= 0 && !is_unseen(t, skip_until) && p->nout < HWP_OUT) {
            int i = p->nout++;
            while (i > 0 && p->out[i - 1].pts > t) { /* (they come in display order; kept in order whatever happens) */
                p->out[i] = p->out[i - 1];
                i--;
            }
            p->out[i].buf = dst[k];
            p->out[i].pts = t;
        }
    }
}

double hwp_front_pending(const HwPipe *p)
{
    double t = kth(p, 0);
    return t < -1e8 ? -1 : t;
}

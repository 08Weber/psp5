/* psp5 authentic – the mix of a many-channel sound down to two channels (see downmix.h) */
#include "downmix.h"

#define SQ 0.70710678f

void dm_weights(const unsigned char *role, int n, float *wl, float *wr)
{
    int side = 0, back = 0;
    for (int i = 0; i < n; i++) {
        if (role[i] == DM_SL || role[i] == DM_SR)
            side = 1;
        if (role[i] == DM_BL || role[i] == DM_BR)
            back = 1;
    }
    float surround = side && back ? 0.5f : SQ; /* two surround pairs share what one pair would give */
    float gain = n > 2 ? DM_GAIN : 1.0f;
    for (int i = 0; i < n; i++) {
        float l = 0, r = 0;
        switch (role[i]) {
        case DM_FL: l = 1; break;
        case DM_FR: r = 1; break;
        case DM_FC: l = r = n == 1 ? 1.0f : SQ; break; /* (a mono sound is on both sides as it is) */
        case DM_FLC: l = SQ; break;
        case DM_FRC: r = SQ; break;
        case DM_BL: case DM_SL: l = surround; break;
        case DM_BR: case DM_SR: r = surround; break;
        case DM_BC: l = r = 0.5f; break;
        default: break; /* the low-frequency channel and what is above are left out */
        }
        wl[i] = l * gain;
        wr[i] = r * gain;
    }
}

int dm_roles_mask(unsigned mask, int n, unsigned char *role)
{
    static const unsigned char BIT[11] = { DM_FL, DM_FR, DM_FC, DM_LFE, DM_BL, DM_BR, DM_FLC, DM_FRC, DM_BC, DM_SL, DM_SR };
    int k = 0;
    for (int bit = 0; bit < 32; bit++) {
        if (!(mask >> bit & 1u))
            continue;
        if (k >= n || k >= DM_MAX)
            return 0;
        role[k++] = bit < 11 ? BIT[bit] : DM_NONE; /* (above the listener: not mixed) */
    }
    return k == n;
}

int dm_roles_default(int n, unsigned char *role)
{
    static const unsigned char L3[3] = { DM_FL, DM_FR, DM_FC };
    static const unsigned char L4[4] = { DM_FL, DM_FR, DM_BL, DM_BR };
    static const unsigned char L5[5] = { DM_FL, DM_FR, DM_FC, DM_BL, DM_BR };
    static const unsigned char L6[6] = { DM_FL, DM_FR, DM_FC, DM_LFE, DM_BL, DM_BR };
    static const unsigned char L7[7] = { DM_FL, DM_FR, DM_FC, DM_LFE, DM_BC, DM_SL, DM_SR };
    static const unsigned char L8[8] = { DM_FL, DM_FR, DM_FC, DM_LFE, DM_BL, DM_BR, DM_SL, DM_SR };
    const unsigned char *l = n == 3 ? L3 : n == 4 ? L4 : n == 5 ? L5 : n == 6 ? L6 : n == 7 ? L7 : n == 8 ? L8 : 0;
    if (!l)
        return 0;
    for (int i = 0; i < n; i++)
        role[i] = l[i];
    return 1;
}

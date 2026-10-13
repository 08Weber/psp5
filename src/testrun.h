/* psp5 – made by Mille Weber */
#ifndef TESTRUN_H
#define TESTRUN_H

#include <psptypes.h>

/* Testläge (bara i byggen med -DPSP5_TEST): kör ms0:/PSP5/test.txt
 *   wait N         vänta N bildrutor
 *   press KNAPP    cross circle triangle square up down left right l r start select
 *   hold KNAPP:N   håll knappen i N bildrutor
 *   shot NAMN      be testskriptet fotografera fönstret (ms0:/PSP5/shots/NAMN.req)
 *   sleep 1        låtsas att PSP:n går in i viloläge
 *   resume 1       låtsas att PSP:n vaknade ur viloläge
 *   volume N       låtsas att systemvolymen är N (0–30, +256 = ljudet av)
 *   charging 0/1   låtsas att batteriet laddas
 *   battery N      låtsas att batteriet har N %
 */
#ifdef PSP5_TEST
void testrun_init(void);
u32 testrun_buttons(u32 real);            /* ersätter knapparna när skriptet körs */
void testrun_frame(const void *framebuf); /* anropas efter varje färdig bildruta */
int testrun_take_resume(void);            /* 1 en gång efter "resume" i skriptet */
int testrun_take_sleep(void);             /* 1 en gång efter "sleep" i skriptet */
int testrun_volume(void);                /* låtsad volym (se ovan), -1 = riktig */
int testrun_charging(void);              /* låtsad laddning 0/1, -1 = riktig */
int testrun_battery(void);               /* låtsad batterinivå i %, -1 = riktig */
#else
static inline void testrun_init(void) {}
static inline u32 testrun_buttons(u32 real) { return real; }
static inline void testrun_frame(const void *framebuf) {}
static inline int testrun_take_resume(void) { return 0; }
static inline int testrun_take_sleep(void) { return 0; }
static inline int testrun_volume(void) { return -1; }
static inline int testrun_charging(void) { return -1; }
static inline int testrun_battery(void) { return -1; }
#endif

#endif

/* psp5 – made by Mille Weber */
#ifndef TESTRUN_H
#define TESTRUN_H

#include <psptypes.h>

/* Testläge (bara i byggen med -DPSP5_TEST): kör ms0:/PSP5/test.txt
 *   wait N         vänta N bildrutor
 *   press KNAPP    cross circle triangle square up down left right l r start select
 *   shot NAMN      be testskriptet fotografera fönstret (ms0:/PSP5/shots/NAMN.req)
 *   resume 1       låtsas att PSP:n vaknade ur viloläge
 */
#ifdef PSP5_TEST
void testrun_init(void);
u32 testrun_buttons(u32 real);            /* ersätter knapparna när skriptet körs */
void testrun_frame(const void *framebuf); /* anropas efter varje färdig bildruta */
int testrun_take_resume(void);            /* 1 en gång efter "resume" i skriptet */
#else
static inline void testrun_init(void) {}
static inline u32 testrun_buttons(u32 real) { return real; }
static inline void testrun_frame(const void *framebuf) {}
static inline int testrun_take_resume(void) { return 0; }
#endif

#endif

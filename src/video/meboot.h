/* psp5 authentic – the PSP's Media Engine firmware for the hardware video decoder.
 *
 * The decoder of the PSP's sceMpeg library runs on the Media Engine (the second processor) and the Engine is started with one of several firmware images, chosen by sceMeBootStart(type), a function of the
 * kernel's sceMeCore driver. The default image takes the streams of the PSP's own movies (CABAC) and answers the fatal error 0x80628002 to CAVLC (the maintainer's tests of a Baseline stream and of the
 * HandBrake PSP preset). PMPlayer Ultraviolet (cooleyes' player, GPL) starts the Engine with type 4 for a Baseline stream, 3 for a Main one and 1 for 720 x 480, before it opens the library, and its README
 * says Baseline streams with one reference frame play: that is the route this file opens for psp5 (video-opts.txt, "hw=1"). Nothing of that player's code is in psp5; only that fact is.
 *
 * sceMeBootStart is a kernel function: psp5 reaches it the way it reaches the volume of the OSD, through ARK's kubridge (kuKernelCall) and the address that systemctrl gives for a NID. It is never
 * called while a decoder of the Engine is open (the music is stopped before a video starts). */
#ifndef MEBOOT_H
#define MEBOOT_H

#define MEBOOT_MAIN 3     /* the Engine as the Main profile streams of the library want it (what PMPlayer Ultraviolet starts a Main stream with) */
#define MEBOOT_BASELINE 4 /* ... and for a Baseline stream (CAVLC) */

/* 1 when the function can be found (ARK on a 6.60 / 6.61 PSP), else 0; the NID it was found with (0 before the first call of meboot_start() / meboot_available()) */
int meboot_available(void);
unsigned meboot_nid(void);
/* for the log: the devkit version of the system, which of the NIDs of the table (bit n = entry n) were found in the library (whatever the system's own is), and which module name found it (1 = the first,
 * 0 = none) */
int meboot_devkit(void);
unsigned meboot_mask(void);
int meboot_module(void);

/* Starts the Engine with the firmware image `type`. 0 when it is done (or the Engine has that image already), -1 when the function cannot be found, else the (negative) answer of the kernel. */
int meboot_start(int type);

/* the image this program started the Engine with last (0 when it never did: the one the system started it with) */
int meboot_current(void);

#endif

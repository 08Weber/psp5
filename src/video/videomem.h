/* psp5 authentic – where a video was left: the file name and the length (in whole seconds) tell which video it is, and the place is kept in videomem.txt in psp5's own folder. A place in the
 * first 5 seconds or the last 10 seconds is not kept (the video was not started, or it was finished). The last 200 videos are remembered. */
#ifndef VIDEOMEM_H
#define VIDEOMEM_H

/* 1 and the place in seconds when the video was left in the middle */
int videomem_get(const char *name, int len_s, float *pos);
/* remembers where the video is now (or forgets it when that is too near the start or the end) */
void videomem_set(const char *name, int len_s, float pos);
void videomem_forget(const char *name, int len_s);

#endif

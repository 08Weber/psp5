# Video engine integration notes

The engine's picture descriptor is `VideoFrame` in `video.h`. It owns the
video-only `opaque` and `vram` flags and is deliberately separate from the
shared `Texture` in `../gfx.h`. The later renderer integration must adapt
`video_picture()`'s `VideoFrame` to the graphics API; `videoplay.c` currently
expects a `Texture` and is not part of the build in this phase.

`logs_init()` is intentionally not called by the engine. During player
integration, connect it to the application's log-system lifecycle before the
player can call `video_open()` or any `logs_*()` routine. Do not initialize it
from `video_open()`; it owns a process-wide writer thread and recovery of prior
run markers.

The project and imported engine carry GPL-3.0 notices/licenses. Keep the
upstream copyright and license notices with imported files and include the
GPL-3.0 license and corresponding source when distributing the combined work.

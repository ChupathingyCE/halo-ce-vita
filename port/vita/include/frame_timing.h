/*
FRAME_TIMING.H

The frame and tick times the renderer's on-screen counter shows (d3d8_gxm.c).
Halo CE Vita measured them in its own main loop; on Warthog the Vita side
keeps them itself (platform/vita_glue.c).
*/

#ifndef __VITA_FRAME_TIMING_H
#define __VITA_FRAME_TIMING_H

void halo_frame_timing_recent(float *frame_ms, float *tick_ms, float *render_ms);

#endif

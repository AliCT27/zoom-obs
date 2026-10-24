#pragma once

#include <obs-module.h>

/* A canvas-sized source that draws a thin progress bar along the top or bottom edge of the video. It fills up over the
 * planned length of the video while recording and draws nothing otherwise. */
namespace zoominator_progress {

struct Style {
	float lengthSec = 90.0f;
	float thicknessPx = 8.0f; /* for a 1080-pixel-wide canvas */
	int position = 0;         /* 0 top edge, 1 bottom edge */
	float color[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	bool rainbow = false;
	float rainbowSec = 10.0f;
};

/* Registers the source type. Call once from obs_module_load. */
void registerSource();

/* Creates the private source, or nullptr when it cannot be created. Main thread only. */
obs_source_t *create();

/* Safe to call from any thread. */
void setStyle(const Style &style);

/* How far the recording is: active = a recording exists (running or paused), running = time is passing, baseMs = the
 * recorded time at this moment. The source carries the time on from here by itself. Safe from any thread. */
void setTime(bool active, bool running, double baseMs);

} // namespace zoominator_progress

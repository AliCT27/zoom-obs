#pragma once

#include <obs-module.h>

/* A canvas-sized source that draws the cursor effects (for now: a ring around the mouse cursor). It is added to the
 * current scene like the frame, and the main thread tells it where the cursor is. */
namespace zoominator_cursorfx {

struct Style {
	float sizePx = 90.0f; /* ring diameter at 1x, in canvas pixels */
	float thicknessPx = 6.0f;
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

/* Where the cursor is, in canvas pixels, and how much the view is zoomed (the ring grows with it). Safe from any thread. */
void setCursor(bool visible, double x, double y, double zoom);

} // namespace zoominator_cursorfx

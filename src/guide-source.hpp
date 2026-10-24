#pragma once

#include <obs-module.h>

/* A source that draws the safe-zone guide for vertical videos: the areas that the Reels interface covers (top,
 * bottom, sides) are tinted and the safe area is outlined. It is as big as the canvas, is only shown while nothing
 * is being recorded or streamed, and is never part of the output. */
namespace zoominator_guide {

struct Style {
	int topPx = 270;
	int bottomPx = 670;
	int leftPx = 65;
	int rightPx = 65;
};

/* Registers the source type. Call once from obs_module_load. */
void registerSource();

/* Creates the private source, or nullptr when it cannot be created. Main thread only. */
obs_source_t *create();

/* Safe to call from any thread. */
void setStyle(const Style &style);

} // namespace zoominator_guide

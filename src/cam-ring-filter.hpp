#pragma once

#include <obs-module.h>

/* A video filter that turns a webcam source into a circle with a ring and a soft
 * glow around it. The controller attaches it to the chosen source while the
 * option is on and removes it again when it is turned off. */
namespace zoominator_camring {

struct Style {
	float ringColor[4] = {0.18f, 0.66f, 0.86f, 1.0f};
	float ringPx = 5.0f;
	float glowPx = 24.0f;
	float glowStrength = 0.8f;
	bool rainbow = false;     /* ring and glow colour cycle round the colour wheel */
	bool flow = false;        /* ...running round the ring like an LED strip */
	float rainbowSec = 10.0f; /* seconds for one full turn */
};

/* Registers the filter type. Call once from obs_module_load. */
void registerFilter();

/* Adds the filter to `target` (or reuses one already there) and returns a strong
 * reference, or nullptr if it could not be created or its shader failed to
 * load. Main thread only. */
obs_source_t *attach(obs_source_t *target);

/* Removes the filter from `target` and drops the reference attach() returned. */
void detach(obs_source_t *target, obs_source_t *filter);

/* Safe to call from any thread. */
void setStyle(obs_source_t *filter, const Style &style);

} // namespace zoominator_camring

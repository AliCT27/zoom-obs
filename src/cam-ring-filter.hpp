#pragma once

#include <obs-module.h>

/* A video filter that turns a webcam source into a circle (or a rounded square) with a ring and a soft
 * glow around it. The controller attaches it to the chosen source while the
 * option is on and removes it again when it is turned off. */
namespace zoominator_camring {

struct Style {
	float ringColor[4] = {0.18f, 0.66f, 0.86f, 1.0f};
	float ringPx = 5.0f;
	float glowPx = 24.0f;
	float glowStrength = 0.8f;
	float corner = 1.0f;      /* 1 = circle, 0 = sharp square corners */
	bool shadow = false;      /* drop shadow behind the whole shape (picture, ring and glow) */
	float shadowColor[3] = {0.0f, 0.0f, 0.0f};
	float shadowBlurPx = 24.0f;
	float shadowDx = 0.0f;    /* offset in pixels, x right, y down */
	float shadowDy = 12.0f;
	float shadowAlpha = 0.5f;
	bool rainbow = false;     /* ring and glow colour cycle round the colour wheel */
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

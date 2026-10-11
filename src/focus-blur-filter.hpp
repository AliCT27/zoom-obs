#pragma once

#include <obs-module.h>

/* A video filter that blurs everything outside a circle and leaves the circle
 * sharp. The zoom controller attaches one to each zoomed source while a zoom is
 * running (Focus style "Blur") and removes it again afterwards, so it does not
 * stay in the user's filter lists.
 *
 * All coordinates passed to setParams() are in pixels of the source the filter
 * is attached to. */
namespace zoominator_blur {

/* Registers the filter type. Call once from obs_module_load. */
void registerFilter();

/* Adds a filter to `target` (or reuses one already there) and returns a strong
 * reference to it, or nullptr when the filter could not be created or its
 * shader failed to load. Main thread only. */
obs_source_t *attach(obs_source_t *target);

/* Removes the filter from `target` and drops the reference attach() returned. */
void detach(obs_source_t *target, obs_source_t *filter);

/* Safe to call from any thread; the render thread picks the values up on the
 * next frame. strength 0 makes the filter a pass-through. */
void setParams(obs_source_t *filter, float centerX, float centerY, float radius, float softness, float blurRadius,
	       float strength);

} // namespace zoominator_blur

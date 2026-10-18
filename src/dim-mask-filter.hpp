#pragma once

#include <obs-module.h>

/* A video filter for the focus dim image: keeps the dim only where the captured screen is (its flat rectangle, or
 * the turned shape with the 3D camera), so the background around the screen is not darkened. */
namespace zoominator_dimmask {

void registerFilter();

/* Adds the filter to `target` (or reuses it); strong ref or nullptr. Main thread only. */
obs_source_t *attach(obs_source_t *target);
void detach(obs_source_t *target, obs_source_t *filter);

/* Thread safe. The screen's flat rectangle on the canvas (empty / invalid = do not mask). */
void setSheet(float left, float top, float right, float bottom);
void clearSheet();
/* Thread safe. Where the dim image is drawn: its centre and scale on the canvas. */
void setItem(float centerX, float centerY, float scale);

} // namespace zoominator_dimmask

#pragma once

#include <obs-module.h>


/* A video filter that shows its source as a flat sheet turned in 3D, so the
 * captured screen can lean towards the mouse. The controller keeps one on each
 * target source while "3D camera" is enabled and removes it again afterwards. */
namespace zoominator_tilt {

void registerFilter();

/* Adds a filter to `target` (or reuses one already there); strong ref or nullptr. Main thread only. */
obs_source_t *attach(obs_source_t *target);
void detach(obs_source_t *target, obs_source_t *filter);

/* Thread safe. Angles in radians: yaw turns around the vertical axis, pitch around the horizontal one.
 * perspective is the camera distance in multiples of the source's longer side. */
void setParams(obs_source_t *filter, float yaw, float pitch, float perspective);

/* Shared by the filter, the frame and the focus / halo placement so they all agree (defined in tilt-filter.cpp).
 * The turned sheet shrinks a little so the edge swinging towards the camera stays inside its frame. */
float sheetScale(float yaw, float pitch, float persp, float w, float h);

/* Where a point of the flat sheet ends up once it is turned: (x, y) is the offset from the sheet's centre
 * in source pixels; replaced by the offset of the displayed point in the same units. */
void projectPoint(double yaw, double pitch, double persp, double w, double h, double &x, double &y);

} // namespace zoominator_tilt

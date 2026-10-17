#pragma once

#include <obs-module.h>

#include <algorithm>
#include <cmath>

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

/* Shared by the filter, the frame and the focus / halo placement so they all agree.
 * The turned sheet shrinks a little so the edge swinging towards the camera stays inside its frame. */
inline float sheetScale(float yaw, float pitch, float persp, float w, float h)
{
	const float longSide = std::max(w, h);
	const float dist = std::max(0.5f, persp) * longSide;
	const float swing = 0.5f * longSide * std::max(std::fabs(std::sin(yaw)), std::fabs(std::sin(pitch)));
	return std::clamp((dist - swing) / dist, 0.3f, 1.0f);
}

/* Where a point of the flat sheet ends up once it is turned: (x, y) is the offset from the sheet's centre
 * in source pixels; replaced by the offset of the displayed point in the same units. */
inline void projectPoint(double yaw, double pitch, double persp, double w, double h, double &x, double &y)
{
	const double longSide = std::max(w, h);
	const double dist = std::max(0.5, persp) * longSide;
	const double sa = std::sin(yaw), ca = std::cos(yaw), sb = std::sin(pitch), cb = std::cos(pitch);
	const double xr = x * ca + y * sb * sa;
	const double yr = y * cb;
	const double zr = -x * sa + y * sb * ca;
	const double depth = std::max(1.0, dist + zr);
	const double k = dist / depth * (double)sheetScale((float)yaw, (float)pitch, (float)persp, (float)w, (float)h);
	x = xr * k;
	y = yr * k;
}

} // namespace zoominator_tilt

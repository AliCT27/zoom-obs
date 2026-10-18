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

/* Shared description of the turned screen, so that things drawn outside the filter (the Frame, the focus
 * spotlight, the cursor halo) can follow the same 3D turn. All sizes are canvas pixels. */
struct Pose {
	float yaw = 0.0f, pitch = 0.0f;
	float cx = 0.0f, cy = 0.0f; /* centre of the screen sheet */
	float dist = 1.0f;          /* camera distance */
	float scl = 1.0f;           /* shrink applied so the turned sheet stays in frame */
};

/* Turn angles and camera distance, published every main-thread tick. Thread safe. */
void setAngles(float yaw, float pitch, float perspective);
/* The screen sheet as it is on the canvas right now (uncropped bounds). Thread safe. */
void setSheet(float left, float top, float right, float bottom);
void clearSheet();
/* False when nothing is turned (no sheet, or angles ~ 0). */
bool getPose(Pose &out);
/* Where a point on the flat sheet ends up after the turn, and how much the picture is scaled there. */
void project(const Pose &pose, float x, float y, float &outX, float &outY, float &localScale);

} // namespace zoominator_tilt
